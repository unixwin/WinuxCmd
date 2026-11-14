//! `-regex` / `-iregex` matcher: whole-path match (GNU re_match length ==
//! path length) through winux_regex::dialect, plus one find-side
//! acceleration for the dominant pattern shape `.*TAIL`.
//!
//! `.*TAIL` matches a whole path iff TAIL whole-matches some suffix that
//! starts on a character boundary (`.` consumes any codepoint but `\n`).
//! The leading `.*` makes the general interpreter carry one state per
//! path position through the whole tail; splitting it off turns the test
//! into a few cheap suffix attempts, filtered by TAIL's first literal
//! byte. The split is taken only when it is exact: no top-level
//! alternation (`.*a|b` is `(.*a)|b`), no quantifier on the `.*`, no
//! left-context-sensitive token in TAIL (`^`, `\``, `\b`, `\B`, `\<`,
//! `\>`), and the path holds no newline. Otherwise the full regex runs.

use winux_regex::dialect::{self, Dialect, DialectError};
use winux_regex::{Regex, Syntax};

pub struct FindRegex {
    full: Regex,
    tail: Option<(Regex, Option<u8>)>,
    icase: bool,
}

/// Is the remainder after a leading `.*` safe to match on suffixes?
/// Returns TAIL's first literal byte (if it starts with one) on success.
fn tail_ok(syntax: Syntax, t: &[u8]) -> Option<Option<u8>> {
    if let Some(&c) = t.first() {
        if matches!(c, b'*' | b'+' | b'?' | b'{') && syntax == Syntax::Extended {
            return None;
        }
        if c == b'*' {
            return None;
        }
        if c == b'\\'
            && matches!(t.get(1), Some(b'{') | Some(b'+') | Some(b'?'))
            && syntax == Syntax::Basic
        {
            return None;
        }
    }
    let mut depth = 0i32;
    let mut i = 0;
    while i < t.len() {
        match t[i] {
            b'\\' => {
                let k = *t.get(i + 1)?;
                match k {
                    b'b' | b'B' | b'<' | b'>' | b'`' => return None,
                    b'(' if syntax == Syntax::Basic => depth += 1,
                    b')' if syntax == Syntax::Basic => depth -= 1,
                    b'|' if syntax == Syntax::Basic && depth == 0 => return None,
                    _ => {}
                }
                i += 2;
            }
            b'^' => return None,
            b'(' if syntax == Syntax::Extended => {
                depth += 1;
                i += 1;
            }
            b')' if syntax == Syntax::Extended => {
                depth -= 1;
                i += 1;
            }
            b'|' if syntax == Syntax::Extended && depth <= 0 => return None,
            b'[' => {
                // skip the bracket ([:x:], leading ] / ^])
                let mut j = i + 1;
                if t.get(j) == Some(&b'^') {
                    j += 1;
                }
                if t.get(j) == Some(&b']') {
                    j += 1;
                }
                loop {
                    match t.get(j) {
                        None => return None,
                        Some(b']') => break,
                        Some(b'[') if matches!(t.get(j + 1), Some(b':' | b'.' | b'=')) => {
                            let close = t[j + 1];
                            j += 2;
                            while j + 1 < t.len() && !(t[j] == close && t[j + 1] == b']') {
                                j += 1;
                            }
                            j += 2;
                        }
                        _ => j += 1,
                    }
                }
                i = j + 1;
            }
            _ => i += 1,
        }
    }
    // first literal byte of TAIL (followed by no quantifier)
    let first = match t.first() {
        Some(&c) if !b".[\\()*+?{|^$".contains(&c) => {
            let q = t.get(1).copied();
            let quantified = match syntax {
                Syntax::Extended => matches!(q, Some(b'*' | b'+' | b'?' | b'{')),
                Syntax::Basic => {
                    q == Some(b'*')
                        || (q == Some(b'\\') && matches!(t.get(2), Some(b'{' | b'+' | b'?')))
                }
            };
            if quantified {
                None
            } else {
                Some(c)
            }
        }
        _ => None,
    };
    Some(first)
}

impl FindRegex {
    pub fn compile(d: Dialect, pattern: &[u8], icase: bool) -> Result<FindRegex, DialectError> {
        let full = dialect::compile(d, pattern, icase)?;
        let mut tail = None;
        if let Ok((syntax, native)) = dialect::translate(d, pattern) {
            if let Some(rest) = native.strip_prefix(b".*") {
                if let Some(first) = tail_ok(syntax, rest) {
                    if let Ok(re) = Regex::compile_bytes_ignore_case(syntax, rest, icase) {
                        tail = Some((re, first));
                    }
                }
            }
        }
        Ok(FindRegex { full, tail, icase })
    }

    pub fn is_match(&self, p: &[u8]) -> bool {
        if let Some((tail, first)) = &self.tail {
            if !p.contains(&b'\n') {
                for i in 0..=p.len() {
                    if i < p.len() && (p[i] & 0xC0) == 0x80 {
                        continue;
                    }
                    if let Some(f) = first {
                        match p.get(i) {
                            Some(&b) if b == *f || (self.icase && b.eq_ignore_ascii_case(f)) => {}
                            _ => continue,
                        }
                    }
                    if tail.matches_entire_bytes(&p[i..]) {
                        return true;
                    }
                }
                return false;
            }
        }
        self.full.matches_entire_bytes(p)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn both(d: Dialect, pat: &str, text: &str) -> bool {
        let r = FindRegex::compile(d, pat.as_bytes(), false).unwrap();
        let slow = dialect::compile(d, pat.as_bytes(), false)
            .unwrap()
            .matches_entire(text);
        assert_eq!(r.is_match(text.as_bytes()), slow, "{pat:?} on {text:?}");
        slow
    }

    #[test]
    fn split_agrees_with_full_match() {
        let texts = [
            "t/a/x.rs",
            "./tree/d000/s1/f004.rs",
            "x.rs.txt",
            "rs",
            "",
            "a/b",
            "./ab",
            "café/x.rs",
            "a|b",
        ];
        let pats = [
            ".*\\.rs",
            ".*/f0[0-4][0-9]\\.rs",
            ".*rs",
            ".*a|b",
            ".*\\(a\\|b\\)",
            ".*\\bx.*",
            ".*/x\\.rs$",
            ".*\\(.\\)\\1.*",
            ".*",
            ".*é/x.*",
        ];
        for p in pats {
            for t in texts {
                both(Dialect::Emacs, p, t);
            }
        }
        for p in [".*\\.(rs|c)", ".*a|b", ".*[[:digit:]]+\\.rs", ".*+x"] {
            for t in texts {
                both(Dialect::PosixEgrep, p, t);
            }
        }
        assert!(FindRegex::compile(Dialect::Emacs, b".*\\.rs", false)
            .unwrap()
            .tail
            .is_some());
        assert!(FindRegex::compile(Dialect::Emacs, b".*a\\|b", false)
            .unwrap()
            .tail
            .is_none());
    }
}
