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
pub mod dialect;
pub(crate) mod fastpath;
pub mod matcher;
pub mod parser;

use std::fmt;

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
        let p = parser::Parser::new(syntax, pattern, ignore_case);
        let (root, capture_count) = p.parse()?;
        let fast = if cfg!(feature = "fast-path") {
            FastPath::build(&root, capture_count, ignore_case)
        } else {
            FastPath::disabled()
        };
        let regex = Regex {
            root,
            capture_count,
            ignore_case,
            fast,
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
