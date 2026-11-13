//! Acceptance gate: byte-exact parity with GNU grep -o (WSL GNU grep 3.11,
//! LC_ALL=C, glibc regexec) over tests/matrix.tsv — 138 pattern x input
//! combinations across BRE and ERE.
//!
//! The committed baseline tests/matrix_expected.txt was produced by
//! target/regex-probe/gnu_matrix.sh on the WSL GNU side and the engine side
//! diffed CLEAN against it (0 differing lines). This test re-runs the
//! engine over the same table and requires byte-identical output.
//!
//! Match emission mirrors GNU grep -o's observable rule (probed): only
//! non-empty matches are printed, one per line, in scan order; an empty
//! match advances one byte.

use winux_regex::{Regex, Syntax};

/// Same emission as examples/matrix_harness.rs (kept in sync).
fn engine_rows(tsv: &str) -> String {
    let mut out = String::new();
    for (idx, line) in tsv.split('\n').enumerate() {
        let line = line.trim_end_matches('\r');
        if line.is_empty() {
            continue;
        }
        let parts: Vec<&str> = line.split('\t').collect();
        assert_eq!(parts.len(), 3, "row {} malformed", idx);
        let syntax = match parts[0] {
            "BRE" => Syntax::Basic,
            "ERE" => Syntax::Extended,
            other => panic!("row {} unknown syntax {}", idx, other),
        };
        out.push_str(&format!("##ROW {}\n", idx));
        let Ok(re) = Regex::compile_bytes(syntax, parts[1].as_bytes()) else {
            // GNU grep prints nothing for a compile error; so do we (the
            // accept/reject boundary itself is pinned by parser_accept).
            continue;
        };
        let text = parts[2].as_bytes();
        let mut cursor = 0usize;
        while cursor <= text.len() {
            let Some(m) = re.find_at_bytes(text, cursor) else {
                break;
            };
            if m.end > m.begin {
                out.push_str(&String::from_utf8_lossy(m.slice_of(text)));
                out.push('\n');
            }
            cursor = if m.end > m.begin { m.end } else { m.begin + 1 };
        }
    }
    out
}

#[test]
fn byte_exact_vs_gnu_grep_o() {
    let tsv = include_str!("matrix.tsv");
    let expected = include_str!("matrix_expected.txt");
    let got = engine_rows(tsv);
    if got != expected {
        // Show the first divergence for diagnosis.
        let got_lines: Vec<&str> = got.lines().collect();
        let want_lines: Vec<&str> = expected.lines().collect();
        for i in 0..got_lines.len().max(want_lines.len()) {
            let g = got_lines.get(i).copied().unwrap_or("<EOF>");
            let w = want_lines.get(i).copied().unwrap_or("<EOF>");
            if g != w {
                panic!(
                    "first divergence at output line {}:\n  engine: {:?}\n  GNU:    {:?}",
                    i + 1,
                    g,
                    w
                );
            }
        }
        panic!("outputs differ only in length");
    }
}

#[test]
fn matrix_covers_at_least_100_combinations() {
    let tsv = include_str!("matrix.tsv");
    let rows = tsv.lines().filter(|l| !l.trim().is_empty()).count();
    assert!(
        rows >= 100,
        "matrix shrunk below the acceptance bar: {}",
        rows
    );
}
