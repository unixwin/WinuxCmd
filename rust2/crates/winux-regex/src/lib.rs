//! winux-regex — GNU-aligned BRE/ERE engine for the WinuxCmd 2.0 rust2
//! commands (grep/sed/find/expr/csplit consumers; see
//! `DOCS/2.0-regex-engine-decision.md`, decision A of issue #1488).
//!
//! This is a semantic port of the C++ `portable_regex` engine
//! (`src/utils/regex.cppm`), with GNU grep 3.11 (glibc regcomp, LC_ALL=C)
//! re-probed pattern-by-pattern during the port; every divergence found is
//! fixed to the GNU behavior and documented at the fix site (see
//! `parser.rs` for the list).
//!
//! Scope (decision doc §2/§4): POSIX BRE + GNU extensions, POSIX ERE + GNU
//! extensions, backreferences `\1..9`, capture groups, POSIX
//! leftmost-longest matching (NOT leftmost-first), C-locale byte classes.
//! Perl syntax is out of scope: `grep -P` keeps PCRE2 at the command layer.
//! Byte-oriented throughout: `&[u8]` is the primary API; the `&str` forms
//! are thin wrappers. `.` and the word boundaries advance whole UTF-8
//! codepoints (the C++ engine's D7 rule, matching GNU grep under a UTF-8
//! locale).
//!
//! # Contract
//!
//! * std-only, zero dependencies, no process-global state. The C++ engine's
//!   `thread_local work_limit_exceeded` flag becomes
//!   [`ErrorKind::WorkLimit`] returned through the checked find API
//!   (Phase 2); the plain call shape collapses it to `None`.
//!
//! ```
//! use winux_regex::{Regex, Syntax};
//!
//! let re = Regex::compile(Syntax::Basic, r"\(a\|ab\)\+").unwrap();
//! assert_eq!(re.capture_count(), 1);
//! // POSIX leftmost-longest: the whole "ab" wins, not the prefix "a".
//! let m = re.find_at("ab", 0).unwrap();
//! assert_eq!(m.slice_of(b"ab"), b"ab");
//! // GNU rejects `\1` without a group:
//! assert!(Regex::compile(Syntax::Basic, r"\1").is_err());
//! ```

pub mod ast;
pub mod dfa;
pub(crate) mod fastpath;
pub mod matcher;
pub mod parser;
mod pike;

use std::fmt;

pub use pike::MatchCache;

use fastpath::FastPath;

/// Regex flavor. GNU extensions are enabled in both.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Syntax {
    /// `grep` default: `\(\)`, `\|`, `\+`, `\?`, `\{m,n\}`, `\1`..`\9`,
    /// context-dependent `^`/`$`/`*` literals per POSIX BRE.
    Basic,
    /// `grep -E`: `()`, `|`, `+`, `?`, `{m,n}`, GNU backreferences,
    /// context-independent anchors.
    Extended,
}

/// Dialect options beyond the grep defaults. `CompileOptions::default()`
/// reproduces [`Regex::compile_bytes`] exactly; every field is opt-in so
/// existing consumers keep their behavior (additive API for the sed port,
/// WinuxCmd #1488).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct CompileOptions {
    /// ASCII case folding (grep `-i`, sed `I`).
    pub ignore_case: bool,
    /// glibc REG_NEWLINE (sed `M`): `^`/`$` also match around
    /// [`CompileOptions::line_delimiter`], `.` and non-matching bracket
    /// lists never match newline.
    pub multiline: bool,
    /// Byte the multiline anchors split on (`\n`; NUL under `sed -z`).
    pub line_delimiter: u8,
    /// GNU `` \` `` / `\'` buffer anchors (otherwise literal `` ` `` / `'`).
    pub buffer_anchors: bool,
    /// GNU sed's glibc syntax bits: RE_CONTEXT_INVALID_DUP (BRE) /
    /// RE_CONTEXT_INVALID_OPS (ERE), no RE_INVALID_INTERVAL_ORD, and no
    /// RE_UNMATCHED_RIGHT_PAREN_ORD — see `Parser::parse_piece_strict`.
    pub posix_strict: bool,
    /// `.` rejects bytes that do not start a well-formed UTF-8 sequence
    /// (glibc multibyte-locale behavior).
    pub strict_utf8_dot: bool,
    /// glibc RE_NO_GNU_OPS (sed `--posix`): `\w \W \s \S \b \B \< \>`
    /// and the buffer anchors are plain literals; in BRE also
    /// RE_LIMITED_OPS — `\+ \? \|` are literals.
    pub no_gnu_ops: bool,
    /// glibc RE_UNMATCHED_RIGHT_PAREN_ORD (sed under POSIXLY_CORRECT or
    /// `--posix`): a stray ERE `)` / BRE `\)` is a literal `)` even under
    /// [`CompileOptions::posix_strict`].
    pub unmatched_right_paren_ord: bool,
}

impl Default for CompileOptions {
    fn default() -> Self {
        CompileOptions {
            ignore_case: false,
            multiline: false,
            line_delimiter: b'\n',
            buffer_anchors: false,
            posix_strict: false,
            strict_utf8_dot: false,
            no_gnu_ops: false,
            unmatched_right_paren_ord: false,
        }
    }
}

impl CompileOptions {
    /// The GNU sed 4.9 dialect: strict glibc syntax bits, buffer anchors
    /// and the UTF-8 `.` rule, with the per-regex `I`/`M` flags given.
    pub fn sed(ignore_case: bool, multiline: bool) -> CompileOptions {
        CompileOptions {
            ignore_case,
            multiline,
            line_delimiter: b'\n',
            buffer_anchors: true,
            posix_strict: true,
            strict_utf8_dot: true,
            no_gnu_ops: false,
            unmatched_right_paren_ord: false,
        }
    }
}

/// Compile / work-limit error.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Error {
    kind: ErrorKind,
    offset: usize,
    message: &'static str,
}

/// Error taxonomy. The accept/reject boundary is GNU grep's; the message
/// strings are ours (consumers render their own prefixes).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ErrorKind {
    /// Pattern ended after a lone `\`.
    TrailingBackslash,
    /// `(` / `\(` without its closer.
    UnmatchedOpen,
    /// `)` / `\)` without an opener.
    UnmatchedClose,
    /// `[` / `[^` / committed `[:` / `[[.` / `[[=` without its terminator.
    UnmatchedBracket,
    /// `\{m,n\}` malformed or reversed; `{m,n}` reversed after an atom.
    InvalidInterval,
    /// Repetition count above glibc `RE_DUP_MAX` (32767).
    IntervalTooBig,
    /// `\N` beyond the capture count (GNU "Invalid back reference").
    InvalidBackReference,
    /// Unknown `[:name:]`.
    InvalidClassName,
    /// Multi-byte `[[.name.]]` / `[[=name=]]` (C locale has none).
    InvalidCollation,
    /// Range with `start > end`.
    InvalidRangeEnd,
    /// Nesting deeper than the engine limit.
    TooComplex,
    /// The per-call work budget was exhausted: the answer is a CONSERVATIVE
    /// no-match, not a verified one (C++ `work_limit_exceeded`, returned
    /// instead of a thread-local).
    WorkLimit,
}

impl Error {
    pub(crate) fn new(kind: ErrorKind, offset: usize, message: &'static str) -> Error {
        Error {
            kind,
            offset,
            message,
        }
    }

    pub fn kind(&self) -> ErrorKind {
        self.kind
    }

    /// Byte offset into the pattern where the error was detected.
    pub fn offset(&self) -> usize {
        self.offset
    }

    pub fn message(&self) -> &'static str {
        self.message
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} at byte {}", self.message, self.offset)
    }
}

impl std::error::Error for Error {}

/// One capture span: `text[begin..end]` when `matched`.
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub struct Submatch {
    pub begin: usize,
    pub end: usize,
    pub matched: bool,
}

/// A successful match: overall span plus the capture-group spans.
/// `captures` always has `capture_count + 1` entries; index 0 is the whole
/// match and is always set.
#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Match {
    pub begin: usize,
    pub end: usize,
    pub captures: Vec<Submatch>,
}

impl Match {
    /// Span of capture `index` (0 = whole match); unset or unmatched -> None.
    pub fn group(&self, index: usize) -> Option<(usize, usize)> {
        let sub = self.captures.get(index)?;
        if sub.matched {
            Some((sub.begin, sub.end))
        } else {
            None
        }
    }

    /// The matched bytes of `text`.
    pub fn slice_of<'a>(&self, text: &'a [u8]) -> &'a [u8] {
        &text[self.begin..self.end]
    }
}

/// A compiled regex.
#[derive(Debug, Clone)]
pub struct Regex {
    root: ast::Node,
    capture_count: usize,
    ignore_case: bool,
    /// Compile-time literal pre-filters (see [`fastpath`]); a necessary
    /// condition only — every candidate still runs through the unmodified
    /// interpreter. Neutral under `disable_fast_path` / no-default-features.
    fast: FastPath,
    /// Thompson NFA for the bounds-only search (`None` with
    /// backreferences or an oversized interval expansion); see `pike.rs`.
    nfa: Option<pike::Nfa>,
    /// Static capture offsets (relative to the match start) when every
    /// piece of the pattern has a fixed byte width and no group sits under
    /// an alternation or a variable repetition — the spans are then fully
    /// determined by the match start (`s/\(a\)\(b\)/\2\1/`).
    fixed_captures: Option<Vec<(usize, usize)>>,
}

impl Regex {
    /// Compile a pattern (BRE or ERE) with case-sensitive matching.
    pub fn compile(syntax: Syntax, pattern: &str) -> Result<Regex, Error> {
        Self::compile_bytes(syntax, pattern.as_bytes())
    }

    /// Compile with GNU grep `-i` folding (ASCII case-fold; C locale).
    pub fn compile_ignore_case(syntax: Syntax, pattern: &str) -> Result<Regex, Error> {
        Self::compile_bytes_ignore_case(syntax, pattern.as_bytes(), true)
    }

    /// Byte-pattern form of [`Regex::compile`].
    pub fn compile_bytes(syntax: Syntax, pattern: &[u8]) -> Result<Regex, Error> {
        Self::compile_bytes_ignore_case(syntax, pattern, false)
    }

    /// Byte-pattern form of [`Regex::compile_ignore_case`].
    pub fn compile_bytes_ignore_case(
        syntax: Syntax,
        pattern: &[u8],
        ignore_case: bool,
    ) -> Result<Regex, Error> {
        Self::compile_with(
            syntax,
            pattern,
            CompileOptions {
                ignore_case,
                ..CompileOptions::default()
            },
        )
    }

    /// Compile under explicit dialect options (see [`CompileOptions`]).
    pub fn compile_with(
        syntax: Syntax,
        pattern: &[u8],
        opts: CompileOptions,
    ) -> Result<Regex, Error> {
        let ignore_case = opts.ignore_case;
        let p = parser::Parser::with_options(syntax, pattern, opts);
        let (root, capture_count) = p.parse()?;
        let fast = if cfg!(feature = "fast-path") {
            FastPath::build(&root, capture_count, ignore_case)
        } else {
            FastPath::disabled()
        };
        let nfa = pike::Nfa::build(&root, ignore_case);
        let fixed_captures = if capture_count > 0 {
            let mut caps = vec![(0usize, 0usize); capture_count + 1];
            fixed_width(&root, 0, &mut caps).map(|_| caps)
        } else {
            None
        };
        let regex = Regex {
            root,
            capture_count,
            ignore_case,
            fast,
            nfa,
            fixed_captures,
        };
        // GNU: `\1` with no open group is "Invalid back reference".
        if let Some(bad) = max_back_reference(&regex.root) {
            if bad as usize > regex.capture_count {
                return Err(Error::new(
                    ErrorKind::InvalidBackReference,
                    0,
                    "invalid back reference",
                ));
            }
        }
        Ok(regex)
    }

    /// Number of capture groups (group 0 not counted).
    pub fn capture_count(&self) -> usize {
        self.capture_count
    }

    /// Parsed AST (engine inspection for gate tests and tooling).
    pub fn root(&self) -> &ast::Node {
        &self.root
    }

    pub fn ignore_case(&self) -> bool {
        self.ignore_case
    }

    /// Scratch for [`Regex::find_at_bytes_with`] / [`Regex::is_match_with`].
    pub fn new_cache(&self) -> MatchCache {
        MatchCache::new()
    }

    /// Does this regex have the Thompson NFA used for verified boolean and
    /// bounds searches? Patterns without one (back-references, an interval
    /// expansion above the NFA cap) are decided by the backtracker under a
    /// per-call work budget whose exhaustion is a CONSERVATIVE no-match, so
    /// a consumer that must not change verdicts — the sed address DFA
    /// filter, which would otherwise be exact — keeps the interpreter for
    /// them.
    pub fn has_nfa(&self) -> bool {
        self.nfa.is_some()
    }

    /// Does `text` contain a match? Uses the Thompson NFA when the pattern
    /// has one (no backreferences), else the backtracking matcher; the
    /// answer is the same either way (a work-limit no-match of the
    /// backtracker cannot occur on the NFA path).
    pub fn is_match_with(&self, cache: &mut MatchCache, text: &[u8]) -> bool {
        if self.fast.pure_literal {
            return self.find_at_bytes(text, 0).is_some();
        }
        match self.nfa_search(cache, text, 0, true) {
            Some(r) => r.is_some(),
            None => self.find_at_bytes(text, 0).is_some(),
        }
    }

    /// [`Regex::find_at_bytes`] with the NFA locating the leftmost-longest
    /// bounds; the backtracker then runs once, anchored at the match start,
    /// only when the pattern has capture groups (same spans and captures as
    /// `find_at_bytes`, far fewer futile attempts).
    pub fn find_at_bytes_with(
        &self,
        cache: &mut MatchCache,
        text: &[u8],
        start: usize,
    ) -> Option<Match> {
        if self.fast.pure_literal {
            return self.find_at_bytes(text, start);
        }
        let (begin, end) = match self.nfa_search(cache, text, start, false) {
            Some(Some(b)) => b,
            Some(None) => return None,
            None => return self.find_at_bytes(text, start),
        };
        let whole = Submatch {
            begin,
            end,
            matched: true,
        };
        if self.capture_count == 0 {
            return Some(Match {
                begin,
                end,
                captures: vec![whole],
            });
        }
        if let Some(fixed) = &self.fixed_captures {
            let mut captures: Vec<Submatch> = fixed
                .iter()
                .map(|&(b, e)| Submatch {
                    begin: begin + b,
                    end: begin + e,
                    matched: true,
                })
                .collect();
            captures[0] = whole;
            return Some(Match {
                begin,
                end,
                captures,
            });
        }
        match self.match_at_bytes(text, begin) {
            Some(m) if m.end == end => Some(m),
            _ => {
                let mut captures = vec![
                    Submatch {
                        begin: 0,
                        end: 0,
                        matched: false
                    };
                    self.capture_count + 1
                ];
                captures[0] = whole;
                Some(Match {
                    begin,
                    end,
                    captures,
                })
            }
        }
    }

    /// Force the pure-interpreter scan for this regex: every compile-time
    /// pre-filter is dropped and all entry points run the exact legacy
    /// loop. A/B differential testing only (`#[doc(hidden)]`, not part of
    /// the stable API); the differential suite requires the two modes to
    /// agree byte-for-byte.
    #[doc(hidden)]
    pub fn disable_fast_path(&mut self) {
        self.fast = FastPath::disabled();
    }
}

/// Fixed byte width of `node` starting at offset `at`, recording capture
/// spans; `None` when any piece is variable-width (`.` is a codepoint),
/// a group sits under an alternation / repetition, or a backreference
/// appears.
fn fixed_width(node: &ast::Node, at: usize, caps: &mut [(usize, usize)]) -> Option<usize> {
    use ast::Node::*;
    match node {
        Empty | AnchorStart | AnchorEnd | WordStart | WordEnd | WordBoundary | NotWordBoundary
        | LineStart(_) | LineEnd(_) | BufferStart | BufferEnd => Some(0),
        Literal(_) | CharClass { .. } => Some(1),
        Sequence(children) => {
            let mut w = 0;
            for c in children {
                w += fixed_width(c, at + w, caps)?;
            }
            Some(w)
        }
        Capture { index, child } => {
            let w = fixed_width(child, at, caps)?;
            caps[*index] = (at, at + w);
            Some(w)
        }
        Repeat { min, max, child } if Some(*min) == *max => {
            if max_back_reference(child).is_some() || has_capture(child) {
                return None;
            }
            let w = fixed_width(child, at, caps)?;
            Some(w * (*min as usize))
        }
        _ => None,
    }
}

fn has_capture(node: &ast::Node) -> bool {
    use ast::Node::*;
    match node {
        Capture { .. } => true,
        Repeat { child, .. } => has_capture(child),
        Sequence(c) | Alternation(c) => c.iter().any(has_capture),
        _ => false,
    }
}

/// Deepest backreference index in the tree.
fn max_back_reference(node: &ast::Node) -> Option<u32> {
    use ast::Node::*;
    match node {
        BackReference(n) => Some(*n),
        Capture { child, .. } => max_back_reference(child),
        Repeat { child, .. } => max_back_reference(child),
        Sequence(children) | Alternation(children) => {
            children.iter().filter_map(max_back_reference).max()
        }
        _ => None,
    }
}
