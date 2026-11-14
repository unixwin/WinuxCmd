//! AST for the winux-regex engine.
//!
//! Ported from `src/utils/regex.cppm` (portable_regex) `detail::Node` /
//! `detail::ClassAtom`. The C++ engine stores nodes in a flat arena indexed
//! by `size_t`; here the same node set becomes a boxed tree, which keeps the
//! match/parse recursion identical in shape while dropping the arena
//! bookkeeping.
//!
//! Grammar coverage: POSIX BRE + GNU BRE extensions (`\| \+ \? \b \B \< \>
//! \s \S \w \W`), POSIX ERE + GNU ERE extensions (backreferences), POSIX
//! bracket classes with `[:class:]`, `[[.coll.]]`, `[[=equiv=]]`. Perl
//! syntax is deliberately out of scope (grep `-P` routes to PCRE2, see
//! DOCS/2.0-regex-engine-decision.md).

/// Quantifier bound: `Some(n)` is `{n}`, `None` is unbounded.
pub type Max = Option<u32>;

/// glibc `RE_DUP_MAX`: repetition counts above this are a compile error
/// ("Regular expression too big" in GNU grep), both in BRE `\{...\}` and
/// ERE `{...}`. Measured against GNU grep 3.11: `a\{32767\}` compiles,
/// `a\{32768\}` does not.
pub const RE_DUP_MAX: u32 = 32_767;

/// One member of a bracket expression's set.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum ClassAtom {
    /// A single byte (backslash is an ordinary member in GNU brackets).
    Literal(u8),
    /// Inclusive byte range `first..=last` (endpoints may be backslashes).
    Range(u8, u8),
    /// `[:name:]` POSIX class, C-locale byte semantics.
    Posix(PosixClass),
}

/// C-locale POSIX character class names accepted inside `[...]`.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum PosixClass {
    Alnum,
    Alpha,
    Blank,
    Cntrl,
    Digit,
    Graph,
    Lower,
    Print,
    Punct,
    Space,
    Upper,
    XDigit,
    Word,
}

impl PosixClass {
    /// Parse the name of `[:name:]`. Unknown names are a compile error in
    /// GNU grep ("Invalid character class name"); the C++ engine accepted
    /// them and never matched, which diverges from GNU. NOTE: `word` is
    /// deliberately absent — GNU grep 3.11 rejects `[[:word:]]` ("Invalid
    /// character class name", probed); the word class exists only through
    /// the `\w` shorthand, which maps to [`PosixClass::Word`] directly.
    pub fn from_name(name: &[u8]) -> Option<PosixClass> {
        Some(match name {
            b"alnum" => PosixClass::Alnum,
            b"alpha" => PosixClass::Alpha,
            b"blank" => PosixClass::Blank,
            b"cntrl" => PosixClass::Cntrl,
            b"digit" => PosixClass::Digit,
            b"graph" => PosixClass::Graph,
            b"lower" => PosixClass::Lower,
            b"print" => PosixClass::Print,
            b"punct" => PosixClass::Punct,
            b"space" => PosixClass::Space,
            b"upper" => PosixClass::Upper,
            b"xdigit" => PosixClass::XDigit,
            _ => return None,
        })
    }

    /// C-locale membership table (byte-oriented; ASCII only).
    pub fn matches(self, b: u8) -> bool {
        let c = b as char;
        match self {
            PosixClass::Alnum => c.is_ascii_alphanumeric(),
            PosixClass::Alpha => c.is_ascii_alphabetic(),
            // C locale blank: space and horizontal tab only.
            PosixClass::Blank => b == b' ' || b == b'\t',
            PosixClass::Cntrl => b < 0x20 || b == 0x7f,
            PosixClass::Digit => c.is_ascii_digit(),
            PosixClass::Graph => (0x21..=0x7e).contains(&b),
            PosixClass::Lower => c.is_ascii_lowercase(),
            PosixClass::Print => (0x20..=0x7e).contains(&b),
            PosixClass::Punct => (0x21..=0x7e).contains(&b) && !c.is_ascii_alphanumeric(),
            PosixClass::Space => matches!(b, b'\t' | b'\n' | 0x0b | 0x0c | b'\r' | b' '),
            PosixClass::Upper => c.is_ascii_uppercase(),
            PosixClass::XDigit => c.is_ascii_hexdigit(),
            PosixClass::Word => c.is_ascii_alphanumeric() || b == b'_',
        }
    }
}

/// Regex AST, mirroring `detail::NodeKind` in regex.cppm.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum Node {
    /// Matches the empty string.
    Empty,
    Literal(u8),
    /// Any codepoint (never splits a well-formed UTF-8 sequence).
    Dot,
    /// `^` — absolute start of the text.
    AnchorStart,
    /// `$` — absolute end of the text.
    AnchorEnd,
    /// `\<`
    WordStart,
    /// `\>`
    WordEnd,
    /// `\b`
    WordBoundary,
    /// `\B`
    NotWordBoundary,
    /// `^` under [`crate::CompileOptions::multiline`] (glibc REG_NEWLINE
    /// `newline_anchor`): text start or right after the delimiter byte.
    LineStart(u8),
    /// `$` under multiline: text end or right before the delimiter byte.
    LineEnd(u8),
    /// GNU `` \` `` — absolute buffer start, also in multiline mode.
    BufferStart,
    /// GNU `\'` — absolute buffer end, also in multiline mode.
    BufferEnd,
    /// `.` under the sed dialect options: like [`Node::Dot`], but it never
    /// matches newline when `exclude_newline` (REG_NEWLINE clears
    /// RE_DOT_NEWLINE) and never matches a byte that does not start a
    /// well-formed UTF-8 sequence when `reject_invalid_utf8` (glibc under a
    /// UTF-8 locale: `printf '\377' | sed 's/./X/'` leaves the byte).
    DotRestricted {
        exclude_newline: bool,
        reject_invalid_utf8: bool,
    },
    /// Bracket expression `[...]`.
    CharClass {
        negated: bool,
        atoms: Vec<ClassAtom>,
    },
    /// Concatenation (empty and single-child sequences collapse).
    Sequence(Vec<Node>),
    /// Alternation (empty and single-child trees collapse).
    Alternation(Vec<Node>),
    /// `(...)` / `\(...\)`; `index` is allocated at the OPENING delimiter
    /// (XBD 9.1 left-to-right numbering; glibc-aligned, regex.cppm #1496).
    Capture {
        index: usize,
        child: Box<Node>,
    },
    /// `\1`..`\9`.
    BackReference(u32),
    /// Greedy repetition `child{min, max}` (max `None` = unbounded).
    Repeat {
        min: u32,
        max: Max,
        child: Box<Node>,
    },
}
