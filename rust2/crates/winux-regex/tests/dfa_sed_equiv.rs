//! Differential suite for the lazy line DFA used as the sed ADDRESS filter
//! (WinuxCmd #1488 sed scan-perf lane): for every (pattern, line) pair the
//! DFA path must decide exactly what the interpreter decides, i.e. the
//! `matches_line` gate in `winux-cmds/src/sed/compile.rs` must never change
//! an address verdict.
//!
//! The gate this test mirrors:
//!
//! * DFA absent (back-references, `M` anchors, oversized automaton) ->
//!   interpreter;
//! * `InputRequirement::Any` -> DFA on any bytes;
//! * `ValidUtf8` -> DFA only on well-formed UTF-8 (ASCII included);
//! * `Ascii` -> DFA only on ASCII;
//! * a line containing the eol byte is never given to the DFA (sed's
//!   `N`-joined pattern space keeps the interpreter);
//! * a regex without the Thompson NFA (back-references, oversized interval)
//!   never builds one: its verdict comes from the budgeted backtracker,
//!   whose exhaustion is a conservative no-match, so an exact DFA answer
//!   could differ (sed builds the filter only when `Regex::has_nfa()`).

use winux_regex::dfa::{InputRequirement, LineDfa, LineMode};
use winux_regex::{CompileOptions, Regex, Syntax};

/// The sed gate's decision, byte-for-byte the `SedRegex::matches_line` rule.
fn sed_gate(re: &Regex, dfa: &mut Option<LineDfa>, eol: u8, line: &[u8]) -> bool {
    if let Some(d) = dfa.as_mut() {
        let input_ok = match d.input_requirement() {
            InputRequirement::Any => true,
            InputRequirement::ValidUtf8 => line.is_ascii() || std::str::from_utf8(line).is_ok(),
            InputRequirement::Ascii => line.is_ascii(),
        };
        if input_ok && !line.contains(&eol) {
            return d.is_match_line(line);
        }
    }
    let mut cache = re.new_cache();
    re.is_match_with(&mut cache, line)
}

fn check(pattern: &str, ere: bool, icase: bool, lines: &[&[u8]]) {
    let syntax = if ere { Syntax::Extended } else { Syntax::Basic };
    let opts = CompileOptions::sed(icase, false);
    let re = match Regex::compile_with(syntax, pattern.as_bytes(), opts) {
        Ok(re) => re,
        Err(e) => panic!("{pattern:?} does not compile: {e}"),
    };
    let mut dfa = built(&re, b'\n');
    for line in lines {
        let want = {
            let mut cache = re.new_cache();
            re.is_match_with(&mut cache, line)
        };
        let got = sed_gate(&re, &mut dfa, b'\n', line);
        assert_eq!(
            want,
            got,
            "pattern {pattern:?} (ere={ere}, icase={icase}) line {:?} (dfa={:?})",
            String::from_utf8_lossy(line),
            dfa.as_ref().map(|d| d.input_requirement())
        );
    }
}

/// The sed construction rule: a line filter exists only for regexes that
/// have the exact Thompson NFA.
fn built(re: &Regex, eol: u8) -> Option<LineDfa> {
    if re.has_nfa() {
        LineDfa::new(&[re], LineMode::Contains, eol)
    } else {
        None
    }
}

/// Deterministic xorshift (no external crates).
struct Rng(u64);

impl Rng {
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x << 13;
        x ^= x >> 7;
        x ^= x << 17;
        self.0 = x;
        x
    }
    fn below(&mut self, n: usize) -> usize {
        (self.next() % n as u64) as usize
    }
    fn pick<'a>(&mut self, xs: &[&'a str]) -> &'a str {
        xs[self.below(xs.len())]
    }
}

const ATOMS: &[&str] = &[
    "a",
    "b",
    "c",
    "f",
    "o",
    "x",
    "-",
    " ",
    "_",
    "1",
    "é",
    "中",
    ".",
    "[ab]",
    "[^a]",
    "[[:alpha:]]",
    "[a-c]",
    "[[:digit:]]",
    "[[:space:]]",
    "^",
    "$",
    "\\<",
    "\\>",
    "\\b",
    "\\B",
    "\\w",
    "\\W",
    "&",
];
const QUANTS: &[&str] = &["", "", "", "*", "+", "?", "\\{1,2\\}", "\\{2\\}"];

fn gen_bre(r: &mut Rng, depth: usize) -> String {
    let n = 1 + r.below(3);
    let mut out = String::new();
    for _ in 0..n {
        if depth < 2 && r.below(6) == 0 {
            out.push_str("\\(");
            out.push_str(&gen_bre(r, depth + 1));
            if r.below(2) == 0 {
                out.push_str("\\|");
                out.push_str(&gen_bre(r, depth + 1));
            }
            out.push_str("\\)");
        } else {
            out.push_str(r.pick(ATOMS));
        }
        let q = r.pick(QUANTS);
        // `^*` is legal BRE but uninteresting; skip quantifying assertions.
        if !matches!(q, "") && !out.ends_with(['^', '$']) {
            out.push_str(q);
        }
    }
    out
}

/// Lines: generated ASCII, multibyte valid UTF-8, ill-formed bytes, empty,
/// and one containing the eol byte (the `N`-joined case).
fn corpus(r: &mut Rng) -> Vec<Vec<u8>> {
    let alphabet: &[&str] = &[
        "a", "b", "c", "f", "o", "x", "-", " ", "_", "1", "é", "中", "Z",
    ];
    let mut out: Vec<Vec<u8>> = vec![
        b"".to_vec(),
        b"a".to_vec(),
        b"abc".to_vec(),
        b"the quick brown fox".to_vec(),
        "é中a".as_bytes().to_vec(),
        b"\xff\xfeab".to_vec(),
        b"a\nb".to_vec(),
        b"lorem ipsum dolor sit amet".to_vec(),
    ];
    for _ in 0..300 {
        let n = r.below(9);
        let mut line = String::new();
        for _ in 0..n {
            line.push_str(r.pick(alphabet));
        }
        out.push(line.into_bytes());
    }
    // A few ill-formed lines: a continuation byte alone, a lone lead byte.
    out.push(vec![0x80, b'a']);
    out.push(vec![0xE4, b'a']);
    out.push(vec![0xE4, 0xB8, 0xAD, b'a']);
    out
}

#[test]
fn sed_address_gate_matches_interpreter() {
    let mut r = Rng(0x5EED_1488_0DF3_68D);
    let lines = corpus(&mut r);
    let line_refs: Vec<&[u8]> = lines.iter().map(|l| l.as_slice()).collect();
    // Hand-picked sed shapes: the benchmark regex, anchor and word forms,
    // BRE groups, a back-reference (DFA must decline), a `M` anchor (decline),
    // and the empty match.
    for (pat, ere) in [
        ("lorem.*amet", false),
        ("lorem.*amet", true),
        ("^the", false),
        ("fox$", false),
        ("^$", false),
        ("a\\|b", false),
        ("a|b", true),
        ("\\(a\\)\\(b\\)", false),
        ("(a)(b)", true),
        ("\\(a\\)\\1", false),
        ("\\<fox\\>", false),
        ("[[:alpha:]]\\{2,3\\}", false),
        ("x*", false),
        ("[^ ]*b", false),
        ("é", false),
        ("中.*a", false),
        (".", false),
        ("^.*$", false),
    ] {
        check(pat, ere, false, &line_refs);
    }
    // `-i` folding (`I` flag) goes through the same chars_equal rule; and a
    // `-z` record model (eol = NUL) sees `\n` as an ordinary byte.
    for pat in ["lorem.*amet", "^the", "[ab]\\{2\\}", "\\<fox\\>", "é"] {
        check(pat, false, true, &line_refs);
    }
    // Generated shapes (BRE, sed dialect).
    let (mut engaged, mut declined) = (0usize, 0usize);
    for _ in 0..250 {
        let pat = gen_bre(&mut r, 0);
        if pat.is_empty() || pat.as_bytes().first() == Some(&b'*') {
            continue;
        }
        let re = Regex::compile_with(
            Syntax::Basic,
            pat.as_bytes(),
            CompileOptions::sed(false, false),
        );
        let Ok(re) = re else { continue };
        let mut dfa = built(&re, b'\n');
        if dfa.is_some() {
            engaged += 1;
        } else {
            declined += 1;
        }
        for line in &line_refs {
            let want = {
                let mut cache = re.new_cache();
                re.is_match_with(&mut cache, line)
            };
            let got = sed_gate(&re, &mut dfa, b'\n', line);
            assert_eq!(
                want,
                got,
                "pattern {pat:?} line {:?}",
                String::from_utf8_lossy(line)
            );
        }
    }
    // Not vacuous: the generated corpus must actually exercise the DFA path
    // (a suite that only ever ran the interpreter would pass trivially).
    assert!(
        engaged >= 50,
        "DFA engaged on only {engaged} patterns (declined {declined}): suite is vacuous"
    );
}

/// `-z` builds the DFA with eol = NUL: a record containing `\n` is ordinary
/// text for the DFA, and the verdict must still match the interpreter.
#[test]
fn dfa_nul_record_model_matches_interpreter() {
    let re = Regex::compile_with(
        Syntax::Basic,
        b"lorem.*amet",
        CompileOptions::sed(false, false),
    )
    .unwrap();
    let mut dfa = built(&re, 0);
    let lines: Vec<&[u8]> = vec![
        b"lorem ipsum amet",
        b"a\nlorem\namet",
        b"lorem x amet\0tail",
        b"\n\n",
        b"",
    ];
    for line in lines {
        let want = {
            let mut cache = re.new_cache();
            re.is_match_with(&mut cache, line)
        };
        assert_eq!(want, sed_gate(&re, &mut dfa, 0, line), "line {line:?}");
    }
}

/// The `M`-flag anchors and buffer anchors are outside the line model: the
/// DFA must decline so sed keeps the interpreter.
#[test]
fn dfa_declines_multiline_and_backrefs() {
    for pat in ["^a", "a$", "a\\|^b"] {
        let re = Regex::compile_with(
            Syntax::Basic,
            pat.as_bytes(),
            CompileOptions::sed(false, true),
        );
        let Ok(re) = re else { continue };
        let d = LineDfa::new(&[&re], LineMode::Contains, b'\n');
        assert!(
            d.is_none(),
            "M-flag pattern {pat:?} must stay on the interpreter"
        );
    }
    let re = Regex::compile_with(
        Syntax::Basic,
        b"\\(a\\)\\1",
        CompileOptions::sed(false, false),
    )
    .unwrap();
    assert!(LineDfa::new(&[&re], LineMode::Contains, b'\n').is_none());
    // An interval expansion above the Thompson-NFA cap has no exact boolean
    // search (the backtracker answers under a work budget), so the sed
    // construction rule declines even though the DFA could build.
    let re = Regex::compile_with(
        Syntax::Basic,
        b"a\\{30000\\}",
        CompileOptions::sed(false, false),
    )
    .unwrap();
    assert!(
        !re.has_nfa(),
        "30000-copy interval should exceed the NFA cap"
    );
    assert!(built(&re, b'\n').is_none());
}
