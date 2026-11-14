//! `fnmatch(3)` as GNU find calls it: flags 0 for `-name`/`-path`/
//! `-lname` (so `*` and `?` match `/` and a leading `.`), FNM_CASEFOLD
//! for the `-i` variants. Byte-oriented (C locale), gnulib fnmatch
//! semantics: backslash quotes the next byte, `[!...]` and `[^...]` negate,
//! `[:class:]` names inside brackets, a `]` right after the opener is a
//! member, an unterminated `[` is an ordinary byte.

fn fold(b: u8, icase: bool) -> u8 {
    if icase {
        b.to_ascii_lowercase()
    } else {
        b
    }
}

fn class_matches(name: &[u8], c: u8) -> Option<bool> {
    Some(match name {
        b"alnum" => c.is_ascii_alphanumeric(),
        b"alpha" => c.is_ascii_alphabetic(),
        b"blank" => c == b' ' || c == b'\t',
        b"cntrl" => c.is_ascii_control(),
        b"digit" => c.is_ascii_digit(),
        b"graph" => c.is_ascii_graphic(),
        b"lower" => c.is_ascii_lowercase(),
        b"print" => c.is_ascii_graphic() || c == b' ',
        b"punct" => c.is_ascii_punctuation(),
        b"space" => matches!(c, b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c),
        b"upper" => c.is_ascii_uppercase(),
        b"xdigit" => c.is_ascii_hexdigit(),
        _ => return None,
    })
}

/// Try to match a bracket expression starting at `p[0] == b'['` against
/// byte `c`. Returns `Some((matched, consumed))`, or `None` when the
/// bracket is unterminated (the `[` is then an ordinary byte).
fn bracket(p: &[u8], c: u8, icase: bool) -> Option<(bool, usize)> {
    let mut i = 1;
    let negate = i < p.len() && (p[i] == b'!' || p[i] == b'^');
    if negate {
        i += 1;
    }
    let mut matched = false;
    let mut first = true;
    let fc = fold(c, icase);
    loop {
        if i >= p.len() {
            return None;
        }
        let mut lo = p[i];
        if lo == b']' && !first {
            i += 1;
            break;
        }
        first = false;
        if lo == b'[' && i + 1 < p.len() && p[i + 1] == b':' {
            // [:class:]
            let rest = &p[i + 2..];
            if let Some(end) = rest.windows(2).position(|w| w == b":]") {
                let name = &rest[..end];
                match class_matches(name, c) {
                    Some(m) => {
                        let m = m
                            || (icase
                                && (class_matches(name, c.to_ascii_lowercase()) == Some(true)
                                    || class_matches(name, c.to_ascii_uppercase()) == Some(true)));
                        matched |= m;
                    }
                    // unknown class: glibc fnmatch makes the whole
                    // pattern fail to match
                    None => return Some((false, p.len())),
                }
                i += 2 + end + 2;
                continue;
            }
        }
        if lo == b'\\' && i + 1 < p.len() {
            i += 1;
            lo = p[i];
        }
        i += 1;
        // range?
        if i + 1 < p.len() && p[i] == b'-' && p[i + 1] != b']' {
            let mut hi = p[i + 1];
            let mut adv = 2;
            if hi == b'\\' && i + 2 < p.len() {
                hi = p[i + 2];
                adv = 3;
            }
            i += adv;
            let (l, h) = (fold(lo, icase), fold(hi, icase));
            if (l <= fc && fc <= h) || (lo <= c && c <= hi) {
                matched = true;
            }
            continue;
        }
        if fold(lo, icase) == fc {
            matched = true;
        }
    }
    Some((matched != negate, i))
}

/// GNU `fnmatch(pattern, text, icase ? FNM_CASEFOLD : 0) == 0`.
pub fn fnmatch(pattern: &[u8], text: &[u8], icase: bool) -> bool {
    // iterative wildcard matcher with single-star backtracking
    let (mut p, mut t) = (0usize, 0usize);
    let mut star: Option<(usize, usize)> = None;
    while t < text.len() {
        if p < pattern.len() {
            let pc = pattern[p];
            match pc {
                b'*' => {
                    while p < pattern.len() && pattern[p] == b'*' {
                        p += 1;
                    }
                    if p == pattern.len() {
                        return true;
                    }
                    star = Some((p, t));
                    continue;
                }
                b'?' => {
                    p += 1;
                    t += 1;
                    continue;
                }
                b'[' => {
                    if let Some((m, used)) = bracket(&pattern[p..], text[t], icase) {
                        if m {
                            p += used;
                            t += 1;
                            continue;
                        }
                    } else if text[t] == b'[' {
                        p += 1;
                        t += 1;
                        continue;
                    }
                }
                b'\\' if p + 1 < pattern.len() => {
                    if fold(pattern[p + 1], icase) == fold(text[t], icase) {
                        p += 2;
                        t += 1;
                        continue;
                    }
                }
                _ => {
                    if fold(pc, icase) == fold(text[t], icase) {
                        p += 1;
                        t += 1;
                        continue;
                    }
                }
            }
        }
        match star {
            Some((sp, st)) => {
                p = sp;
                t = st + 1;
                star = Some((sp, st + 1));
            }
            None => return false,
        }
    }
    while p < pattern.len() && pattern[p] == b'*' {
        p += 1;
    }
    p == pattern.len()
}

#[cfg(test)]
mod tests {
    use super::fnmatch;

    fn m(p: &str, t: &str) -> bool {
        fnmatch(p.as_bytes(), t.as_bytes(), false)
    }

    #[test]
    fn wildcards() {
        assert!(m("*.rs", "main.rs"));
        assert!(m("*", ".hidden"));
        assert!(m("a*b*c", "aXXbYYc"));
        assert!(!m("a*b*c", "aXXbYY"));
        assert!(m("t1/*", "t1/new"));
        assert!(m("t1*", "t1/new"));
        assert!(m("?", "/"));
        assert!(!m("?", ""));
        assert!(m("", ""));
    }

    #[test]
    fn brackets_and_escapes() {
        assert!(m("[n]ew", "new"));
        assert!(m("[!n]*", "old"));
        assert!(m("[^n]*", "old"));
        assert!(!m("[^n]*", "new"));
        assert!(m("*[[:alpha:]]", "new"));
        assert!(!m("*[[:digit:]]", "new"));
        assert!(m("[]a]", "]"));
        assert!(m("[a-c]", "b"));
        assert!(m("n\\ew", "new"));
        assert!(!m("\\*", "new"));
        assert!(m("\\*", "*"));
        assert!(m("[ab", "[ab"));
        assert!(!m("[[:bogus:]]", "a"));
    }

    #[test]
    fn casefold() {
        assert!(fnmatch(b"X.RS", b"x.rs", true));
        assert!(fnmatch(b"[A-C]x", b"bX", true));
        assert!(!fnmatch(b"X.RS", b"x.rs", false));
    }
}
