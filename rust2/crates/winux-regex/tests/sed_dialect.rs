//! GNU sed 4.9 dialect (`CompileOptions::sed`) — every expectation below
//! was probed against WSL GNU sed 4.9 (C.UTF-8) with `s/<re>/X/` over the
//! quoted subject; an `Err` row is the glibc message sed prints after
//! `sed: -e expression #1, char N: ` (lower-cased here, the engine's
//! message text). The grep defaults are untouched: the last test pins the
//! default dialect on the same patterns.

use winux_regex::{CompileOptions, Regex, Syntax};

fn sed(syntax: Syntax, pat: &str) -> Result<Regex, String> {
    Regex::compile_with(syntax, pat.as_bytes(), CompileOptions::sed(false, false))
        .map_err(|e| e.message().to_string())
}

fn sed_m(syntax: Syntax, pat: &str) -> Regex {
    Regex::compile_with(syntax, pat.as_bytes(), CompileOptions::sed(false, true)).unwrap()
}

/// `s/<pat>/X/` over `text` (first match only), like the probes.
fn subst(re: &Regex, text: &str) -> String {
    let t = text.as_bytes();
    match re.find_at_bytes(t, 0) {
        Some(m) => {
            let mut out = t[..m.begin].to_vec();
            out.push(b'X');
            out.extend_from_slice(&t[m.end..]);
            String::from_utf8(out).unwrap()
        }
        None => text.to_string(),
    }
}

const BADRPT: &str = "invalid preceding regular expression";
const BADBR: &str = "invalid content of \\{\\}";
const EBRACE: &str = "unmatched \\{";

#[test]
fn ere_context_invalid_ops() {
    for p in [
        "*a", "a|*b", "(*a)", "^*", "{1}a", "x|{1}", "a$*", r"x\b*", "(a)|+", r"\`*",
    ] {
        assert_eq!(sed(Syntax::Extended, p).unwrap_err(), BADRPT, "{p}");
    }
    for (p, want) in [
        ("(|a)", "X{x}b"),
        ("a||b", "X{x}b"),
        ("()", "Xa{x}b"),
        ("a**", "X{x}b"),
        ("a+?", "X{x}b"),
        ("a|^b", "X{x}b"),
        ("(^a)", "X{x}b"),
        ("a$b", "a{x}b"),
        (r"a\{1\}", "a{x}b"),
        ("a{,}", "X{x}b"),
    ] {
        let re = sed(Syntax::Extended, p).unwrap();
        assert_eq!(subst(&re, "a{x}b"), want, "{p}");
    }
    let re = sed(Syntax::Extended, "a+*").unwrap();
    assert_eq!(subst(&re, "a**b+c"), "X**b+c");
}

#[test]
fn ere_intervals_follow_glibc_fetch_number() {
    for (p, want) in [
        ("a{x}", BADBR),
        ("a{x", EBRACE),
        ("a{", EBRACE),
        ("a{1,x}", BADBR),
        ("a{1", EBRACE),
        ("a{}", BADBR),
        ("a{2,1}", BADBR),
    ] {
        assert_eq!(sed(Syntax::Extended, p).unwrap_err(), want, "{p}");
    }
    assert_eq!(
        sed(Syntax::Extended, "a{32768}").unwrap_err(),
        "regular expression too big"
    );
}

#[test]
fn ere_stray_close_paren_is_an_error() {
    assert_eq!(
        sed(Syntax::Extended, ")").unwrap_err(),
        "unmatched ) or \\)"
    );
}

#[test]
fn bre_context_invalid_dup() {
    for p in [
        r"\(\{1\}a\)",
        r"a\|\{1\}",
        r"\{1\}a",
        "a**",
        r"a*\{2\}",
        r"a\+*",
        r"a\{1\}*",
        r"\(a\)*\{2\}",
    ] {
        assert_eq!(sed(Syntax::Basic, p).unwrap_err(), BADRPT, "{p}");
    }
    for (p, want) in [
        (r"a\{x\}", BADBR),
        (r"a\{x", EBRACE),
        (r"x\{1,0\}", BADBR),
        (r"a\{1", EBRACE),
    ] {
        assert_eq!(sed(Syntax::Basic, p).unwrap_err(), want, "{p}");
    }
    assert_eq!(sed(Syntax::Basic, r"\)").unwrap_err(), "unmatched ) or \\)");
    for (p, want) in [
        (r"a*\+", "X**b+c"),
        (r"\+a", "a**b+c"),
        (r"\?a", "a**b+c"),
        (r"x\b*", "a**b+c"),
        (r"\(^*\)", "a**b+c"),
        (r"a\|\+", "X**b+c"),
        (r"b\++", "a**Xc"),
        (r"\(*a\)", "a**b+c"),
        ("*a", "a**b+c"),
    ] {
        let re = sed(Syntax::Basic, p).unwrap();
        assert_eq!(subst(&re, "a**b+c"), want, "{p}");
    }
}

#[test]
fn multiline_anchors_dot_and_lists() {
    // `N;s/.../X/M` over "ab\ncd".
    let text = "ab\ncd";
    assert_eq!(subst(&sed_m(Syntax::Basic, "b.c"), text), text);
    assert_eq!(subst(&sed_m(Syntax::Basic, "b[^x]c"), text), text);
    assert_eq!(subst(&sed_m(Syntax::Basic, r"b\Wc"), text), "aXd");
    assert_eq!(subst(&sed_m(Syntax::Basic, r"b\Sc"), text), text);
    assert_eq!(subst(&sed_m(Syntax::Basic, r"b\sc"), text), "aXd");
    assert_eq!(subst(&sed_m(Syntax::Basic, "b$"), text), "aX\ncd");
    assert_eq!(subst(&sed_m(Syntax::Basic, "^c"), text), "ab\nXd");
    assert_eq!(subst(&sed_m(Syntax::Basic, ".*"), text), "X\ncd");
    // `[^a]*` cannot cross the newline nor match `a`: empty at 0.
    assert_eq!(subst(&sed_m(Syntax::Basic, "[^a]*"), text), "Xab\ncd");
    // Buffer anchors stay absolute in M mode.
    let re = sed_m(Syntax::Basic, r"\`");
    assert_eq!(re.find_at_bytes(text.as_bytes(), 1), None);
    let re = sed_m(Syntax::Basic, r"\'");
    let m = re.find_at_bytes(text.as_bytes(), 0).unwrap();
    assert_eq!((m.begin, m.end), (5, 5));
    // Non-M: `.` and lists cross newlines, `^` only at 0.
    let plain = sed(Syntax::Basic, "b.c").unwrap();
    assert_eq!(subst(&plain, text), "aXd");
    let plain = sed(Syntax::Basic, "^c").unwrap();
    assert_eq!(subst(&plain, text), text);
}

#[test]
fn multiline_global_anchor_positions() {
    // `N;s/^/X/mg` -> both line starts; `N;s/$/X/mg` -> both line ends.
    let re = sed_m(Syntax::Basic, "^");
    let starts: Vec<usize> = re
        .find_all_bytes(b"ab\ncd")
        .iter()
        .map(|m| m.begin)
        .collect();
    assert_eq!(starts, vec![0, 3]);
    let re = sed_m(Syntax::Basic, "$");
    let ends: Vec<usize> = re
        .find_all_bytes(b"ab\ncd")
        .iter()
        .map(|m| m.begin)
        .collect();
    assert_eq!(ends, vec![2, 5]);
}

#[test]
fn dot_rejects_invalid_utf8_but_takes_whole_codepoints() {
    let re = sed(Syntax::Basic, ".").unwrap();
    assert_eq!(re.find_at_bytes(b"\xff", 0), None);
    let m = re.find_at_bytes("é".as_bytes(), 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 2));
}

#[test]
fn issue_1496_captures_hold_under_the_sed_dialect() {
    // sed_unit_test.cpp #1496 rows (GNU sed 4.8/4.9 byte-for-byte).
    let cases: &[(Syntax, &str, &str, &[&str])] = &[
        (Syntax::Basic, r"^\(\(x\)*\)*$", "xxxx", &["xxxx", "x"]),
        (Syntax::Extended, r"^((x)*)*$", "xxxx", &["xxxx", "x"]),
        (Syntax::Basic, r"^\(a\(b\)*\)*$", "abab", &["ab", "b"]),
        (Syntax::Basic, r"^\(x\(y*\)\)*$", "xyxy", &["xy", "y"]),
        (Syntax::Basic, r"^\(.*\)\{2,\}$", "abcabc", &["abcabc"]),
        (Syntax::Basic, r"^\(a*\)\{2\}$", "aaa", &[""]),
        (Syntax::Basic, r"^\(a*\)\{2,3\}$", "aaa", &["aaa"]),
        (Syntax::Basic, r"^\(a*\)\{1,2\}$", "aaa", &["aaa"]),
        (Syntax::Basic, r"^\(a*\)\{1,3\}$", "aaa", &[""]),
        (
            Syntax::Basic,
            r"^\(a\|ab\)\(c\|bcd\)\(d*\)$",
            "abcd",
            &["a", "bcd", ""],
        ),
    ];
    for (syntax, pat, text, groups) in cases {
        let re = sed(*syntax, pat).unwrap();
        let m = re.find_at_bytes(text.as_bytes(), 0).expect(pat);
        for (i, want) in groups.iter().enumerate() {
            let got = m.group(i + 1).map(|(b, e)| &text[b..e]).unwrap_or("");
            assert_eq!(got, *want, "{pat} group {}", i + 1);
        }
    }
    // Nested-quantifier blow-up (#1495) stays bounded.
    let re = sed(Syntax::Basic, r"\(.*\)*x").unwrap();
    let text = "a".repeat(40);
    assert_eq!(re.find_at_bytes(text.as_bytes(), 0), None);
}

#[test]
fn default_dialect_is_unchanged() {
    // grep keeps its literal braces, stray `)`, empty-repeat ERE starts,
    // literal backquote, and `.` over newline.
    assert!(Regex::compile(Syntax::Extended, "a{x").is_ok());
    assert!(Regex::compile(Syntax::Extended, ")").is_ok());
    assert!(Regex::compile(Syntax::Extended, "*a").is_ok());
    assert!(Regex::compile(Syntax::Basic, "a**").is_ok());
    let re = Regex::compile(Syntax::Basic, r"\`").unwrap();
    assert!(re.find_at("x`", 0).is_some());
}

#[test]
fn posix_mode_ops_and_paren_ord() {
    // sed --posix (RE_NO_GNU_OPS | RE_LIMITED_OPS), probed sed 4.9.
    let mut o = CompileOptions::sed(false, false);
    o.no_gnu_ops = true;
    o.unmatched_right_paren_ord = true;
    let c = |s: Syntax, p: &str| Regex::compile_with(s, p.as_bytes(), o).unwrap();
    assert_eq!(subst(&c(Syntax::Basic, r"a\+"), "aa+"), "aX");
    assert_eq!(subst(&c(Syntax::Basic, r"a\|b"), "a|b"), "X");
    assert_eq!(subst(&c(Syntax::Basic, r"\w"), "aw b"), "aX b");
    // POSIXLY_CORRECT alone: stray `)` is literal.
    let mut p = CompileOptions::sed(false, false);
    p.unmatched_right_paren_ord = true;
    let re = Regex::compile_with(Syntax::Extended, b")", p).unwrap();
    assert_eq!(subst(&re, "a)b"), "aXb");
}
