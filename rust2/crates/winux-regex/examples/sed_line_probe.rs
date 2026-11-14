//! Per-line selection probe for the sed `-n /re/p` hot path (WinuxCmd #1488
//! scan-perf lane, research only).
//!
//! Times exactly the loop sed runs for an address regex: one
//! `Regex::is_match_with` call per line of a buffer, NFA scratch reused.
//! With `--dfa` it runs the same selection through [`winux_regex::dfa::LineDfa`]
//! instead, so the two paths can be compared on identical input without the
//! sed I/O layer; both print the hit count so the two runs are checkable
//! against each other.
//!
//! Usage: cargo run --release -p winux-regex --example sed_line_probe -- \
//!            <pattern> <file> [--dfa] [--icase] [--ere] [--repeat N]

use std::time::Instant;

use winux_regex::{CompileOptions, Regex, Syntax};

fn main() {
    let mut args: Vec<String> = std::env::args().skip(1).collect();
    let pattern = args.remove(0);
    let path = args.remove(0);
    let dfa = args.iter().any(|a| a == "--dfa");
    let icase = args.iter().any(|a| a == "--icase");
    let ere = args.iter().any(|a| a == "--ere");
    let repeat: usize = args
        .iter()
        .position(|a| a == "--repeat")
        .and_then(|i| args.get(i + 1))
        .map(|n| n.parse().unwrap())
        .unwrap_or(3);

    let syntax = if ere { Syntax::Extended } else { Syntax::Basic };
    let opts = CompileOptions::sed(icase, false);
    let re = Regex::compile_with(syntax, pattern.as_bytes(), opts).expect("compiles");
    let bytes = std::fs::read(&path).expect("input");
    let lines: Vec<&[u8]> = bytes
        .split(|b| *b == b'\n')
        .filter(|l| !l.is_empty())
        .collect();

    let mut best = f64::INFINITY;
    let mut hits = 0usize;
    let mut ns_per_line = 0.0;
    let mut dfa_info = String::new();
    for _ in 0..repeat {
        let mut cache = re.new_cache();
        if dfa {
            let mut d =
                winux_regex::dfa::LineDfa::new(&[&re], winux_regex::dfa::LineMode::Contains, b'\n')
                    .expect("dfa");
            dfa_info = format!("{:?} requirement={:?}", d, d.input_requirement());
            let t = Instant::now();
            hits = lines.iter().filter(|l| d.is_match_line(l)).count();
            let dt = t.elapsed().as_secs_f64();
            best = best.min(dt);
            ns_per_line = dt * 1e9 / lines.len() as f64;
        } else {
            let t = Instant::now();
            hits = lines
                .iter()
                .filter(|l| re.is_match_with(&mut cache, l))
                .count();
            let dt = t.elapsed().as_secs_f64();
            best = best.min(dt);
            ns_per_line = dt * 1e9 / lines.len() as f64;
        }
    }
    println!(
        "pattern={pattern:?} lines={} bytes={} hits={} best={best:.4}s ns/line={:.1} MB/s={:.0} {}",
        lines.len(),
        bytes.len(),
        hits,
        ns_per_line,
        bytes.len() as f64 / best / (1024.0 * 1024.0),
        dfa_info
    );
}
