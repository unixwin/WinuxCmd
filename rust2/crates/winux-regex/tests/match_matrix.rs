//! Phase 2: match-semantics matrix. Leftmost-longest order, capture
//! selection, backreferences, repeat/empty-iteration behavior, anchors,
//! word boundaries, UTF-8 codepoint handling. Every row's expected value is
//! transcribed from a GNU probe: `grep -o` / `sed 's/.../.../'` (glibc
//! regexec, LC_ALL=C) captured in `target/regex-probe/` — notably
//! sed_probe.sh, sed_probe2.sh, iso_probe.sh, anchor_probe.sh.

use winux_regex::{Match, Regex, Syntax};

fn bre(pattern: &str) -> Regex {
    Regex::compile(Syntax::Basic, pattern).expect("compile")
}

fn ere(pattern: &str) -> Regex {
    Regex::compile(Syntax::Extended, pattern).expect("compile")
}

/// Overall span of the first match from 0.
fn find_span(re: &Regex, text: &str) -> Option<(usize, usize)> {
    re.find_at(text, 0).map(|m| (m.begin, m.end))
}

/// Capture 1..=n of the first match from 0 as Option<&str> spans.
fn find_groups(re: &Regex, text: &str) -> Vec<Option<(usize, usize)>> {
    let m = re.find_at(text, 0).expect("expected a match");
    (1..re.capture_count() + 1).map(|i| m.group(i)).collect()
}

fn grp(text: &str, span: Option<(usize, usize)>) -> String {
    match span {
        Some((b, e)) => text[b..e].to_string(),
        None => String::new(),
    }
}

// --- leftmost-longest (the regex-crate killer cases) ---------------------

#[test]
fn leftmost_longest() {
    // GNU: grep -E -o 'a|ab' over "ab" prints "ab"; PCRE prints "a".
    assert_eq!(find_span(&ere("a|ab"), "ab"), Some((0, 2)));
    // GNU: sed -E 's/(a|aa)/<\1>/' over "aab" -> "<aa>b".
    assert_eq!(find_span(&ere("(a|aa)"), "aab"), Some((0, 2)));
    assert_eq!(find_span(&ere("(aa|a)"), "aab"), Some((0, 2)));
    // GNU: sed -E 's/(a|ab)(c|bcd)/<\1,\2>/' over "abcd" -> "<a,bcd>".
    let re = ere("(a|ab)(c|bcd)");
    let m = re.find_at("abcd", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 4));
    let g = |i: usize| -> String {
        match m.group(i) {
            Some((b, e)) => "abcd"[b..e].to_string(),
            None => String::new(),
        }
    };
    assert_eq!((g(1), g(2)), ("a".to_string(), "bcd".to_string()));
    // BRE alternation picks longest too.
    assert_eq!(find_span(&bre(r"a\|ab"), "ab"), Some((0, 2)));
    // Literal beats nothing: exact longest at the leftmost start.
    assert_eq!(find_span(&ere("x|xxy"), "xxyy"), Some((0, 3)));
}

// --- capture selection (glibc-aligned) ------------------------------------

#[test]
fn capture_selection_glibc() {
    // GNU: sed 's/\(a*\)\(a*\)/<\1,\2>/' over "aaa" -> "<aaa,>" — the first
    // group is greedily longest.
    let (t, re) = ("aaa", bre(r"\(a*\)\(a*\)"));
    assert_eq!(find_span(&re, t), Some((0, 3)));
    let g = find_groups(&re, t);
    assert_eq!(
        (grp(t, g[0]), grp(t, g[1])),
        ("aaa".to_string(), "".to_string())
    );

    // GNU: sed 's/\(a*\)*$/<\1>/' over "aaa" -> "<aaa>" — trailing empty
    // iterations do NOT wipe a committed non-empty match (glibc update_regs).
    let re = bre(r"\(a*\)*$");
    assert_eq!(find_span(&re, "aaa"), Some((0, 3)));
    assert_eq!(grp("aaa", find_groups(&re, "aaa")[0]), "aaa");

    // GNU: sed -E 's/^((x)*)*$/[\1|\2]/' over "xxxx" -> "[xxxx|x]" — the
    // outer group keeps the full line, the inner keeps the LAST iteration.
    let re = ere("((x)*)*$");
    let m = re.find_at("xxxx", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 4));
    let g1 = m.group(1).map(|(b, e)| &"xxxx"[b..e]);
    let g2 = m.group(2).map(|(b, e)| &"xxxx"[b..e]);
    assert_eq!((g1, g2), (Some("xxxx"), Some("x")));
    // `+` behaves the same (GNU probed identically).
    let re = ere("((x)*)+$");
    let m = re.find_at("xxxx", 0).unwrap();
    assert_eq!(
        (
            m.group(1).map(|(b, e)| &"xxxx"[b..e]),
            m.group(2).map(|(b, e)| &"xxxx"[b..e])
        ),
        (Some("xxxx"), Some("x"))
    );

    // GNU: sed 's/\(a*\)\{2\}/<\1>/' over "aaa" -> "<>" — an EXACT {2}
    // leaves the group empty (unmarked trailing copy); {2,3} / {1,2} keep
    // the full run.
    let re = bre(r"\(a*\)\{2\}");
    assert_eq!(find_span(&re, "aaa"), Some((0, 3)));
    assert_eq!(grp("aaa", find_groups(&re, "aaa")[0]), "");
    let re = bre(r"\(a*\)\{2,3\}");
    assert_eq!(grp("aaa", find_groups(&re, "aaa")[0]), "aaa");
    let re = bre(r"\(a*\)\{1,2\}");
    assert_eq!(grp("aaa", find_groups(&re, "aaa")[0]), "aaa");

    // GNU: sed 's/^\(.*\)\{2,\}$/<\1>/' over "aaa" -> "<aaa>" — the whole
    // line is one full iteration plus one empty one.
    let re = bre(r"^\(.*\)\{2,\}$");
    let m = re.find_at("aaa", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 3));
    assert_eq!(m.group(1).map(|(b, e)| &"aaa"[b..e]), Some("aaa"));

    // GNU: sed -E 's/(a)(b)?c/<\1,\2>/' over "ac" -> "<a,>" — unmatched
    // optional group reports empty (group() -> None).
    let re = ere("(a)(b)?c");
    let m = re.find_at("ac", 0).unwrap();
    assert_eq!(m.group(1), Some((0, 1)));
    assert_eq!(m.group(2), None);

    // GNU: sed -E 's/(x)|(y)/<\1,\2>/' over "y" -> "<,y>".
    let re = ere("(x)|(y)");
    let m = re.find_at("y", 0).unwrap();
    assert_eq!(m.group(1), None);
    assert_eq!(m.group(2), Some((0, 1)));
    // over "xyz" -> "<x,>yz" (leftmost branch wins at pos 0).
    let m = re.find_at("xyz", 0).unwrap();
    assert_eq!(m.group(1), Some((0, 1)));
    assert_eq!(m.group(2), None);

    // GNU: sed -E 's/(a)*$/<\1>/' over "aaa" -> "<a>" — last iteration.
    let re = ere("(a)*$");
    assert_eq!(grp("aaa", find_groups(&re, "aaa")[0]), "a");

    // GNU: sed -E 's/(a){2}/<\1>/' over "aaa" -> "<a>a" — the last COPY of
    // the interval keeps the register.
    let re = ere("(a){2}");
    let m = re.find_at("aaa", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 2));
    assert_eq!(m.group(1).map(|(b, e)| &"aaa"[b..e]), Some("a"));

    // GNU: sed 's/\(aa\)\{2\}/<\1>/' over "aaaa" -> "<aa>".
    let re = bre(r"\(aa\)\{2\}");
    let m = re.find_at("aaaa", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 4));
    assert_eq!(m.group(1).map(|(b, e)| &"aaaa"[b..e]), Some("aa"));

    // GNU: sed 's/\(o\)*o*/<\1>/' over "foo" -> "<>foo" — the empty match
    // at position 0 is leftmost and wins over "oo" at position 1.
    let re = bre(r"\(o\)*o*");
    let m = re.find_at("foo", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 0));
    assert_eq!(m.group(1), None);

    // GNU: sed -E 's/((abc))\1*/<\1,\2>/' over "abcabc" -> "<abc,abc>".
    let re = ere("((abc))\\1*");
    let m = re.find_at("abcabc", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 6));
    assert_eq!(m.group(1).map(|(b, e)| &"abcabc"[b..e]), Some("abc"));
    assert_eq!(m.group(2).map(|(b, e)| &"abcabc"[b..e]), Some("abc"));

    // GNU: sed 's/\(a*\)b\1/<\1>/' over "aaab" -> "aaa<>" — only "b"
    // matches (leftmost starts 0..2 cannot extend), the group participated
    // with an EMPTY match at 3 (matched-empty, not unset).
    let re = bre(r"\(a*\)b\1");
    let m = re.find_at("aaab", 0).unwrap();
    assert_eq!((m.begin, m.end), (3, 4));
    assert_eq!(m.group(1), Some((3, 3)));

    // GNU: sed 's/\(ab\)\1/<\1>/' over "abab" -> "<ab>".
    let re = bre(r"\(ab\)\1");
    let m = re.find_at("abab", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 4));
    assert_eq!(m.group(1).map(|(b, e)| &"abab"[b..e]), Some("ab"));

    // Nested groups: GNU sed 's/\(x\(a\)y\)/<\1,\2>/' over "xay".
    let re = bre(r"\(x\(a\)y\)");
    let m = re.find_at("xay", 0).unwrap();
    assert_eq!(m.group(1).map(|(b, e)| &"xay"[b..e]), Some("xay"));
    assert_eq!(m.group(2).map(|(b, e)| &"xay"[b..e]), Some("a"));

    // GNU: grep -o '\(a*\)\(b\)\1\2' over "abab" -> "abab" (whole line).
    let re = bre(r"\(a*\)\(b\)\1\2");
    let m = re.find_at("abab", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 4));
    assert_eq!(m.group(1).map(|(b, e)| &"abab"[b..e]), Some("a"));
    assert_eq!(m.group(2).map(|(b, e)| &"abab"[b..e]), Some("b"));
}

// --- find_at / find_all / match_at / matches_entire ------------------------

#[test]
fn find_and_spans() {
    // GNU: grep -o '[0-9]\{3\}' over "abc123def456" -> "123","456".
    let re = bre("[0-9]\\{3\\}");
    let ms = re.find_all_bytes(b"abc123def456");
    let spans: Vec<(usize, usize)> = ms.iter().map(|m| (m.begin, m.end)).collect();
    assert_eq!(spans, vec![(3, 6), (9, 12)]);

    // Start offset: find_at(text, k) only sees matches at/beyond k.
    let re = bre("ab");
    assert_eq!(find_span(&re, "abab"), Some((0, 2)));
    assert_eq!(
        re.find_at("abab", 1).map(|m| (m.begin, m.end)),
        Some((2, 4))
    );
    assert_eq!(
        re.find_at("abab", 2).map(|m| (m.begin, m.end)),
        Some((2, 4))
    );
    assert_eq!(re.find_at("abab", 3), None);

    // match_at anchors; find_at searches.
    let re = bre("b");
    assert_eq!(re.match_at("ab", 0), None);
    assert_eq!(re.match_at("ab", 1).map(|m| (m.begin, m.end)), Some((1, 2)));
    assert_eq!(re.find_at("ab", 0).map(|m| (m.begin, m.end)), Some((1, 2)));

    // matches_entire (find -regex semantics).
    let re = ere("a.c");
    assert!(re.matches_entire("abc"));
    assert!(!re.matches_entire("abcd"));
    assert!(!re.matches_entire("xabc"));

    // Zero-width matches advance one byte (C++ find_all rule).
    let re = bre("x\\{0,0\\}");
    let ms = re.find_all_bytes(b"ab");
    assert_eq!(ms.len(), 3); // at 0, 1, 2 (end)
    assert!(ms.iter().all(|m| m.begin == m.end));

    // Empty-capable patterns yield empty matches between content matches.
    let re = bre("b*");
    let ms = re.find_all_bytes(b"ab");
    let spans: Vec<(usize, usize)> = ms.iter().map(|m| (m.begin, m.end)).collect();
    assert_eq!(spans, vec![(0, 0), (1, 2), (2, 2)]);
    // Note: `grep -o` prints only "b" — the empty-match SUPPRESSION is a
    // grep-side emit rule, not an engine rule (documented for the grep wave).
}

// --- anchors ----------------------------------------------------------------

#[test]
fn anchors() {
    // ^ anchors to absolute start in a line (grep strips \n).
    let re = bre("^abc");
    assert_eq!(find_span(&re, "abcabc"), Some((0, 3)));
    let ms = re.find_all_bytes(b"abcabc");
    assert_eq!(ms.len(), 1);
    let re = bre("abc$");
    assert_eq!(
        re.find_at("abcabc", 0).map(|m| (m.begin, m.end)),
        Some((3, 6))
    );
    // ERE mid-pattern anchors never match (GNU-verified: `grep -E 'a^b'`
    // finds nothing on "a^b").
    let re = ere("a^b");
    assert_eq!(re.find_at("a^b", 0), None);
    // BRE mid-pattern ^/$ are literals (GNU-verified).
    let re = bre("a^b");
    assert_eq!(find_span(&re, "a^b"), Some((0, 3)));
    let re = bre("a$b");
    assert_eq!(find_span(&re, "a$b"), Some((0, 3)));
    // BRE branch-start anchors still anchor (GNU: a\|^b finds "b" on "b",
    // nothing on "xb"; on "a^b" branch 1 matches "a").
    let re = bre("a\\|^b");
    assert_eq!(find_span(&re, "xb"), None);
    assert_eq!(find_span(&re, "b"), Some((0, 1)));
    assert_eq!(find_span(&re, "a^b"), Some((0, 1)));
    let re = bre("\\(a$\\)");
    assert_eq!(find_span(&re, "a"), Some((0, 1)));
    assert_eq!(find_span(&re, "az"), None);
    // ^*a: the * is a literal after the initial ^ (GNU: finds nothing).
    let re = bre("^*a");
    assert_eq!(find_span(&re, "a"), None);
    // ERE leading quantifier repeats the empty expression: matches empty.
    let re = ere("^*a");
    let m = re.find_at("a", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 1));
    // {2} at expression start repeats the empty expression (GNU count=1).
    let re = ere("{2}");
    let m = re.find_at("ab", 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 0));
    // `{2,1}` at expression start is literal braces.
    let re = ere("{2,1}");
    assert_eq!(re.find_at("a", 0), None);
    assert_eq!(find_span(&re, "x{2,1}y"), Some((1, 6)));
}

// --- word boundaries and UTF-8 ----------------------------------------------

#[test]
fn word_boundaries() {
    // GNU: grep -o '\bx' over "abcabc xxyy aaab ab z{} [a] (x) n" -> "x","x".
    let re = bre("\\bx");
    let ms = re.find_all_bytes(b"abcabc xxyy");
    assert_eq!(ms.len(), 1);
    // \b at a word edge.
    let re = bre("\\babc\\b");
    assert_eq!(find_span(&re, "abc"), Some((0, 3)));
    assert_eq!(find_span(&re, "xabcx"), None);
    // \< \> behave as GNU.
    let re = bre("\\<x");
    assert_eq!(
        re.find_at("abc x", 0).map(|m| (m.begin, m.end)),
        Some((4, 5))
    );
    assert_eq!(re.find_at("axb", 0), None);
    let re = bre("x\\>");
    assert_eq!(
        re.find_at("ab x", 0).map(|m| (m.begin, m.end)),
        Some((3, 4))
    );
    // \B between word chars (GNU: ab\B matched "ab","ab").
    let re = bre("ab\\B");
    assert_eq!(find_span(&re, "abc abc"), Some((0, 2)));
    assert_eq!(re.find_at("ab ab", 0), None);
}

#[test]
fn utf8_codepoints() {
    // D7: `.` consumes a whole codepoint (GNU grep under a UTF-8 locale;
    // the C-locale GNU behavior of slicing bytes is NOT the engine spec —
    // documented divergence, same as the C++ engine).
    let re = bre(".");
    let text = "中a";
    let m = re.find_at(text, 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 3)); // one 3-byte codepoint
    let re = bre("中.");
    assert_eq!(find_span(&re, "中x"), Some((0, 4)));
    // Word boundary treats multi-byte codepoints as word chars.
    let re = bre("\\b中");
    assert_eq!(find_span(&re, "中"), Some((0, 3)));
    assert_eq!(find_span(&re, " 中"), Some((1, 4)));
    // The scan never starts mid-codepoint.
    let re = bre("a");
    assert_eq!(re.find_at("中a", 0).map(|m| (m.begin, m.end)), Some((3, 4)));
    // Invalid/truncated lead bytes degrade to single bytes.
    let bytes: &[u8] = &[0xE4, b'a', b'b'];
    let re = bre(".");
    let m = re.match_at_bytes(bytes, 0).unwrap();
    assert_eq!(m.end, 1); // 0xE4 with a non-continuation follower = 1 byte
}

// --- backreferences ----------------------------------------------------------

#[test]
fn backreferences() {
    // GNU: grep -o '\(abc\)\1' over "abcabc ..." -> "abcabc".
    let re = bre(r"\(abc\)\1");
    assert_eq!(find_span(&re, "abcabc"), Some((0, 6)));
    // ERE backrefs are the GNU extension; \1 with no group is a compile
    // error (covered in Phase 1).
    let re = ere("(a)\\1");
    assert_eq!(find_span(&re, "aa"), Some((0, 2)));
    // Single-digit only: \(a\)\10 = \1 + literal 0 (GNU-verified "aa0").
    let re = bre(r"\(a\)\10");
    assert_eq!(find_span(&re, "aa0"), Some((0, 3)));
    // Case-folded backrefs under -i (glibc REG_ICASE).
    let re = Regex::compile_ignore_case(Syntax::Basic, r"\(a\)b\1").unwrap();
    assert_eq!(find_span(&re, "AbA"), Some((0, 3)));
    // Backref to a group that matched empty: matches empty, so the pattern
    // is just `x` per occurrence.
    let re = bre(r"\(\)\1x");
    assert_eq!(find_span(&re, "xx"), Some((0, 1)));
}

// --- case folding --------------------------------------------------------------

#[test]
fn ignore_case() {
    let re = Regex::compile_ignore_case(Syntax::Basic, "abc").unwrap();
    assert_eq!(find_span(&re, "AbC"), Some((0, 3)));
    // Class ranges fold at compile (C++ rule: bounds folded to lower), so
    // -i [A-Z]+ covers both cases: GNU grep -i '[A-Z]+' over "abcXYZ"
    // prints the whole "abcXYZ".
    let re = Regex::compile_ignore_case(Syntax::Extended, "[A-Z]+").unwrap();
    assert_eq!(find_span(&re, "abcXYZ"), Some((0, 6)));
    assert_eq!(find_span(&re, "XYZ"), Some((0, 3)));
    // POSIX class cross-fold: -i [[:upper:]] matches lowercase too.
    let re = Regex::compile_ignore_case(Syntax::Basic, "[[:upper:]]").unwrap();
    assert_eq!(find_span(&re, "1a2"), Some((1, 2)));
    let re = Regex::compile_ignore_case(Syntax::Basic, "[[:lower:]]").unwrap();
    assert_eq!(find_span(&re, "1A2"), Some((1, 2)));
}

// --- classes in the matcher -----------------------------------------------------

#[test]
fn classes_match() {
    // GNU: grep -E -o '[[:alpha:]]+' picks letter runs; under BRE the
    // trailing `+` is a LITERAL (tested below with the plain class).
    let re = ere("[[:alpha:]]+");
    assert_eq!(find_span(&re, "12ab34"), Some((2, 4)));
    let re = ere("[[:digit:]]+");
    assert_eq!(find_span(&re, "12ab34"), Some((0, 2)));
    let re = bre("[[:alpha:]]");
    assert_eq!(find_span(&re, "12ab34"), Some((2, 3)));
    let re = bre("[[:space:]]");
    assert_eq!(find_span(&re, "ab cd"), Some((2, 3)));
    // Blank is space+tab in the C locale.
    let re = bre("[[:blank:]]");
    assert_eq!(find_span(&re, "a\tb"), Some((1, 2)));
    assert_eq!(re.find_at("a\nb", 0), None);
    // Negation and ranges.
    let re = bre("[^a-z]");
    assert_eq!(find_span(&re, "abZ12"), Some((2, 3)));
    // GNU: [a-z\]] = class {a-z,\} then literal ] (probe: `b\]c` -> "\]").
    let re = bre("[a-z\\]]");
    let m = re.match_at("b\\]c", 1).unwrap();
    assert_eq!((m.begin, m.end), (1, 3));
    assert_eq!(re.match_at("b\\", 1), None);
    // \w \s shorthands are POSIX classes (GNU); the runs need ERE because
    // a BRE `+` is a literal (GNU: '\w+' under BRE matches one word char).
    let re = ere("\\w+");
    assert_eq!(find_span(&re, "..abc_1.."), Some((2, 7)));
    let re = ere("\\s+");
    assert_eq!(find_span(&re, "ab  cd"), Some((2, 4)));
    let re = bre("\\w");
    assert_eq!(find_span(&re, "..abc_1.."), Some((2, 3)));
}

// --- work budget ------------------------------------------------------------------

#[test]
fn work_budget_degrades_consistently() {
    // A pathological nested repeat stays bounded: every attempt is
    // memoized (polynomial), and when the caps trip the answer degrades to
    // a conservative no-match (`checked_*` would surface WorkLimit). Same
    // tradeoff as the C++ engine (WinuxCmd #1495). GNU grep still matches
    // such lines via its DFA prefilter; consumers keep that through the
    // documented limit — never a hang.
    let line: String = "a".repeat(60);
    let re = bre(r"\(.*\)*b");
    let t0 = std::time::Instant::now();
    assert_eq!(re.find_at_bytes(line.as_bytes(), 0), None);
    assert!(t0.elapsed() < std::time::Duration::from_secs(5));
    // The same pattern on ordinary text still finds the full line
    // (leftmost-longest, greedy single iteration).
    let line_b: String = format!("{}b", "a".repeat(40));
    let re = bre(r"\(.*\)*b");
    let m = re.find_at_bytes(line_b.as_bytes(), 0).unwrap();
    assert_eq!((m.begin, m.end), (0, 41));
    // checked_* agree with the plain form on normal patterns.
    assert!(re.checked_find_at_bytes(line_b.as_bytes(), 0).is_ok());
}

// --- API surface --------------------------------------------------------------------

#[test]
fn api_shapes() {
    let re = Regex::compile(Syntax::Extended, "(a+)(b?)").unwrap();
    assert_eq!(re.capture_count(), 2);
    let m: Match = re.find_at("aab", 0).unwrap();
    assert_eq!(m.slice_of(b"aab"), b"aab");
    assert_eq!(m.group(0), Some((0, 3)));
    assert_eq!(m.group(1), Some((0, 2)));
    assert_eq!(m.group(2), Some((2, 3)));
    assert_eq!(m.group(3), None);
    // checked_* surface work limits as errors; normal patterns are Ok.
    assert!(re.checked_find_at("aab", 0).is_ok());
    assert!(re.checked_match_at("aab", 0).is_ok());
    // Empty pattern matches empty everywhere.
    let re = Regex::compile(Syntax::Basic, "").unwrap();
    assert_eq!(find_span(&re, "abc"), Some((0, 0)));
    // start beyond text length is None, not a panic.
    let re = bre("a");
    assert_eq!(re.find_at("ab", 5), None);
    assert_eq!(re.match_at("ab", 5), None);
}
