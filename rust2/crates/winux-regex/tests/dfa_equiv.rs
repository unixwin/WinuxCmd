//! Differential suite for the lazy line DFA (`winux_regex::dfa`): on every
//! generated (pattern set, line) pair the DFA must decide exactly what the
//! interpreter decides — `find_at_bytes(line, 0).is_some()` for
//! `LineMode::Contains`, `matches_entire_bytes(line)` for
//! `LineMode::Entire` — and `find_line` over a multi-line buffer must stop
//! at the first line the interpreter selects. Ill-formed UTF-8 lines are
//! only compared when the DFA reports it does not need well-formed input
//! (the module contract).

use winux_regex::dfa::{InputRequirement, LineDfa, LineMode};
use winux_regex::{Regex, Syntax};

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

const ERE_ATOMS: &[&str] = &[
    "a",
    "b",
    "c",
    "A",
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
    "[é]",
];
const QUANTS: &[&str] = &["", "", "", "*", "+", "?", "{1,2}", "{2}", "{0,1}", "{0,}"];

fn gen_ere(r: &mut Rng, depth: usize) -> String {
    let n = 1 + r.below(4);
    let mut out = String::new();
    for _ in 0..n {
        if depth < 3 && r.below(5) == 0 {
            out.push('(');
            out.push_str(&gen_ere(r, depth + 1));
            if r.below(2) == 0 {
                out.push('|');
                out.push_str(&gen_ere(r, depth + 1));
            }
            out.push(')');
        } else {
            out.push_str(r.pick(ERE_ATOMS));
        }
        let q = r.pick(QUANTS);
        // `^*` and friends are legal but uninteresting; skip quantifying
        // the assertions.
        let last = out.chars().last().unwrap_or(' ');
        if !matches!(last, '^' | '$' | '<' | '>') && !out.ends_with("\\b") && !out.ends_with("\\B")
        {
            out.push_str(q);
        }
    }
    if depth == 0 && r.below(4) == 0 {
        out.push('|');
        out.push_str(&gen_ere(r, 1));
    }
    out
}

/// ERE -> BRE spelling of the generated subset.
fn to_bre(ere: &str) -> String {
    let mut out = String::new();
    let mut in_bracket = false;
    let chars: Vec<char> = ere.chars().collect();
    let mut i = 0;
    while i < chars.len() {
        let c = chars[i];
        if in_bracket {
            out.push(c);
            if c == ']' && !(i > 0 && (chars[i - 1] == '[' || chars[i - 1] == '^')) {
                // crude: the generated brackets never hold a leading ']'
                let opened = out.matches('[').count();
                let closed = out.matches(']').count();
                if closed >= opened {
                    in_bracket = false;
                }
            }
            i += 1;
            continue;
        }
        match c {
            '[' => {
                in_bracket = true;
                out.push(c);
            }
            '\\' => {
                out.push(c);
                if i + 1 < chars.len() {
                    out.push(chars[i + 1]);
                    i += 1;
                }
            }
            '(' | ')' | '|' | '+' | '?' | '{' | '}' => {
                out.push('\\');
                out.push(c);
            }
            _ => out.push(c),
        }
        i += 1;
    }
    out
}

const TEXT_UNITS: &[&[u8]] = &[
    b"a",
    b"b",
    b"c",
    b"A",
    b"B",
    b"-",
    b" ",
    b"_",
    b"1",
    b"2",
    "é".as_bytes(),
    "中".as_bytes(),
    "É".as_bytes(),
    b"ab",
    b"abc",
    b"\t",
];
const BAD_UNITS: &[&[u8]] = &[b"\xff", b"\xc3", b"\xa9", b"\xe4\xb8", b"\x80"];

fn gen_line(r: &mut Rng, allow_bad: bool) -> Vec<u8> {
    let n = r.below(9);
    let mut out = Vec::new();
    for _ in 0..n {
        if allow_bad && r.below(6) == 0 {
            out.extend_from_slice(BAD_UNITS[r.below(BAD_UNITS.len())]);
        } else {
            out.extend_from_slice(TEXT_UNITS[r.below(TEXT_UNITS.len())]);
        }
    }
    out
}

fn interp(regexes: &[Regex], mode: LineMode, line: &[u8]) -> Option<bool> {
    let mut any = false;
    for re in regexes {
        let hit = match mode {
            LineMode::Contains => match re.checked_find_at_bytes(line, 0) {
                Ok(m) => m.is_some(),
                Err(_) => return None, // work limit: not comparable
            },
            LineMode::Entire => re.matches_entire_bytes(line),
        };
        any |= hit;
    }
    Some(any)
}

fn run_case(r: &mut Rng, syntax: Syntax, ic: bool, mode: LineMode, npat: usize) -> usize {
    let mut regexes = Vec::new();
    let mut spelled = Vec::new();
    while regexes.len() < npat {
        let ere = gen_ere(r, 0);
        let pat = if syntax == Syntax::Basic {
            to_bre(&ere)
        } else {
            ere
        };
        let compiled = if ic {
            Regex::compile_ignore_case(syntax, &pat)
        } else {
            Regex::compile(syntax, &pat)
        };
        if let Ok(re) = compiled {
            regexes.push(re);
            spelled.push(pat);
        }
    }
    let refs: Vec<&Regex> = regexes.iter().collect();
    let Some(mut dfa) = LineDfa::new(&refs, mode, b'\n') else {
        return 0;
    };
    let req = dfa.input_requirement();
    let allow_bad = req == InputRequirement::Any;
    let mut checked = 0;
    let mut buf = Vec::new();
    let mut expect_first: Option<usize> = None;
    let mut starts = Vec::new();
    for li in 0..12 {
        let mut line = gen_line(r, allow_bad);
        if req == InputRequirement::Ascii {
            line.retain(|b| b.is_ascii());
        }
        let Some(want) = interp(&regexes, mode, &line) else {
            return checked;
        };
        let got = dfa.is_match_line(&line);
        assert_eq!(
            got,
            want,
            "mode {:?} ic {} syntax {:?} patterns {:?} line {:?}",
            mode,
            ic,
            syntax,
            spelled,
            String::from_utf8_lossy(&line)
        );
        if want && expect_first.is_none() {
            expect_first = Some(li);
        }
        starts.push(buf.len());
        buf.extend_from_slice(&line);
        buf.push(b'\n');
        checked += 1;
    }
    let found = dfa.find_line(&buf, 0, buf.len());
    match expect_first {
        None => assert!(found.is_none(), "find_line false positive {:?}", spelled),
        Some(li) => {
            let (s, e) = found.expect("find_line missed a line");
            assert_eq!(s, starts[li], "find_line start, patterns {:?}", spelled);
            assert_eq!(buf[e], b'\n');
        }
    }
    checked
}

#[test]
fn dfa_matches_interpreter_on_generated_corpus() {
    let mut r = Rng(0x1488_2026_beef_cafe);
    let mut total = 0usize;
    let rounds: usize = std::env::var("DFA_EQUIV_ROUNDS")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(6000);
    for round in 0..rounds {
        let syntax = if round % 2 == 0 {
            Syntax::Extended
        } else {
            Syntax::Basic
        };
        let ic = round % 5 == 0;
        let mode = if round % 3 == 0 {
            LineMode::Entire
        } else {
            LineMode::Contains
        };
        let npat = 1 + (round % 4 == 1) as usize * 2;
        total += run_case(&mut r, syntax, ic, mode, npat);
    }
    assert!(total > 40_000, "too few comparisons: {total}");
}

#[test]
fn dfa_matrix_rows_agree_with_interpreter() {
    // Every row of the GNU acceptance matrix, both modes.
    let tsv = include_str!("matrix.tsv");
    for line in tsv.lines() {
        let parts: Vec<&str> = line.trim_end_matches('\r').split('\t').collect();
        if parts.len() != 3 {
            continue;
        }
        let syntax = if parts[0] == "BRE" {
            Syntax::Basic
        } else {
            Syntax::Extended
        };
        let Ok(re) = Regex::compile(syntax, parts[1]) else {
            continue;
        };
        for mode in [LineMode::Contains, LineMode::Entire] {
            let Some(mut dfa) = LineDfa::new(&[&re], mode, b'\n') else {
                continue;
            };
            let text = parts[2].as_bytes();
            for sub in [text, &text[..text.len() / 2], b"" as &[u8]] {
                let want = interp(std::slice::from_ref(&re), mode, sub).unwrap();
                assert_eq!(dfa.is_match_line(sub), want, "{:?} {:?}", parts[1], sub);
            }
        }
    }
}

#[test]
fn dfa_refuses_back_references_and_handles_nul_eol() {
    let re = Regex::compile(Syntax::Basic, r"\(a\)\1").unwrap();
    assert!(LineDfa::new(&[&re], LineMode::Contains, b'\n').is_none());
    // -z: newline is an ordinary byte, NUL ends lines.
    let re = Regex::compile(Syntax::Basic, "a.b").unwrap();
    let mut dfa = LineDfa::new(&[&re], LineMode::Contains, 0).unwrap();
    assert!(dfa.is_match_line(b"a\nb"));
    let buf = b"zz\0xa\nb\0";
    assert_eq!(dfa.find_line(buf, 0, buf.len()), Some((3, 7)));
    // empty set matches nothing
    let mut none = LineDfa::new(&[], LineMode::Contains, b'\n').unwrap();
    assert!(!none.is_match_line(b"anything"));
    assert!(!none.is_match_line(b""));
}

#[test]
fn dfa_cache_flush_keeps_answers() {
    // (a|b)*a(a|b){12} has ~2^13 DFA states: forces cache flushes.
    let re = Regex::compile(Syntax::Extended, "(a|b)*a(a|b){12}").unwrap();
    let mut dfa = LineDfa::new(&[&re], LineMode::Contains, b'\n').unwrap();
    let mut r = Rng(42);
    for _ in 0..400 {
        let line: Vec<u8> = (0..60)
            .map(|_| if r.below(2) == 0 { b'a' } else { b'b' })
            .collect();
        let want = re.find_at_bytes(&line, 0).is_some();
        assert_eq!(dfa.is_match_line(&line), want);
    }
}
