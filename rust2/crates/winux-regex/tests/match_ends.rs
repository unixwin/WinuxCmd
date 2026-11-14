//! `Regex::match_ends_at_bytes`: every end of a match anchored at a start,
//! longest first — the shorter-match walk of grep -w. The longest end must
//! always equal `match_at_bytes`, and every listed end must be a real
//! anchored match end (checked against `matches_entire_bytes` on the
//! prefix for patterns without end-context assertions).

use winux_regex::{Regex, Syntax};

#[test]
fn ends_are_descending_and_agree_with_match_at() {
    let cases: &[(Syntax, &str, &str, usize, &[usize])] = &[
        (Syntax::Extended, "a|ab|abc", "abcd", 0, &[3, 2, 1]),
        (Syntax::Extended, "a*", "aaab", 0, &[3, 2, 1, 0]),
        (Syntax::Basic, "fo*", "xfoo", 1, &[4, 3, 2]),
        (Syntax::Basic, "fo*", "xfoo", 0, &[]),
        (Syntax::Extended, "(ab)+", "ababa", 0, &[4, 2]),
        (Syntax::Extended, "x$", "ax", 1, &[2]),
        (Syntax::Extended, "x$", "axx", 1, &[]),
        (Syntax::Basic, r"\(a\)\1*", "aaa", 0, &[3, 2, 1]),
    ];
    for (syntax, pat, text, start, want) in cases {
        let re = Regex::compile(*syntax, pat).unwrap();
        let got = re.match_ends_at_bytes(text.as_bytes(), *start);
        assert_eq!(&got[..], *want, "{pat} on {text} at {start}");
        let best = re.match_at_bytes(text.as_bytes(), *start).map(|m| m.end);
        assert_eq!(best, got.first().copied(), "{pat} longest end");
    }
}

#[test]
fn every_end_is_a_real_match_end() {
    let pats = [
        "(a|b)*c?",
        "a{1,3}b*",
        "[[:alpha:]]+[0-9]?",
        "(ab|a)(bc|c)?",
    ];
    let texts = ["ababcc", "aaabbb", "abc1x", "abcbc"];
    for p in pats {
        let re = Regex::compile(Syntax::Extended, p).unwrap();
        let anchored = Regex::compile(Syntax::Extended, &format!("^({p})$")).unwrap();
        for t in texts {
            for start in 0..=t.len() {
                for e in re.match_ends_at_bytes(t.as_bytes(), start) {
                    assert!(
                        anchored.matches_entire_bytes(&t.as_bytes()[start..e]),
                        "{p}: [{start},{e}) of {t}"
                    );
                }
            }
        }
    }
}
