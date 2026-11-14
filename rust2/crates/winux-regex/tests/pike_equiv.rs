//! The NFA bounds search (`find_at_bytes_with` / `is_match_with`) must
//! agree with the backtracking matcher span-for-span and capture-for-
//! capture: a pseudo-random differential over patterns x subjects x start
//! offsets, in the grep default dialect and the sed dialect (with `I`/`M`).

use winux_regex::{CompileOptions, Regex, Syntax};

const BRE: &[&str] = &[
    "a",
    "ab",
    "a*",
    "a*b",
    ".",
    ".*",
    "a.c",
    "^a",
    "a$",
    "^$",
    "x*",
    r"\(a\)\(b\)",
    r"\(a*\)*",
    r"\(a\|ab\)\(c\|bcd\)\(d*\)",
    r"a\{2,3\}",
    r"\(ab\)\{2\}",
    r"[a-c]\+",
    r"[^a]*",
    r"\bab",
    r"\Bb",
    r"\<a",
    r"b\>",
    r"\w\+",
    r"\W",
    r"a\?b",
    r"\(.*\)\{2,\}",
    "lorem.*amet",
    "é",
    ".é",
    r"[[:alpha:]]*",
    r"a\|b\|",
    r"\(\)",
    "*a",
    r"^*",
    "a**b",
    r"\`a",
    r"b\'",
    r"\(a*\)\{1,3\}",
    r"[[:space:]]",
    "ab*c*",
    r"\(x\(y*\)\)*",
];
const ERE: &[&str] = &[
    "a|ab|abc",
    "(a|ab)(c|bcd)(d*)",
    "((x)*)*",
    "a+",
    "(ab)+",
    "a{0,}",
    "a{,2}b",
    "(a|)+",
    "[ab]{2}",
    "^(a|b)*$",
    "(.)(.)",
    "x?y?",
    "a|^b",
    "(^a)",
    "c$|a",
    "(a*)+b",
];
const SUBJECTS: &[&str] = &[
    "",
    "a",
    "ab",
    "abc",
    "abcd",
    "aab",
    "baaac",
    "xyxy",
    "ab\ncd",
    "a\nb\n",
    "é",
    "aé b",
    "lorem ipsum dolor sit amet",
    "ababab",
    "xxxx",
    "a b a",
    "\u{ff}ab",
    "abcabc",
    "bcd",
];

/// Deterministic xorshift corpus of extra subjects over a small alphabet.
fn extra_subjects() -> Vec<Vec<u8>> {
    let alphabet: &[u8] = b"abcxy \n";
    let mut s: u64 = 0x1488_1488;
    let mut out = Vec::new();
    for _ in 0..40 {
        s ^= s << 13;
        s ^= s >> 7;
        s ^= s << 17;
        let n = (s % 12) as usize;
        let mut v = Vec::new();
        for _ in 0..n {
            s ^= s << 13;
            s ^= s >> 7;
            s ^= s << 17;
            v.push(alphabet[(s % alphabet.len() as u64) as usize]);
        }
        out.push(v);
    }
    out
}

fn check(re: &Regex, label: &str) {
    let mut cache = re.new_cache();
    let mut subjects: Vec<Vec<u8>> = SUBJECTS.iter().map(|s| s.as_bytes().to_vec()).collect();
    subjects.extend(extra_subjects());
    // invalid UTF-8 byte
    subjects.push(b"a\xffb".to_vec());
    for t in &subjects {
        for start in 0..=t.len() {
            let want = re.find_at_bytes(t, start);
            let got = re.find_at_bytes_with(&mut cache, t, start);
            assert_eq!(got, want, "{label} on {t:?} from {start}");
        }
        assert_eq!(
            re.is_match_with(&mut cache, t),
            re.find_at_bytes(t, 0).is_some(),
            "{label} is_match on {t:?}"
        );
    }
}

#[test]
fn nfa_agrees_with_backtracker_grep_dialect() {
    for p in BRE {
        if let Ok(re) = Regex::compile(Syntax::Basic, p) {
            check(&re, &format!("BRE {p}"));
        }
        if let Ok(re) = Regex::compile_ignore_case(Syntax::Basic, p) {
            check(&re, &format!("BRE -i {p}"));
        }
    }
    for p in ERE {
        if let Ok(re) = Regex::compile(Syntax::Extended, p) {
            check(&re, &format!("ERE {p}"));
        }
    }
}

#[test]
fn nfa_agrees_with_backtracker_sed_dialect() {
    for (icase, mline) in [(false, false), (true, false), (false, true), (true, true)] {
        let o = CompileOptions::sed(icase, mline);
        for p in BRE {
            if let Ok(re) = Regex::compile_with(Syntax::Basic, p.as_bytes(), o) {
                check(&re, &format!("sed BRE {p} I={icase} M={mline}"));
            }
        }
        for p in ERE {
            if let Ok(re) = Regex::compile_with(Syntax::Extended, p.as_bytes(), o) {
                check(&re, &format!("sed ERE {p} I={icase} M={mline}"));
            }
        }
    }
}

#[test]
fn backreference_patterns_fall_back() {
    let re = Regex::compile(Syntax::Basic, r"\(a\)\1").unwrap();
    let mut c = re.new_cache();
    let m = re.find_at_bytes_with(&mut c, b"xaa", 0).unwrap();
    assert_eq!((m.begin, m.end), (1, 3));
    assert!(!re.is_match_with(&mut c, b"ab"));
}

#[test]
fn fixed_width_captures_agree() {
    for p in [
        r"\(a\)\(b\)",
        r"x\(ab\)\{2\}",
        r"\([a-c]\)\(\)\(c\)",
        r"^\(a\)b\(c\)$",
        r"\(a\(b\)\)c",
    ] {
        let re = Regex::compile(Syntax::Basic, p).unwrap();
        check(&re, &format!("fixed {p}"));
    }
}
