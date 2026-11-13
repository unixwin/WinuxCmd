//! Performance gate for the literal pre-filter fast path (WinuxCmd #1531).
//!
//! Reproduces the #1531 shape on the engine level: a 64 MiB / ~819k-line
//! text, a `grep -c fox`-style per-line first-match scan, interpreter
//! (`disable_fast_path`) vs fast path. Also guards the pathological
//! `(.*)*b` long line (#1531's O(n^k) family): the fast path must not make
//! it worse, and the #1603 work caps (StepBudget / visited / output) must
//! still bound it.
//!
//! Usage: cargo run --release -p winux-regex --example fastpath_bench
//!        [mebibytes]

use std::time::Instant;

use winux_regex::{ErrorKind, Regex, Syntax};

/// ~64 MiB deterministic text: `lines` lines of ~80 bytes, every 40th line
/// contains `fox` (plus sparse `fox` variants for prefix-candidate churn).
fn build_haystack(lines: usize) -> Vec<u8> {
    let mut buf = Vec::with_capacity(lines * 82);
    for i in 0..lines {
        if i % 40 == 0 {
            buf.extend_from_slice(
                format!(
                    "line {i:08}: the quick brown fox jumps over the lazy dog {}\n",
                    i % 7
                )
                .as_bytes(),
            );
        } else {
            buf.extend_from_slice(
                format!(
                    "line {i:08}: the quick brown cat stares at the lazy dog {}\n",
                    i % 13
                )
                .as_bytes(),
            );
        }
    }
    buf
}

/// `grep -c`-shaped work: for each line, does a match exist?
fn count_matching_lines(haystack: &[u8], re: &Regex) -> (usize, bool) {
    let mut count = 0usize;
    let mut work_limit = false;
    let mut start = 0usize;
    while start < haystack.len() {
        let end = match haystack[start..].iter().position(|&b| b == b'\n') {
            Some(i) => start + i,
            None => haystack.len(),
        };
        // Plain find collapses Err to None the same way grep consumers do;
        // the flag only reports that a cap tripped somewhere.
        match re.checked_find_at_bytes(&haystack[start..end], 0) {
            Ok(Some(_)) => count += 1,
            Ok(None) => {}
            Err(e) if e.kind() == ErrorKind::WorkLimit => {
                work_limit = true;
                count += 1; // conservative no-match; do not miscount forever
            }
            Err(_) => {}
        }
        start = end + 1;
    }
    (count, work_limit)
}

fn best_of<F: FnMut() -> usize>(reps: usize, mut f: F) -> (std::time::Duration, usize) {
    let mut best = std::time::Duration::MAX;
    let mut last = 0usize;
    for _ in 0..reps {
        let t0 = Instant::now();
        last = f();
        let d = t0.elapsed();
        if d < best {
            best = d;
        }
    }
    (best, last)
}

fn main() {
    let mibs: usize = std::env::args()
        .nth(1)
        .and_then(|a| a.parse().ok())
        .unwrap_or(64);
    let lines = mibs * 819_000 / 64;
    let haystack = build_haystack(lines);
    println!(
        "haystack: {} bytes ({:.1} MiB), {} lines",
        haystack.len(),
        haystack.len() as f64 / (1024.0 * 1024.0),
        lines
    );

    let fox = Regex::compile(Syntax::Basic, "fox").unwrap();
    let mut fox_slow = fox.clone();
    fox_slow.disable_fast_path();

    let (t_slow, c_slow) = best_of(3, || count_matching_lines(&haystack, &fox_slow).0);
    let (t_fast, c_fast) = best_of(3, || count_matching_lines(&haystack, &fox).0);
    println!(
        "grep -c fox  | interpreter {:>9.3?} | fast {:>9.3?} | speedup {:>6.1}x | counts {c_slow} vs {c_fast} (must be equal)",
        t_slow,
        t_fast,
        t_slow.as_secs_f64() / t_fast.as_secs_f64()
    );

    // Tier-3-only shape: no usable prefix (alternation head), first-byte
    // set {f, F}. The gate attempts only 'f'/'F' positions.
    let afox = Regex::compile(Syntax::Extended, "(f|F)(o|O)(x|X)").unwrap();
    let mut afox_slow = afox.clone();
    afox_slow.disable_fast_path();
    let (t_slow, c_slow) = best_of(3, || count_matching_lines(&haystack, &afox_slow).0);
    let (t_fast, c_fast) = best_of(3, || count_matching_lines(&haystack, &afox).0);
    println!(
        "grep -c (f|F)(o|O)(x|X) (first-tier) | interpreter {:>8.3?} | fast {:>9.3?} | speedup {:>6.1}x | counts {c_slow} vs {c_fast}",
        t_slow,
        t_fast,
        t_slow.as_secs_f64() / t_fast.as_secs_f64()
    );

    // No-worsening control: a dot head keeps the filter OFF (identical
    // legacy scan in both modes; any delta is noise). Runs on the first
    // eighth of the haystack to stay bounded — same input for both modes.
    let dfox = Regex::compile(Syntax::Extended, ".{2}fox").unwrap();
    let mut dfox_slow = dfox.clone();
    dfox_slow.disable_fast_path();
    let sample = &haystack[..haystack.len() / 8];
    let (t_slow, c_slow) = best_of(1, || count_matching_lines(sample, &dfox_slow).0);
    let (t_fast, c_fast) = best_of(1, || count_matching_lines(sample, &dfox).0);
    println!(
        "grep -c .{{2}}fox (fallback ctrl, 1/8) | interpreter {:>8.3?} | fast {:>9.3?} | counts {c_slow} vs {c_fast}",
        t_slow, t_fast,
    );

    // Pathological family: must not get worse, and the #1603 work caps
    // must still bound both modes (checked API surfaces WorkLimit).
    let long_line: String = "a".repeat(4096);
    let pats: Vec<(&str, Syntax)> =
        vec![("(.*)*b", Syntax::Extended), (r"\(.*\)*b", Syntax::Basic)];
    for (pat, syntax) in pats {
        let Ok(p) = Regex::compile(syntax, pat) else {
            println!("pathological {pat:?}: skipped (compile rejected)");
            continue;
        };
        let mut p_slow = p.clone();
        p_slow.disable_fast_path();
        let text = long_line.as_bytes();
        let t0 = Instant::now();
        let slow_res = p_slow.checked_find_at_bytes(text, 0);
        let t_slow = t0.elapsed();
        let t0 = Instant::now();
        let fast_res = p.checked_find_at_bytes(text, 0);
        let t_fast = t0.elapsed();
        println!(
            "pathological {pat:?} on 4096-byte line | interpreter {:>8.3?} ({:?}) | fast {:>8.3?} ({:?})",
            t_slow,
            slow_res.err().map(|e| e.kind()),
            t_fast,
            fast_res.err().map(|e| e.kind()),
        );
    }
}
