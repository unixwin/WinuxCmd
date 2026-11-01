// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
//! Executable ban-lint (DOCS/2.0-cmdlib-design.md §6): library sources must
//! not reference process-level IO/state APIs — the three #1487 defect
//! classes must be inexpressible in library code.
//!
//! Runs as an ordinary cargo test; the mechanical-replacement checklist
//! (design §5 step 4) requires it green for every command. The host demo
//! (`src/host/`) is exempt by design: it is the side that TRANSLATES process
//! state into an explicit context, so it must use these APIs.

use std::fs;
use std::path::{Path, PathBuf};

/// (token, reason). Keep in sync with the table in
/// DOCS/2.0-cmdlib-design.md §6 — one table, two enforcement points (this
/// test + the future CI grep).
const BANNED: &[(&str, &str)] = &[
    (
        "GetStdHandle",
        "#1487 pit 3: cached process std handles (src/utils/console.cppm:61)",
    ),
    (
        "SetStdHandle",
        "library code must not mutate host process stdio",
    ),
    ("GetCurrentDirectory", "process cwd read instead of ctx.cwd"),
    (
        "GetEnvironmentVariable",
        "process env read instead of ctx.env_block",
    ),
    (
        "SetEnvironmentVariable",
        "library code must not mutate host process env",
    ),
    ("std::env::", "Rust process-environment access"),
    (
        "std::io::stdout",
        "process stdout write instead of ctx.stdout",
    ),
    (
        "std::io::stderr",
        "process stderr write instead of ctx.stderr",
    ),
    ("std::process::exit", "exit() instead of a return code"),
    (
        "std::process::Command",
        "spawning processes is out of the pilot ABI scope",
    ),
    (
        "printf",
        "#1487 pit 1: CRT stdio (fd 0/1/2 bound at DLL load)",
    ),
    ("fprintf", "#1487 pit 1: CRT stdio"),
    ("fwrite", "#1487 pit 1: CRT stdio"),
    ("fread", "#1487 pit 1: CRT stdio"),
    ("fopen", "#1487 pit 1: CRT stdio"),
    (
        "std::cin",
        "#1487 pit 2: iostream objects bound at load time",
    ),
    (
        "std::cout",
        "#1487 pit 2: iostream objects bound at load time",
    ),
    (
        "std::cerr",
        "#1487 pit 2: iostream objects bound at load time",
    ),
];

/// Line comments are stripped before matching so that doc text may name a
/// banned API while explaining the ban (design §6). Block comments are not
/// stripped — banned tokens must not appear in them.
fn strip_line_comments(src: &str) -> String {
    src.lines()
        .map(|line| match line.find("//") {
            Some(idx) => &line[..idx],
            None => line,
        })
        .collect::<Vec<_>>()
        .join("\n")
}

fn is_exempt(path: &Path) -> bool {
    // src/host/ is the injection side and is allowed to use process APIs.
    path.components().any(|c| c.as_os_str() == "host")
}

#[test]
fn library_sources_reference_no_process_state() {
    let src_root = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("src");
    let mut scanned = 0usize;
    let mut violations: Vec<String> = Vec::new();

    let mut stack = vec![src_root.clone()];
    while let Some(dir) = stack.pop() {
        for entry in fs::read_dir(&dir).unwrap() {
            let path = entry.unwrap().path();
            if path.is_dir() {
                stack.push(path);
                continue;
            }
            if path.extension().and_then(|e| e.to_str()) != Some("rs") {
                continue;
            }
            if is_exempt(&path) {
                continue;
            }
            scanned += 1;
            let code = strip_line_comments(&fs::read_to_string(&path).unwrap());
            for (token, reason) in BANNED {
                for (lineno, line) in code.lines().enumerate() {
                    if line.contains(token) {
                        violations.push(format!(
                            "{}:{}: `{}` — {}",
                            path.display(),
                            lineno + 1,
                            token,
                            reason
                        ));
                    }
                }
            }
        }
    }

    assert!(
        scanned > 0,
        "lint scanned nothing under {}",
        src_root.display()
    );
    assert!(
        violations.is_empty(),
        "banned process-state tokens in library sources (design §6):\n{}",
        violations.join("\n")
    );
}
