//! Pattern parser: BRE + ERE -> [`Node`] tree.
//!
//! Ported from `src/utils/regex.cppm` `detail::Parser`, with the GNU grep
//! 3.11 (glibc regcomp, LC_ALL=C) acceptance set re-probed pattern-by-pattern
//! and the divergences found during this port fixed to GNU behavior:
//!
//! * a trailing backslash is an error (GNU "Trailing backslash"); the C++
//!   engine accepted it as a literal `\`.
//! * backslash is an ORDINARY member inside `[...]` — GNU `[a\]]` is the
//!   class `{a,\}` followed by a literal `]` (`grep -o '[a-z\]]'` matches
//!   the two bytes `\]`). The C++ engine treated `\X` as an escape there.
//! * unknown `[:name:]` class names are a compile error (GNU "Invalid
//!   character class name"); the C++ engine accepted them as never-matching.
//! * ERE `{,n}` / `{,}` and BRE `\{,n\}` / `\{,\}` are valid (missing min
//!   is 0); the C++ engine rejected every missing-min form.
//! * ERE a malformed `{...}` (no digits at all / unclosed) falls back to
//!   LITERAL braces (`x{2,3` matches `x{2,3`); reversed `{m,n}` bounds after
//!   an atom are an error, at an expression start they are literal; counts
//!   above glibc `RE_DUP_MAX` (32767) error in both syntaxes. The C++
//!   engine errored on every malformed ERE brace.
//! * ERE `)` at top level is a literal (glibc accepts a stray `)`); the C++
//!   engine errored. BRE `\)` at top level stays an error (GNU errors).
//! * BRE `^` is an anchor only at a branch start (pattern start, after
//!   `\(`, after `\|`) and `$` only at a branch end (pattern end, before
//!   `\)` / `\|`) — POSIX context-dependent anchors, GNU-verified (`a^b`
//!   matches `a^b` under BRE, never under ERE). ERE anchors are
//!   context-independent. The C++ engine anchored everywhere in BRE.
//! * BRE `*` is a literal at a branch start or directly after a
//!   branch-initial `^` (`^*a` finds nothing; GNU agrees). The C++ engine
//!   always quantified.
//! * ERE `* + ? {m,n}` at a branch start repeat the EMPTY expression
//!   (glibc warns "* / {...} at start of expression" and matches empty);
//!   the C++ engine made them literals.
//! * backreferences are single-digit `\1`..`\9`; `\0` is a literal `0` and
//!   `\10` is `\1` followed by a literal `0` (GNU probes).

use crate::ast::{ClassAtom, Max, Node, PosixClass, RE_DUP_MAX};
use crate::{CompileOptions, Error, ErrorKind, Syntax};

/// Maximum group nesting / AST depth. glibc has its own internal limits;
/// this guards the parser and the matcher recursion on hostile patterns.
const MAX_DEPTH: usize = 200;

pub struct Parser<'a> {
    syntax: Syntax,
    pattern: &'a [u8],
    ignore_case: bool,
    /// Dialect options beyond the grep defaults (see [`CompileOptions`]).
    opts: CompileOptions,
    pos: usize,
    capture_count: usize,
    depth: usize,
}

/// Position context for the BRE context-dependent literals.
#[derive(Clone, Copy)]
struct PosCtx {
    /// First atom position of the current alternation branch.
    first: bool,
    /// The branch began with a `^` anchor (BRE: a following `*` is literal).
    after_caret: bool,
    /// [`CompileOptions::posix_strict`] only: the previous piece was an
    /// anchor. glibc's parse_expression returns right after an ANCHOR
    /// token, so the next token starts a fresh expression ("^*" is
    /// `<ANCHOR><CHAR *>` in BRE and REG_BADRPT in ERE).
    after_anchor: bool,
}

const BRANCH_START: PosCtx = PosCtx {
    first: true,
    after_caret: false,
    after_anchor: false,
};

impl<'a> Parser<'a> {
    pub fn new(syntax: Syntax, pattern: &'a [u8], ignore_case: bool) -> Parser<'a> {
        Parser::with_options(
            syntax,
            pattern,
            CompileOptions {
                ignore_case,
                ..CompileOptions::default()
            },
        )
    }

    /// Parser for an explicit dialect (sed's glibc syntax bits, `M`
    /// multiline mode, buffer anchors; see [`CompileOptions`]).
    pub fn with_options(syntax: Syntax, pattern: &'a [u8], opts: CompileOptions) -> Parser<'a> {
        Parser {
            syntax,
            pattern,
            ignore_case: opts.ignore_case,
            opts,
            pos: 0,
            capture_count: 0,
            depth: 0,
        }
    }

    pub fn capture_count(&self) -> usize {
        self.capture_count
    }

    pub fn parse(mut self) -> Result<(Node, usize), Error> {
        let root = self.parse_expression(BRANCH_START)?;
        if self.pos != self.pattern.len() {
            // Depth-aware group-end handling lets only a stray BRE `\)`
            // stop the expression early: GNU "Unmatched ) or \(".
            return Err(Error::new(
                ErrorKind::UnmatchedClose,
                self.pos,
                "unmatched ) or \\)",
            ));
        }
        Ok((root, self.capture_count))
    }

    // --- token helpers ---------------------------------------------------

    fn peek(&self, offset: usize) -> Option<u8> {
        self.pattern.get(self.pos + offset).copied()
    }

    fn at(&self, offset: usize, want: u8) -> bool {
        self.peek(offset) == Some(want)
    }

    fn at_backslash(&self, offset: usize, want: u8) -> bool {
        self.peek(offset) == Some(b'\\') && self.peek(offset + 1) == Some(want)
    }

    fn bump(&mut self) -> Option<u8> {
        let b = self.peek(0);
        if b.is_some() {
            self.pos += 1;
        }
        b
    }

    fn eat(&mut self, want: u8) -> bool {
        if self.at(0, want) {
            self.pos += 1;
            true
        } else {
            false
        }
    }

    fn is_alt_at_current(&self) -> bool {
        match self.syntax {
            Syntax::Extended => self.at(0, b'|'),
            Syntax::Basic => !self.opts.no_gnu_ops && self.at_backslash(0, b'|'),
        }
    }

    fn is_group_end_at_current(&self) -> bool {
        match self.syntax {
            Syntax::Extended => self.at(0, b')'),
            Syntax::Basic => self.at_backslash(0, b')'),
        }
    }

    fn consume_alt(&mut self) {
        self.pos += if self.syntax == Syntax::Basic { 2 } else { 1 };
    }

    fn consume_group_end(&mut self) {
        self.pos += if self.syntax == Syntax::Basic { 2 } else { 1 };
    }

    // --- grammar ----------------------------------------------------------

    fn parse_expression(&mut self, pos: PosCtx) -> Result<Node, Error> {
        let mut branches = vec![self.parse_sequence(pos)?];
        while self.is_alt_at_current() {
            self.consume_alt();
            branches.push(self.parse_sequence(BRANCH_START)?);
        }
        Ok(collapse_alternation(branches))
    }

    fn parse_sequence(&mut self, pos: PosCtx) -> Result<Node, Error> {
        let mut pieces: Vec<Node> = Vec::new();
        let mut ctx = pos;
        while self.pos < self.pattern.len() {
            if self.is_alt_at_current() || (self.depth > 0 && self.is_group_end_at_current()) {
                break;
            }
            let piece = self.parse_piece(ctx)?;
            // BRE: a `*` right after a branch-initial `^` anchor is the
            // next piece's literal atom (POSIX "after an initial ^").
            ctx = PosCtx {
                first: false,
                after_caret: ctx.first && matches!(piece, Node::AnchorStart | Node::LineStart(_)),
                after_anchor: self.opts.posix_strict && is_anchor_node(&piece),
            };
            pieces.push(piece);
        }
        Ok(collapse_sequence(pieces))
    }

    fn parse_piece(&mut self, pos: PosCtx) -> Result<Node, Error> {
        if self.opts.posix_strict {
            return self.parse_piece_strict(pos);
        }
        let mut atom =
            if self.syntax == Syntax::Extended && pos.first && self.starts_ere_empty_repeat()? {
                // ERE: a quantifier or a well-formed interval at a branch start
                // repeats the EMPTY expression (glibc "* / {...} at start of
                // expression": warns and matches empty). The quantifier loop
                // below consumes the operator and applies it to Empty.
                Node::Empty
            } else {
                self.parse_atom(pos)?
            };
        let first = pos.first;
        loop {
            match self.syntax {
                Syntax::Extended => {
                    if self.eat(b'*') {
                        atom = self.repeat(atom, 0, None);
                        continue;
                    }
                    if self.eat(b'+') {
                        atom = self.repeat(atom, 1, None);
                        continue;
                    }
                    if self.eat(b'?') {
                        atom = self.repeat(atom, 0, Some(1));
                        continue;
                    }
                    if self.at(0, b'{') {
                        match self.scan_ere_interval(true)? {
                            IntervalScan::Interval { min, max, len } => {
                                self.pos += len;
                                atom = self.repeat(atom, min, max);
                                continue;
                            }
                            IntervalScan::Literal => {}
                        }
                    }
                }
                Syntax::Basic => {
                    if self.at(0, b'*') {
                        // POSIX BRE: `*` directly after a branch-initial `^`
                        // is the NEXT piece's literal atom, not a closure.
                        if first && matches!(atom, Node::AnchorStart) {
                            break;
                        }
                        self.pos += 1;
                        atom = self.repeat(atom, 0, None);
                        continue;
                    }
                    if self.at_backslash(0, b'+') {
                        self.pos += 2;
                        atom = self.repeat(atom, 1, None);
                        continue;
                    }
                    if self.at_backslash(0, b'?') {
                        self.pos += 2;
                        atom = self.repeat(atom, 0, Some(1));
                        continue;
                    }
                    if self.at_backslash(0, b'{') {
                        let (min, max, len) = self.parse_bre_interval()?;
                        self.pos += len;
                        atom = self.repeat(atom, min, max);
                        continue;
                    }
                }
            }
            break;
        }
        Ok(atom)
    }

    /// [`CompileOptions::posix_strict`] piece: glibc parse_expression +
    /// parse_dup_op under GNU sed's syntax bits (RE_SYNTAX_POSIX_BASIC /
    /// RE_SYNTAX_POSIX_EXTENDED; probed against GNU sed 4.9):
    ///
    /// * ERE (RE_CONTEXT_INVALID_OPS): `* + ? {` at an expression start
    ///   (branch start, after `(` / `|`, after an anchor) is REG_BADRPT.
    /// * BRE (RE_CONTEXT_INVALID_DUP): `\{` at an expression start is
    ///   REG_BADRPT; `*` there is a literal, as are `\+` / `\?`; after one
    ///   repetition operator a following `*` or `\{` is REG_BADRPT
    ///   (`a**`).
    /// * anchors take no repetition operator: the next token starts a
    ///   fresh expression.
    /// * intervals follow glibc fetch_number exactly (no
    ///   RE_INVALID_INTERVAL_ORD): malformed specs are errors in both
    ///   syntaxes, never literal braces.
    fn parse_piece_strict(&mut self, pos: PosCtx) -> Result<Node, Error> {
        let expr_start = pos.first || pos.after_anchor;
        if expr_start {
            let badrpt = match self.syntax {
                Syntax::Extended => matches!(self.peek(0), Some(b'*' | b'+' | b'?' | b'{')),
                Syntax::Basic => self.at_backslash(0, b'{'),
            };
            if badrpt {
                return Err(bad_repeat(self.pos));
            }
        }
        let atom_pos = PosCtx {
            first: expr_start,
            ..pos
        };
        let mut atom = self.parse_atom(atom_pos)?;
        if is_anchor_node(&atom) {
            return Ok(atom);
        }
        loop {
            let (min, max) = match self.syntax {
                Syntax::Extended => {
                    if self.eat(b'*') {
                        (0, None)
                    } else if self.eat(b'+') {
                        (1, None)
                    } else if self.eat(b'?') {
                        (0, Some(1))
                    } else if self.at(0, b'{') {
                        let (min, max, len) = self.glibc_interval(1)?;
                        self.pos += len;
                        (min, max)
                    } else {
                        break;
                    }
                }
                Syntax::Basic => {
                    if self.eat(b'*') {
                        (0, None)
                    } else if !self.opts.no_gnu_ops && self.at_backslash(0, b'+') {
                        self.pos += 2;
                        (1, None)
                    } else if !self.opts.no_gnu_ops && self.at_backslash(0, b'?') {
                        self.pos += 2;
                        (0, Some(1))
                    } else if self.at_backslash(0, b'{') {
                        let (min, max, len) = self.glibc_interval(2)?;
                        self.pos += len;
                        (min, max)
                    } else {
                        break;
                    }
                }
            };
            atom = self.repeat(atom, min, max);
            if self.syntax == Syntax::Basic && (self.at(0, b'*') || self.at_backslash(0, b'{')) {
                return Err(bad_repeat(self.pos));
            }
        }
        Ok(atom)
    }

    /// glibc parse_dup_op interval scan (cursor at the opener, `open_len`
    /// = 1 for ERE `{`, 2 for BRE `\{`). fetch_number semantics: no digits
    /// = -1, a non-digit or the pattern end = -2; `{,m}` is `{0,m}`;
    /// `-2` at the pattern end is "Unmatched \{", elsewhere "Invalid
    /// content of \{\}"; reversed bounds or a missing closer are BADBR;
    /// counts above RE_DUP_MAX are "Regular expression too big".
    fn glibc_interval(&self, open_len: usize) -> Result<(u32, Max, usize), Error> {
        let bre = open_len == 2;
        let len = self.pattern.len();
        let mut cur = self.pos + open_len;
        // One fetch_number call. Returns (value, stop) where stop is
        // Close / Comma / End.
        #[derive(PartialEq)]
        enum Stop {
            Close,
            Comma,
            End,
        }
        let fetch = |cur: &mut usize| -> (i64, Stop) {
            let mut num: i64 = -1;
            loop {
                if *cur >= len {
                    return (-2, Stop::End);
                }
                let c = self.pattern[*cur];
                if bre {
                    if c == b'\\' {
                        if *cur + 1 >= len {
                            // trailing backslash inside the interval
                            *cur += 1;
                            return (-2, Stop::End);
                        }
                        let n = self.pattern[*cur + 1];
                        *cur += 2;
                        if n == b'}' {
                            return (num, Stop::Close);
                        }
                        num = -2;
                        continue;
                    }
                } else if c == b'}' {
                    *cur += 1;
                    return (num, Stop::Close);
                }
                *cur += 1;
                if c == b',' {
                    return (num, Stop::Comma);
                }
                num = if !c.is_ascii_digit() || num == -2 {
                    -2
                } else if num == -1 {
                    i64::from(c - b'0')
                } else {
                    (num * 10 + i64::from(c - b'0')).min(i64::from(RE_DUP_MAX) + 1)
                };
            }
        };
        let (mut start, stop1) = fetch(&mut cur);
        let end: i64;
        let last_stop;
        if start == -1 {
            if stop1 == Stop::Comma {
                start = 0;
            } else {
                // `{}`
                return Err(Error::new(
                    ErrorKind::InvalidInterval,
                    cur,
                    "invalid content of \\{\\}",
                ));
            }
        }
        if start != -2 {
            match stop1 {
                Stop::Close => {
                    end = start;
                    last_stop = Stop::Close;
                }
                Stop::Comma => {
                    let (e, st) = fetch(&mut cur);
                    end = e;
                    last_stop = st;
                }
                Stop::End => {
                    end = -2;
                    last_stop = Stop::End;
                }
            }
        } else {
            end = 0;
            last_stop = stop1;
        }
        if start == -2 || end == -2 {
            if last_stop == Stop::End {
                return Err(Error::new(ErrorKind::InvalidInterval, cur, "unmatched \\{"));
            }
            return Err(Error::new(
                ErrorKind::InvalidInterval,
                cur,
                "invalid content of \\{\\}",
            ));
        }
        if (end != -1 && start > end) || last_stop != Stop::Close {
            return Err(Error::new(
                ErrorKind::InvalidInterval,
                cur,
                "invalid content of \\{\\}",
            ));
        }
        let top = if end == -1 { start } else { end };
        if top > i64::from(RE_DUP_MAX) {
            return Err(Error::new(
                ErrorKind::IntervalTooBig,
                cur,
                "regular expression too big",
            ));
        }
        let max = if end == -1 { None } else { Some(end as u32) };
        Ok((start as u32, max, cur - self.pos))
    }

    /// ERE only: does a quantifier / interval start here, at a branch start?
    /// A malformed or reversed `{...}` stays literal and returns false.
    fn starts_ere_empty_repeat(&mut self) -> Result<bool, Error> {
        match self.peek(0) {
            Some(b'*') | Some(b'+') | Some(b'?') => Ok(true),
            Some(b'{') => match self.scan_ere_interval(false)? {
                IntervalScan::Interval { min, max, len } => {
                    // Rewind; the quantifier loop below consumes it.
                    let _ = (min, max, len);
                    Ok(true)
                }
                IntervalScan::Literal => Ok(false),
            },
            _ => Ok(false),
        }
    }

    fn repeat(&mut self, child: Node, min: u32, max: Max) -> Node {
        Node::Repeat {
            min,
            max,
            child: Box::new(child),
        }
    }

    fn parse_atom(&mut self, pos: PosCtx) -> Result<Node, Error> {
        let ch = match self.bump() {
            Some(ch) => ch,
            None => return Ok(Node::Empty),
        };
        match ch {
            b'^' => {
                if self.syntax == Syntax::Extended || pos.first {
                    Ok(if self.opts.multiline {
                        Node::LineStart(self.opts.line_delimiter)
                    } else {
                        Node::AnchorStart
                    })
                } else {
                    Ok(Node::Literal(b'^'))
                }
            }
            b'$' => {
                if self.syntax == Syntax::Extended || self.is_bre_dollar_end() {
                    Ok(if self.opts.multiline {
                        Node::LineEnd(self.opts.line_delimiter)
                    } else {
                        Node::AnchorEnd
                    })
                } else {
                    Ok(Node::Literal(b'$'))
                }
            }
            b'.' => Ok(if self.opts.multiline || self.opts.strict_utf8_dot {
                Node::DotRestricted {
                    exclude_newline: self.opts.multiline,
                    reject_invalid_utf8: self.opts.strict_utf8_dot,
                }
            } else {
                Node::Dot
            }),
            b'[' => self.parse_char_class(),
            b'*' if self.syntax == Syntax::Basic && (pos.first || pos.after_caret) => {
                // POSIX BRE literal `*`: first character of the expression /
                // subexpression, or directly after an initial `^`.
                Ok(Node::Literal(b'*'))
            }
            b'(' if self.syntax == Syntax::Extended => self.parse_group(),
            b')' if self.syntax == Syntax::Extended && self.depth == 0 => {
                if self.opts.posix_strict && !self.opts.unmatched_right_paren_ord {
                    // GNU sed clears RE_UNMATCHED_RIGHT_PAREN_ORD: a stray
                    // `)` is "Unmatched ) or \)" (probed sed 4.9).
                    return Err(Error::new(
                        ErrorKind::UnmatchedClose,
                        self.pos,
                        "unmatched ) or \\)",
                    ));
                }
                // glibc accepts a stray top-level `)` as a literal
                // (`grep -E ')'` matches `)`); the C++ engine errored.
                Ok(Node::Literal(b')'))
            }
            b'\\' => self.parse_escape_atom(),
            _ => Ok(Node::Literal(ch)),
        }
    }

    /// BRE `$` is an anchor at a branch end: pattern end or right before
    /// `\)` / `\|` (POSIX context-dependent anchors, GNU-verified).
    fn is_bre_dollar_end(&self) -> bool {
        self.pos == self.pattern.len() || self.at_backslash(0, b')') || self.at_backslash(0, b'|')
    }

    fn parse_group(&mut self) -> Result<Node, Error> {
        let index = self.open_capture()?;
        let child = self.parse_expression(BRANCH_START);
        self.depth -= 1;
        let child = child?;
        if !self.eat(b')') {
            return Err(Error::new(
                ErrorKind::UnmatchedOpen,
                self.pos,
                "unmatched ( or \\(",
            ));
        }
        Ok(Node::Capture {
            index,
            child: Box::new(child),
        })
    }

    fn open_capture(&mut self) -> Result<usize, Error> {
        if self.depth >= MAX_DEPTH {
            return Err(Error::new(
                ErrorKind::TooComplex,
                self.pos,
                "regular expression too complex",
            ));
        }
        self.capture_count += 1; // numbered at the OPENING delimiter
        self.depth += 1;
        Ok(self.capture_count)
    }

    fn parse_escape_atom(&mut self) -> Result<Node, Error> {
        let escaped = match self.bump() {
            Some(b) => b,
            None => {
                return Err(Error::new(
                    ErrorKind::TrailingBackslash,
                    self.pos,
                    "trailing backslash",
                ))
            }
        };
        if self.opts.no_gnu_ops
            && matches!(
                escaped,
                b'<' | b'>' | b'b' | b'B' | b'`' | b'\'' | b's' | b'S' | b'w' | b'W'
            )
        {
            return Ok(Node::Literal(escaped));
        }
        if escaped == b')'
            && self.syntax == Syntax::Basic
            && self.depth == 0
            && self.opts.unmatched_right_paren_ord
        {
            return Ok(Node::Literal(b')'));
        }
        match escaped {
            b'<' => Ok(Node::WordStart),
            b'>' => Ok(Node::WordEnd),
            b'b' => Ok(Node::WordBoundary),
            b'B' => Ok(Node::NotWordBoundary),
            b'`' if self.opts.buffer_anchors => Ok(Node::BufferStart),
            b'\'' if self.opts.buffer_anchors => Ok(Node::BufferEnd),
            b'1'..=b'9' => Ok(Node::BackReference(u32::from(escaped - b'0'))),
            // `\0` is a literal zero digit in GNU (single-digit backrefs
            // only, so `\10` parses as `\1` + literal `0`).
            b's' => Ok(shorthand_class(PosixClass::Space, false)),
            b'S' => Ok(shorthand_class(PosixClass::Space, true)),
            b'w' => Ok(shorthand_class(PosixClass::Word, false)),
            b'W' => Ok(shorthand_class(PosixClass::Word, true)),
            b'(' if self.syntax == Syntax::Basic => {
                let index = self.open_capture()?;
                let child = self.parse_expression(BRANCH_START);
                self.depth -= 1;
                let child = child?;
                if !self.is_group_end_at_current() {
                    return Err(Error::new(
                        ErrorKind::UnmatchedOpen,
                        self.pos,
                        "unmatched ( or \\(",
                    ));
                }
                self.consume_group_end();
                Ok(Node::Capture {
                    index,
                    child: Box::new(child),
                })
            }
            b')' if self.syntax == Syntax::Basic && self.depth == 0 => Err(Error::new(
                ErrorKind::UnmatchedClose,
                self.pos,
                "unmatched ) or \\)",
            )),
            _ => Ok(Node::Literal(escaped)),
        }
    }

    // --- intervals ----------------------------------------------------------

    /// BRE `\{m[,n]\}` with the cursor before the `\{`. Any malformed form
    /// is an error (GNU: `a\{2` -> "Unmatched \{", `a\{a\}` / `a\{2,1\}` ->
    /// "Invalid content of \{\}", `a\{99999999999\}` -> too big); `\{,\}`
    /// and `\{,n\}` mean `{0,}` / `{0,n}` (GNU-verified). Returns
    /// `(min, max, bytes consumed)`; the cursor stays before the `\{`.
    fn parse_bre_interval(&mut self) -> Result<(u32, Max, usize), Error> {
        let start = self.pos;
        let mut cursor = start + 2; // skip `\{`
        let min = self.scan_interval_number(&mut cursor)?.unwrap_or(0);
        let max = if cursor < self.pattern.len() && self.pattern[cursor] == b',' {
            cursor += 1;
            self.scan_interval_number(&mut cursor)? // None = `\{m,\}` unbounded
        } else {
            Some(min) // `\{m\}` — exact count
        };
        if cursor + 1 >= self.pattern.len()
            || self.pattern[cursor] != b'\\'
            || self.pattern[cursor + 1] != b'}'
        {
            return Err(Error::new(
                ErrorKind::InvalidInterval,
                cursor,
                "unmatched \\{",
            ));
        }
        cursor += 2;
        check_interval_bounds(min, max, cursor)?;
        Ok((min, max, cursor - start))
    }

    /// ERE `{m[,n]}` scan with the cursor at the `{`. glibc rules, probed on
    /// GNU grep 3.11: a well-formed spec is an interval after an atom
    /// (reversed bounds -> error) and repeats the empty expression at a
    /// branch start (reversed bounds there -> literal braces); a spec with
    /// no digits at all or no closing `}` is literal braces in both spots;
    /// counts above RE_DUP_MAX error in both spots.
    fn scan_ere_interval(&mut self, after_atom: bool) -> Result<IntervalScan, Error> {
        let start = self.pos;
        let mut cursor = start + 1; // skip `{`
        let min = self.scan_interval_number(&mut cursor)?;
        let comma = cursor < self.pattern.len() && self.pattern[cursor] == b',';
        let max = if comma {
            cursor += 1;
            self.scan_interval_number(&mut cursor)?
        } else {
            min.map(|m| m) // `{m}` — exact count
        };
        if cursor >= self.pattern.len() || self.pattern[cursor] != b'}' {
            return Ok(IntervalScan::Literal); // `x{2,3` -> literal braces
        }
        if min.is_none() && !comma {
            // GNU: `x{}` is a compile error ("Invalid content of \{\}",
            // probed). At an EXPRESSION start glibc instead consumes `{}`
            // zero-width and matches the tail ("{}" over "x{}y" matched
            // "}"): one of the pathological leading-brace corners this
            // engine documents rather than emulates (see the matrix report)
            // — here we fall back to literal braces.
            if after_atom {
                return Err(Error::new(
                    ErrorKind::InvalidInterval,
                    cursor + 1,
                    "invalid content of \\{\\}",
                ));
            }
            return Ok(IntervalScan::Literal);
        }
        let min = min.unwrap_or(0);
        if let Some(n) = max {
            if n < min {
                if after_atom {
                    return Err(Error::new(
                        ErrorKind::InvalidInterval,
                        cursor + 1,
                        "invalid content of \\{\\}",
                    ));
                }
                return Ok(IntervalScan::Literal); // leading `{2,1}` -> literal
            }
        }
        Ok(IntervalScan::Interval {
            min,
            max,
            len: cursor + 1 - start,
        })
    }

    /// Scan a decimal repetition count from `cursor`; `Ok(None)` = no digits
    /// there (allowed as an empty min/max). Counts above RE_DUP_MAX are
    /// errors in both syntaxes (GNU: "Regular expression too big").
    fn scan_interval_number(&self, cursor: &mut usize) -> Result<Option<u32>, Error> {
        if *cursor >= self.pattern.len() || !self.pattern[*cursor].is_ascii_digit() {
            return Ok(None);
        }
        let mut value: u64 = 0;
        while *cursor < self.pattern.len() && self.pattern[*cursor].is_ascii_digit() {
            value = value * 10 + u64::from(self.pattern[*cursor] - b'0');
            if value > u64::from(RE_DUP_MAX) {
                return Err(Error::new(
                    ErrorKind::IntervalTooBig,
                    *cursor,
                    "regular expression too big",
                ));
            }
            *cursor += 1;
        }
        Ok(Some(value as u32))
    }

    // --- bracket expressions ----------------------------------------------

    fn parse_char_class(&mut self) -> Result<Node, Error> {
        let mut atoms: Vec<ClassAtom> = Vec::new();
        let negated = self.eat(b'^');
        // `]` is a literal only at the very first member position.
        let mut first_member = true;
        loop {
            let b = match self.peek(0) {
                Some(b) => b,
                None => {
                    return Err(Error::new(
                        ErrorKind::UnmatchedBracket,
                        self.pos,
                        "unmatched [ or [^",
                    ))
                }
            };
            if b == b']' && !first_member {
                self.pos += 1;
                if negated && self.opts.multiline {
                    // REG_NEWLINE: RE_HAT_LISTS_NOT_NEWLINE — a non-matching
                    // list never matches newline (GNU sed `M`, probed; the
                    // `\W` / `\S` shorthands are unaffected).
                    atoms.push(ClassAtom::Literal(b'\n'));
                }
                return Ok(Node::CharClass { negated, atoms });
            }

            // `[:name:]`, `[[.coll.]]`, `[[=equiv=]]`. A `[` that starts
            // none of them is an ordinary member (GNU `[[abc]]` = [,a,b,c).
            if b == b'[' {
                match self.peek(1) {
                    Some(b':') => {
                        atoms.push(ClassAtom::Posix(self.parse_posix_class()?));
                        first_member = false;
                        continue;
                    }
                    Some(b'.') | Some(b'=') => {
                        atoms.push(self.parse_collating()?);
                        first_member = false;
                        // glibc rejects a range whose start is a collating
                        // symbol (`[[.a.]-z]` -> "Unmatched [").
                        if self.at(0, b'-') && self.peek(1) != Some(b']') {
                            return Err(Error::new(
                                ErrorKind::InvalidRangeEnd,
                                self.pos,
                                "invalid range end",
                            ));
                        }
                        continue;
                    }
                    _ => {}
                }
            }

            // A `-` right after a completed range or a POSIX class (with an
            // endpoint following) is GNU "Invalid range end" — `[a-c-e]`,
            // `[a-c-e-]`, `[[:alpha:]-9]`, `[a-c[:digit:]-9]` all error.
            let prev_takes_range = match atoms.last() {
                None | Some(ClassAtom::Literal(_)) => true,
                Some(_) => false,
            };
            if b == b'-' && !prev_takes_range && self.peek(1).is_some_and(|b2| b2 != b']') {
                return Err(Error::new(
                    ErrorKind::InvalidRangeEnd,
                    self.pos,
                    "invalid range end",
                ));
            }

            // Backslash is an ORDINARY member byte in GNU brackets.
            let start = self.bump().expect("peeked above");

            // Range? A `-` right before the closing `]` stays literal, and
            // the first-position `]` never starts one (GNU `[]-a]` is
            // `]`, `-`, `a` — three members, not a range).
            let is_range = self.at(0, b'-')
                && self.peek(1).is_some_and(|b| b != b']')
                && prev_takes_range
                && !(first_member && start == b']');
            if is_range {
                self.pos += 1; // `-`
                let end = self.bump().expect("peek(1) existed");
                // GNU checks the RAW endpoints first: `[z-a]` and even
                // `grep -i '[Z-A]'` / `grep -i '[a-Z]'` all error
                // ("Invalid range end", probed). The C++ engine swapped
                // instead of erroring — a silent divergence fixed here.
                if start > end {
                    return Err(Error::new(
                        ErrorKind::InvalidRangeEnd,
                        self.pos,
                        "invalid range end",
                    ));
                }
                // -i folds the endpoints afterwards; the matcher folds the
                // input byte, so `[A-Z]` caseless covers both cases.
                let (lo, hi) = if self.ignore_case {
                    (start.to_ascii_lowercase(), end.to_ascii_lowercase())
                } else {
                    (start, end)
                };
                atoms.push(ClassAtom::Range(lo, hi));
            } else {
                atoms.push(ClassAtom::Literal(if self.ignore_case {
                    start.to_ascii_lowercase()
                } else {
                    start
                }));
            }
            first_member = false;
        }
    }

    /// `[:name:]` inside a bracket. Committing at `[:` is final: a missing
    /// `:]` is an unmatched-bracket error, an unknown name is GNU "Invalid
    /// character class name" (both GNU-verified).
    fn parse_posix_class(&mut self) -> Result<PosixClass, Error> {
        let name_start = self.pos + 2;
        let mut cursor = name_start;
        while cursor + 1 < self.pattern.len()
            && !(self.pattern[cursor] == b':' && self.pattern[cursor + 1] == b']')
        {
            cursor += 1;
        }
        if cursor + 1 >= self.pattern.len() {
            return Err(Error::new(
                ErrorKind::UnmatchedBracket,
                self.pos,
                "unmatched [ or [^",
            ));
        }
        let name = &self.pattern[name_start..cursor];
        let class = PosixClass::from_name(name).ok_or_else(|| {
            Error::new(
                ErrorKind::InvalidClassName,
                self.pos,
                "invalid character class name",
            )
        })?;
        self.pos = cursor + 2;
        Ok(class)
    }

    /// `[[.c.]]` / `[[=c=]]` inside a bracket. Only single-byte symbols have
    /// C-locale meaning; a multi-byte symbol is a GNU compile error
    /// ("Invalid collation character"), as is a missing terminator
    /// (`[[.]-z]` errors in GNU).
    fn parse_collating(&mut self) -> Result<ClassAtom, Error> {
        let terminator = self.peek(1).expect("caller peeked [x");
        let name_start = self.pos + 2;
        let mut cursor = name_start;
        while cursor + 1 < self.pattern.len()
            && !(self.pattern[cursor] == terminator && self.pattern[cursor + 1] == b']')
        {
            cursor += 1;
        }
        if cursor + 1 >= self.pattern.len() {
            return Err(Error::new(
                ErrorKind::UnmatchedBracket,
                self.pos,
                "unmatched [ or [^",
            ));
        }
        let name = &self.pattern[name_start..cursor];
        if name.len() != 1 {
            return Err(Error::new(
                ErrorKind::InvalidCollation,
                self.pos,
                "invalid collation character",
            ));
        }
        self.pos = cursor + 2;
        Ok(ClassAtom::Literal(name[0]))
    }
}

enum IntervalScan {
    Interval { min: u32, max: Max, len: usize },
    Literal,
}

fn check_interval_bounds(min: u32, max: Max, at: usize) -> Result<(), Error> {
    if let Some(n) = max {
        if n < min {
            return Err(Error::new(
                ErrorKind::InvalidInterval,
                at,
                "invalid content of \\{\\}",
            ));
        }
    }
    Ok(())
}

/// Zero-width assertion nodes that glibc lexes as ANCHOR tokens (no
/// repetition operator may follow them under the strict dialect).
fn is_anchor_node(node: &Node) -> bool {
    matches!(
        node,
        Node::AnchorStart
            | Node::AnchorEnd
            | Node::LineStart(_)
            | Node::LineEnd(_)
            | Node::BufferStart
            | Node::BufferEnd
            | Node::WordStart
            | Node::WordEnd
            | Node::WordBoundary
            | Node::NotWordBoundary
    )
}

fn bad_repeat(at: usize) -> Error {
    // Reuses the interval error kind (no new ErrorKind variant, so
    // exhaustive matches in other consumers stay valid); the message
    // carries the glibc REG_BADRPT text.
    Error::new(
        ErrorKind::InvalidInterval,
        at,
        "invalid preceding regular expression",
    )
}

fn shorthand_class(class: PosixClass, negated: bool) -> Node {
    Node::CharClass {
        negated,
        atoms: vec![ClassAtom::Posix(class)],
    }
}

fn collapse_sequence(mut pieces: Vec<Node>) -> Node {
    match pieces.len() {
        0 => Node::Empty,
        1 => pieces.pop().expect("len checked"),
        _ => Node::Sequence(pieces),
    }
}

fn collapse_alternation(mut branches: Vec<Node>) -> Node {
    match branches.len() {
        0 => Node::Empty,
        1 => branches.pop().expect("len checked"),
        _ => Node::Alternation(branches),
    }
}
