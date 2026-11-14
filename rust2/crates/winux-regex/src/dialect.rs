//! GNU regex *dialect* front-end: the `re_syntax_options` families that
//! glibc's `re_compile_pattern` accepts and that GNU findutils exposes as
//! `find -regextype` (lib/regextype.c), translated onto the two native
//! syntaxes of this engine ([`Syntax::Basic`] / [`Syntax::Extended`]).
//!
//! The native parser is the GNU grep BRE/ERE pair. The other GNU syntax
//! bit-sets differ from those two only in token classification, so each
//! dialect is a byte-level rewrite into the native syntax followed by the
//! ordinary compile. Every rule below was probed against GNU findutils
//! 4.9.0 (`find -regextype T -regex P`, glibc 2.39, WSL) on a fixture of
//! literal-metacharacter file names:
//!
//! * `emacs` (`RE_SYNTAX_EMACS` = 0, the GNU find DEFAULT) and
//!   `findutils-default` (`EMACS | RE_DOT_NEWLINE`): `\( \) \|` group and
//!   alternate, bare `+ ? *` are operators (literal at a branch start),
//!   `( ) | { }` are literal, there are NO intervals (`a\{2\}` matches the
//!   text `a{2}`), NO character classes (`[[:upper:]]` is a bracket of the
//!   bytes `[:uper` followed by `]`), backslash is literal inside brackets,
//!   `^`/`$` are anchors only at a branch start/end, GNU operators
//!   (`\w \W \s \S \b \B \< \> \` \'`) and back-references work.
//! * `posix-basic`, `ed`, `sed` (`RE_SYNTAX_POSIX_BASIC`): grep BRE, except
//!   a repetition operator directly after another one (`a**`) is "Invalid
//!   preceding regular expression".
//! * `grep`: grep BRE unchanged.
//! * `posix-minimal-basic` (`RE_LIMITED_OPS`): `\+ \? \|` are literal.
//! * `posix-extended` / `posix-awk`: an operator where an expression is
//!   expected (`*a`, `(*a)`, `a|*b`) is "Invalid preceding regular
//!   expression" (`RE_CONTEXT_INVALID_OPS`).
//! * `egrep` / `posix-egrep`: grep ERE unchanged.
//! * `awk`, `posix-awk`, `gnu-awk`: backslash escapes inside brackets
//!   (`RE_BACKSLASH_ESCAPE_IN_LISTS`: `[\]x]` is the set `{],x}`); `awk`
//!   and `gnu-awk` take a leading operator literally; `awk` has no
//!   intervals and no back-references (`\1` is the digit); `awk` and
//!   `posix-awk` have no GNU operators (`\w` is the letter `w`).
//!
//! The rewrite never changes capture numbering: groups map 1:1.

use crate::{Error, ErrorKind, Regex, Syntax};

/// A GNU regex syntax family (findutils `-regextype` name table order).
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Dialect {
    FindutilsDefault,
    Ed,
    Emacs,
    GnuAwk,
    Grep,
    PosixAwk,
    Awk,
    PosixBasic,
    PosixEgrep,
    Egrep,
    PosixExtended,
    PosixMinimalBasic,
    Sed,
}

/// `(name, dialect)` in findutils `lib/regextype.c` order — the order GNU
/// prints in its "valid types are ..." diagnostic.
pub const DIALECT_NAMES: [(&str, Dialect); 13] = [
    ("findutils-default", Dialect::FindutilsDefault),
    ("ed", Dialect::Ed),
    ("emacs", Dialect::Emacs),
    ("gnu-awk", Dialect::GnuAwk),
    ("grep", Dialect::Grep),
    ("posix-awk", Dialect::PosixAwk),
    ("awk", Dialect::Awk),
    ("posix-basic", Dialect::PosixBasic),
    ("posix-egrep", Dialect::PosixEgrep),
    ("egrep", Dialect::Egrep),
    ("posix-extended", Dialect::PosixExtended),
    ("posix-minimal-basic", Dialect::PosixMinimalBasic),
    ("sed", Dialect::Sed),
];

impl Dialect {
    /// Look up a findutils `-regextype` name (exact, case-sensitive).
    pub fn from_name(name: &str) -> Option<Dialect> {
        DIALECT_NAMES
            .iter()
            .find(|(n, _)| *n == name)
            .map(|(_, d)| *d)
    }

    fn is_emacs(self) -> bool {
        matches!(self, Dialect::Emacs | Dialect::FindutilsDefault)
    }

    fn is_basic(self) -> bool {
        matches!(
            self,
            Dialect::PosixBasic
                | Dialect::Ed
                | Dialect::Sed
                | Dialect::Grep
                | Dialect::PosixMinimalBasic
        )
    }

    fn is_awk(self) -> bool {
        matches!(self, Dialect::Awk | Dialect::PosixAwk | Dialect::GnuAwk)
    }

    /// GNU operators (`\w`, `\<`, `` \` ``, ...) available.
    fn gnu_ops(self) -> bool {
        !matches!(self, Dialect::Awk | Dialect::PosixAwk)
    }
}

/// Compile failure of a dialect pattern: either an engine error or the
/// glibc `REG_BADRPT` class the native grep syntaxes never raise.
#[derive(Debug, Clone, PartialEq, Eq)]
pub enum DialectError {
    /// The rewritten pattern failed in the engine (or the rewrite itself
    /// found an unbalanced group / bracket).
    Regex(Error),
    /// glibc "Invalid preceding regular expression".
    BadRepeat,
}

impl DialectError {
    /// The glibc `re_compile_pattern` message (C locale) for this failure,
    /// the wording GNU tools print after their own prefix.
    pub fn glibc_message(&self) -> &'static str {
        match self {
            DialectError::BadRepeat => "Invalid preceding regular expression",
            DialectError::Regex(e) => match e.kind() {
                ErrorKind::TrailingBackslash => "Trailing backslash",
                ErrorKind::UnmatchedOpen => "Unmatched ( or \\(",
                ErrorKind::UnmatchedClose => "Unmatched ) or \\)",
                ErrorKind::UnmatchedBracket => "Unmatched [, [^, [:, [., or [=",
                ErrorKind::InvalidInterval => "Invalid content of \\{\\}",
                ErrorKind::IntervalTooBig | ErrorKind::TooComplex => "Regular expression too big",
                ErrorKind::InvalidBackReference => "Invalid back reference",
                ErrorKind::InvalidClassName => "Invalid character class name",
                ErrorKind::InvalidCollation => "Invalid collation character",
                ErrorKind::InvalidRangeEnd => "Invalid range end",
                ErrorKind::WorkLimit => "Memory exhausted",
            },
        }
    }
}

impl From<Error> for DialectError {
    fn from(e: Error) -> Self {
        DialectError::Regex(e)
    }
}

/// Compile `pattern` under `dialect`; `ignore_case` is glibc `RE_ICASE`
/// (find `-iregex`).
pub fn compile(dialect: Dialect, pattern: &[u8], ignore_case: bool) -> Result<Regex, DialectError> {
    let (syntax, native) = translate(dialect, pattern)?;
    Ok(Regex::compile_bytes_ignore_case(
        syntax,
        &native,
        ignore_case,
    )?)
}

/// Rewrite `pattern` into the native syntax this dialect maps onto.
pub fn translate(dialect: Dialect, pattern: &[u8]) -> Result<(Syntax, Vec<u8>), DialectError> {
    if dialect.is_emacs() {
        Ok((Syntax::Extended, emacs_to_ere(pattern)?))
    } else if dialect.is_basic() {
        Ok((Syntax::Basic, basic_rewrite(dialect, pattern)?))
    } else {
        Ok((Syntax::Extended, extended_rewrite(dialect, pattern)?))
    }
}

fn err(kind: ErrorKind, offset: usize, message: &'static str) -> DialectError {
    DialectError::Regex(Error::new(kind, offset, message))
}

/// Bytes that are operators somewhere in the native ERE syntax and must be
/// escaped to stay literal.
fn ere_special(c: u8) -> bool {
    matches!(
        c,
        b'.' | b'['
            | b']'
            | b'('
            | b')'
            | b'*'
            | b'+'
            | b'?'
            | b'{'
            | b'}'
            | b'|'
            | b'^'
            | b'$'
            | b'\\'
    )
}

fn push_ere_literal(out: &mut Vec<u8>, c: u8) {
    if ere_special(c) {
        out.push(b'\\');
    }
    out.push(c);
}

/// End index (exclusive) of a native bracket expression starting at
/// `start` (`pattern[start] == b'['`), honoring `[:x:]`, `[.x.]`, `[=x=]`
/// and a leading `]`; `None` when unterminated.
fn native_bracket_end(p: &[u8], start: usize) -> Option<usize> {
    let mut i = start + 1;
    if i < p.len() && p[i] == b'^' {
        i += 1;
    }
    if i < p.len() && p[i] == b']' {
        i += 1;
    }
    while i < p.len() {
        match p[i] {
            b']' => return Some(i + 1),
            b'[' if i + 1 < p.len() && matches!(p[i + 1], b':' | b'.' | b'=') => {
                let close = p[i + 1];
                let mut j = i + 2;
                let mut found = None;
                while j + 1 < p.len() {
                    if p[j] == close && p[j + 1] == b']' {
                        found = Some(j + 2);
                        break;
                    }
                    j += 1;
                }
                match found {
                    Some(e) => i = e,
                    None => return None,
                }
            }
            _ => i += 1,
        }
    }
    None
}

/// One member of a re-emitted bracket set.
#[derive(Debug, Clone, PartialEq, Eq)]
enum Member {
    Byte(u8),
    Range(u8, u8),
    /// `[:class:]`, `[.c.]`, `[=c=]` copied verbatim.
    Raw(Vec<u8>),
}

/// Parse a bracket of a non-native dialect. `classes`: `[:name:]` is a
/// class (else `[` is a literal member); `escapes`: backslash makes the
/// next byte a literal member (`RE_BACKSLASH_ESCAPE_IN_LISTS`). Returns
/// (negated, members, index after the closing `]`).
fn parse_foreign_bracket(
    p: &[u8],
    start: usize,
    classes: bool,
    escapes: bool,
) -> Result<(bool, Vec<Member>, usize), DialectError> {
    let unmatched = || err(ErrorKind::UnmatchedBracket, start, "unmatched [");
    let mut i = start + 1;
    let mut negated = false;
    if i < p.len() && p[i] == b'^' {
        negated = true;
        i += 1;
    }
    let mut members: Vec<Member> = Vec::new();
    let mut first = true;
    loop {
        if i >= p.len() {
            return Err(unmatched());
        }
        let c = p[i];
        if c == b']' && !first {
            i += 1;
            break;
        }
        first = false;
        // collating / equivalence / class openers
        if c == b'[' && i + 1 < p.len() {
            let k = p[i + 1];
            if k == b'.' || k == b'=' || (k == b':' && classes) {
                let mut j = i + 2;
                let mut end = None;
                while j + 1 < p.len() {
                    if p[j] == k && p[j + 1] == b']' {
                        end = Some(j + 2);
                        break;
                    }
                    j += 1;
                }
                let e = end.ok_or_else(unmatched)?;
                members.push(Member::Raw(p[i..e].to_vec()));
                i = e;
                continue;
            }
        }
        let (lo, next) = if escapes && c == b'\\' {
            if i + 1 >= p.len() {
                return Err(unmatched());
            }
            (p[i + 1], i + 2)
        } else {
            (c, i + 1)
        };
        // range?
        if next + 1 < p.len() && p[next] == b'-' && p[next + 1] != b']' {
            let (hi, after) = if escapes && p[next + 1] == b'\\' {
                if next + 2 >= p.len() {
                    return Err(unmatched());
                }
                (p[next + 2], next + 3)
            } else {
                (p[next + 1], next + 2)
            };
            if hi < lo {
                return Err(err(ErrorKind::InvalidRangeEnd, i, "invalid range end"));
            }
            members.push(Member::Range(lo, hi));
            i = after;
            continue;
        }
        members.push(Member::Byte(lo));
        i = next;
    }
    Ok((negated, members, i))
}

/// Re-emit a parsed member set as a native (grep) bracket meaning the same
/// byte set: `]` first, literal `[` / `^` / `-` placed where the native
/// parser cannot read them as `[:`-openers, negation, or range dashes.
fn emit_bracket(out: &mut Vec<u8>, negated: bool, members: &[Member]) {
    let has = |b: u8| members.iter().any(|m| *m == Member::Byte(b));
    let others: Vec<&Member> = members
        .iter()
        .filter(|m| !matches!(m, Member::Byte(b']' | b'[' | b'^' | b'-')))
        .collect();
    let close = has(b']');
    let (open, caret, dash) = (has(b'['), has(b'^'), has(b'-'));
    if !negated && !close && others.is_empty() && caret && !open && !dash {
        // the set {^} alone: a bracket would read as a negation
        out.extend_from_slice(b"\\^");
        return;
    }
    out.push(b'[');
    if negated {
        out.push(b'^');
    }
    if close {
        out.push(b']');
    }
    for m in &others {
        match m {
            Member::Byte(b) => out.push(*b),
            Member::Range(lo, hi) => {
                out.push(*lo);
                out.push(b'-');
                if *hi == b']' {
                    out.extend_from_slice(b"[.].]");
                } else {
                    out.push(*hi);
                }
            }
            Member::Raw(r) => out.extend_from_slice(r),
        }
    }
    let caret_first = !negated && !close && others.is_empty();
    if caret_first && caret {
        // `^` must not open the set: lead with `[` (or `-`) instead
        if open {
            out.push(b'[');
            out.push(b'^');
        } else {
            out.push(b'-');
            out.push(b'^');
            out.push(b']');
            return;
        }
    } else {
        if caret {
            out.push(b'^');
        }
        if open {
            out.push(b'[');
        }
    }
    if dash {
        out.push(b'-');
    }
    out.push(b']');
}

/// emacs / findutils-default -> native ERE.
fn emacs_to_ere(p: &[u8]) -> Result<Vec<u8>, DialectError> {
    let mut out = Vec::with_capacity(p.len() + 8);
    let mut i = 0;
    let mut depth = 0usize;
    // at a branch start (pattern start, after `\(` / `\|`, or after a
    // branch-initial `^`): `* + ?` are literal, `^` is an anchor
    let mut start = true;
    while i < p.len() {
        let c = p[i];
        match c {
            b'\\' => {
                if i + 1 >= p.len() {
                    return Err(err(ErrorKind::TrailingBackslash, i, "trailing backslash"));
                }
                let k = p[i + 1];
                i += 2;
                match k {
                    b'(' => {
                        depth += 1;
                        out.push(b'(');
                        start = true;
                        continue;
                    }
                    b')' => {
                        if depth == 0 {
                            return Err(err(
                                ErrorKind::UnmatchedClose,
                                i - 2,
                                "unmatched ) or \\)",
                            ));
                        }
                        depth -= 1;
                        out.push(b')');
                    }
                    b'|' => {
                        out.push(b'|');
                        start = true;
                        continue;
                    }
                    b'1'..=b'9' | b'w' | b'W' | b's' | b'S' | b'b' | b'B' | b'<' | b'>' => {
                        out.push(b'\\');
                        out.push(k);
                    }
                    b'`' => out.push(b'^'),
                    b'\'' => out.push(b'$'),
                    _ => push_ere_literal(&mut out, k),
                }
                start = false;
            }
            b'(' | b')' | b'|' | b'{' | b'}' => {
                out.push(b'\\');
                out.push(c);
                i += 1;
                start = false;
            }
            b'*' | b'+' | b'?' => {
                if start {
                    out.push(b'\\');
                }
                out.push(c);
                i += 1;
                start = false;
            }
            b'^' => {
                i += 1;
                if start {
                    out.push(b'^');
                    // `^*` keeps the star literal
                    continue;
                }
                out.extend_from_slice(b"\\^");
            }
            b'$' => {
                i += 1;
                let at_end = i == p.len()
                    || (p[i] == b'\\' && i + 1 < p.len() && matches!(p[i + 1], b')' | b'|'));
                if at_end {
                    out.push(b'$');
                } else {
                    out.extend_from_slice(b"\\$");
                }
                start = false;
            }
            b'[' => {
                let (neg, members, next) = parse_foreign_bracket(p, i, false, false)?;
                emit_bracket(&mut out, neg, &members);
                i = next;
                start = false;
            }
            _ => {
                out.push(c);
                i += 1;
                start = false;
            }
        }
    }
    if depth > 0 {
        return Err(err(ErrorKind::UnmatchedOpen, p.len(), "unmatched ( or \\("));
    }
    Ok(out)
}

/// BRE-family dialects -> native (grep) BRE.
fn basic_rewrite(d: Dialect, p: &[u8]) -> Result<Vec<u8>, DialectError> {
    let strict_dup = matches!(d, Dialect::PosixBasic | Dialect::Ed | Dialect::Sed);
    let limited = d == Dialect::PosixMinimalBasic;
    let mut out = Vec::with_capacity(p.len() + 4);
    let mut i = 0;
    let mut start = true;
    let mut last_dup = false;
    while i < p.len() {
        let c = p[i];
        match c {
            b'\\' => {
                if i + 1 >= p.len() {
                    // let the native parser report it
                    out.push(b'\\');
                    i += 1;
                    continue;
                }
                let k = p[i + 1];
                match k {
                    b'+' | b'?' | b'|' if limited => {
                        out.push(k);
                        i += 2;
                        start = false;
                        last_dup = false;
                    }
                    b'(' | b'|' => {
                        out.extend_from_slice(&p[i..i + 2]);
                        i += 2;
                        start = true;
                        last_dup = false;
                    }
                    b'{' => {
                        if strict_dup && last_dup && !start {
                            return Err(DialectError::BadRepeat);
                        }
                        // copy the interval through its `\}`
                        let mut j = i + 2;
                        while j + 1 < p.len() && !(p[j] == b'\\' && p[j + 1] == b'}') {
                            j += 1;
                        }
                        let end = (j + 2).min(p.len());
                        out.extend_from_slice(&p[i..end]);
                        i = end;
                        last_dup = !start;
                        start = false;
                    }
                    b'+' | b'?' => {
                        if strict_dup && last_dup && !start {
                            return Err(DialectError::BadRepeat);
                        }
                        out.extend_from_slice(&p[i..i + 2]);
                        i += 2;
                        last_dup = !start;
                        start = false;
                    }
                    b'`' if start => {
                        out.push(b'^');
                        i += 2;
                    }
                    b'\'' => {
                        let next = i + 2;
                        let at_end = next == p.len()
                            || (p[next] == b'\\'
                                && next + 1 < p.len()
                                && matches!(p[next + 1], b')' | b'|'));
                        if at_end {
                            out.push(b'$');
                        } else {
                            out.extend_from_slice(&p[i..i + 2]);
                        }
                        i += 2;
                        start = false;
                        last_dup = false;
                    }
                    _ => {
                        out.extend_from_slice(&p[i..i + 2]);
                        i += 2;
                        start = false;
                        last_dup = false;
                    }
                }
            }
            b'*' => {
                if start {
                    // literal star at a branch start
                    out.push(b'*');
                    start = false;
                    last_dup = false;
                } else {
                    if strict_dup && last_dup {
                        return Err(DialectError::BadRepeat);
                    }
                    out.push(b'*');
                    last_dup = true;
                }
                i += 1;
            }
            b'^' if start => {
                out.push(b'^');
                i += 1;
            }
            b'[' => {
                let end = native_bracket_end(p, i).unwrap_or(p.len());
                out.extend_from_slice(&p[i..end]);
                i = end;
                start = false;
                last_dup = false;
            }
            _ => {
                out.push(c);
                i += 1;
                start = false;
                last_dup = false;
            }
        }
    }
    Ok(out)
}

/// ERE-family dialects -> native (grep) ERE.
fn extended_rewrite(d: Dialect, p: &[u8]) -> Result<Vec<u8>, DialectError> {
    let invalid_ops = matches!(d, Dialect::PosixExtended | Dialect::PosixAwk);
    let literal_lead_ops = matches!(d, Dialect::Awk | Dialect::GnuAwk);
    let no_intervals = d == Dialect::Awk;
    let no_backrefs = d == Dialect::Awk;
    let awk = d.is_awk();
    let gnu_ops = d.gnu_ops();
    let mut out = Vec::with_capacity(p.len() + 4);
    let mut i = 0;
    let mut start = true;
    while i < p.len() {
        let c = p[i];
        match c {
            b'\\' => {
                if i + 1 >= p.len() {
                    out.push(b'\\');
                    i += 1;
                    continue;
                }
                let k = p[i + 1];
                i += 2;
                match k {
                    b'w' | b'W' | b's' | b'S' | b'b' | b'B' | b'<' | b'>' if !gnu_ops => {
                        push_ere_literal(&mut out, k)
                    }
                    b'`' | b'\'' if !gnu_ops => push_ere_literal(&mut out, k),
                    b'`' => out.push(b'^'),
                    b'\'' => out.push(b'$'),
                    b'1'..=b'9' if no_backrefs => out.push(k),
                    _ => {
                        out.push(b'\\');
                        out.push(k);
                    }
                }
                start = false;
            }
            b'(' | b'|' => {
                out.push(c);
                i += 1;
                start = true;
            }
            b'*' | b'+' | b'?' | b'{' => {
                if c == b'{' && no_intervals {
                    out.extend_from_slice(b"\\{");
                    i += 1;
                    start = false;
                    continue;
                }
                if start {
                    if invalid_ops {
                        return Err(DialectError::BadRepeat);
                    }
                    if literal_lead_ops {
                        out.push(b'\\');
                    }
                }
                out.push(c);
                i += 1;
                start = false;
            }
            b'}' if no_intervals => {
                out.extend_from_slice(b"\\}");
                i += 1;
                start = false;
            }
            b'[' => {
                if awk {
                    let (neg, members, next) = parse_foreign_bracket(p, i, true, true)?;
                    emit_bracket(&mut out, neg, &members);
                    i = next;
                } else {
                    let end = native_bracket_end(p, i).unwrap_or(p.len());
                    out.extend_from_slice(&p[i..end]);
                    i = end;
                }
                start = false;
            }
            _ => {
                out.push(c);
                i += 1;
                start = false;
            }
        }
    }
    Ok(out)
}

#[cfg(test)]
mod tests {
    use super::*;

    /// Whole-text match under a dialect (find `-regex` semantics).
    fn full(d: Dialect, pat: &str, text: &str) -> bool {
        compile(d, pat.as_bytes(), false)
            .unwrap_or_else(|e| panic!("{pat:?} under {d:?}: {e:?}"))
            .matches_entire(text)
    }

    /// The fixture names of the findutils probe (p3, 2026-11-14).
    const NAMES: [&str; 18] = [
        "./ab", "./aab", "./a+b", "./a?b", "./a|b", "./a(b)", "./a{2}", "./aa", "./a.b", "./x^y",
        "./x$y", "./[x]", "./a\\b", "./b", "./abab", "./Ab", "./a*", "./aXb",
    ];

    fn hits(d: Dialect, pat: &str) -> Vec<&'static str> {
        let re = compile(d, pat.as_bytes(), false).unwrap();
        let mut v: Vec<&str> = NAMES
            .iter()
            .copied()
            .filter(|n| re.matches_entire(n))
            .collect();
        v.sort();
        v
    }

    #[test]
    fn names_table_lookup() {
        assert_eq!(Dialect::from_name("emacs"), Some(Dialect::Emacs));
        assert_eq!(
            Dialect::from_name("posix-minimal-basic"),
            Some(Dialect::PosixMinimalBasic)
        );
        assert_eq!(Dialect::from_name("Emacs"), None);
        assert_eq!(DIALECT_NAMES[0].0, "findutils-default");
    }

    #[test]
    fn emacs_operators_match_gnu_probe() {
        let e = Dialect::Emacs;
        assert_eq!(hits(e, r"\./a+b"), ["./aab", "./ab"]);
        assert_eq!(hits(e, r"\./a\+b"), ["./a+b"]);
        assert_eq!(hits(e, r"\./a?b"), ["./ab", "./b"]);
        assert_eq!(hits(e, r"\./a\?b"), ["./a?b"]);
        assert_eq!(hits(e, r"\./\(a\|b\)"), ["./b"]);
        assert!(hits(e, r"\./(a|b)").is_empty());
        assert_eq!(hits(e, r"\./a|b"), ["./a|b"]);
        assert_eq!(hits(e, r"\./a{2}"), ["./a{2}"]);
        assert_eq!(hits(e, r"\./a\{2\}"), ["./a{2}"]);
        assert_eq!(hits(e, r"\./\(ab\)\1"), ["./abab"]);
        assert!(hits(e, r".*[[:upper:]].*").is_empty());
        assert_eq!(hits(e, r"\./a\\b"), ["./a\\b"]);
        assert!(hits(e, r".*[\]x].*").is_empty());
        assert_eq!(hits(e, r"\./x^y"), ["./x^y"]);
        assert_eq!(hits(e, r"\./x$y"), ["./x$y"]);
        assert!(hits(e, r"*.*b").is_empty());
        assert_eq!(hits(e, r"\./a\w"), ["./aa", "./ab"]);
        assert_eq!(hits(e, r"\`\./ab\'"), ["./ab"]);
        assert_eq!(hits(e, r"\./a**"), ["./aa"]);
        assert_eq!(hits(e, r".*\(^\|/\)ab"), ["./ab"]);
        assert_eq!(hits(e, r".*\b.*").len(), 18);
    }

    #[test]
    fn emacs_errors() {
        let e = Dialect::Emacs;
        let msg = |p: &str| compile(e, p.as_bytes(), false).unwrap_err().glibc_message();
        assert_eq!(msg(r"\(\./ab"), "Unmatched ( or \\(");
        assert_eq!(msg(r"\./ab\)"), "Unmatched ) or \\)");
        assert_eq!(msg(r"[\./ab"), "Unmatched [, [^, [:, [., or [=");
        assert_eq!(msg(r"\./(ab)\1"), "Invalid back reference");
        assert_eq!(msg("ab\\"), "Trailing backslash");
        assert!(compile(e, b"(\\./ab", false).is_ok());
    }

    #[test]
    fn emacs_brackets_reemit() {
        let e = Dialect::Emacs;
        assert!(full(e, "[[:]x", ":x"));
        assert!(full(e, "[[:]x", "[x"));
        assert!(full(e, "[]a]", "]"));
        assert!(full(e, "[^a]", "b"));
        assert!(!full(e, "[^a]", "a"));
        assert!(full(e, "[[^]", "^"));
        assert!(full(e, "[[^]", "["));
        assert!(full(e, "[a-]", "-"));
        assert!(full(e, "[\\]", "\\"));
    }

    #[test]
    fn basic_family() {
        assert_eq!(hits(Dialect::PosixBasic, r"\./a\+b"), ["./aab", "./ab"]);
        assert_eq!(hits(Dialect::PosixBasic, r"\./a\{2\}"), ["./aa"]);
        assert_eq!(
            compile(Dialect::PosixBasic, br"\./a**", false).unwrap_err(),
            DialectError::BadRepeat
        );
        assert_eq!(
            compile(Dialect::Sed, br"\./a**", false).unwrap_err(),
            DialectError::BadRepeat
        );
        assert_eq!(hits(Dialect::Grep, r"\./a**"), ["./aa"]);
        assert_eq!(hits(Dialect::PosixMinimalBasic, r"\./a**"), ["./aa"]);
        assert_eq!(hits(Dialect::PosixMinimalBasic, r"\./a\+b"), ["./a+b"]);
        assert_eq!(hits(Dialect::PosixMinimalBasic, r"\./\(a\|b\)"), ["./a|b"]);
        assert_eq!(
            hits(Dialect::PosixMinimalBasic, r"\./a\{1,2\}b"),
            ["./aab", "./ab"]
        );
        assert!(hits(Dialect::PosixBasic, r"*.*b").is_empty());
    }

    #[test]
    fn extended_family() {
        assert_eq!(
            compile(Dialect::PosixExtended, b"*.*b", false).unwrap_err(),
            DialectError::BadRepeat
        );
        assert_eq!(
            compile(Dialect::PosixAwk, b"*.*b", false).unwrap_err(),
            DialectError::BadRepeat
        );
        assert_eq!(hits(Dialect::Egrep, "*.*b").len(), 11);
        assert!(hits(Dialect::Awk, "*.*b").is_empty());
        assert!(hits(Dialect::GnuAwk, "*.*b").is_empty());
        assert_eq!(hits(Dialect::PosixExtended, r"\./(ab)\1"), ["./abab"]);
        assert!(hits(Dialect::Awk, r"\./(ab)\1").is_empty());
        assert_eq!(hits(Dialect::Awk, r"\./a{2}"), ["./a{2}"]);
        assert_eq!(hits(Dialect::GnuAwk, r"\./a{2}"), ["./aa"]);
        assert_eq!(
            hits(Dialect::Awk, r".*[\]x].*"),
            ["./[x]", "./x$y", "./x^y"]
        );
        assert!(hits(Dialect::PosixExtended, r".*[\]x].*").is_empty());
        assert!(hits(Dialect::Awk, r"\./a\w").is_empty());
        assert!(hits(Dialect::PosixAwk, r"\./a\w").is_empty());
        assert_eq!(hits(Dialect::GnuAwk, r"\./a\w"), ["./aa", "./ab"]);
        assert_eq!(
            hits(Dialect::PosixEgrep, r"\./a{,2}b"),
            ["./aab", "./ab", "./b"]
        );
    }

    #[test]
    fn ignore_case() {
        let re = compile(Dialect::Emacs, br"\./AB", true).unwrap();
        assert!(re.matches_entire("./ab"));
        assert!(re.matches_entire("./Ab"));
    }
}
