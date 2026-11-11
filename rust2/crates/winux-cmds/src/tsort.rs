//! `tsort` — GNU coreutils tsort semantics, following src/tsort.c
//! (coreutils 9.4) as the specification. Every case below was probed
//! byte-for-byte against WSL coreutils 9.4 (target/probes/p_tsort*.sh).
//!
//! * Input is whitespace-separated token pairs (node, dependency); an odd
//!   token count is fatal: `tsort: FILE: input contains an odd number of
//!   tokens` (the input name is "-" for stdin or a missing operand), rc 1.
//! * A self-pair registers the node but is not a loop; duplicate pairs are
//!   recorded once (record_relation dedupes identical relations silently).
//! * The ready set is a FIFO queue seeded in node order; when a node is
//!   emitted, its successors join the tail in the REVERSE of the order the
//!   relations were read (record_relation prepends upstream in GNU, so
//!   `m a` then `m z` emits m, z, a).
//! * A cycle is broken at the earliest-mentioned remaining node: GNU prints
//!   `tsort: FILE: input contains a loop:` plus one `tsort: MEMBER` line per
//!   cycle member on stderr, emits the break node, keeps going, and exits 1.
//! * Empty input prints nothing with rc 0. A second operand is a usage
//!   error before any input is read.

use std::collections::{BTreeMap, BTreeSet, VecDeque};
use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use crate::fsutil::resolve;
use winux_core::{CmdResult, Ctx, Io};

pub fn tsort_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // GNU tsort main() parses argv with getopt_long against an EMPTY option
    // table plus the virtual help/version entries (probed WSL coreutils
    // 9.4): `--` ends option processing and is never an operand (`tsort --
    // ` reads stdin rc 0; `tsort -- --` opens the file `--`), operands
    // permute after options (`tsort a --help` prints usage rc 0), an
    // unregistered short option gets `invalid option -- 'w'` + try-help
    // rc 1 (the 1.x C++ exe silently accepted -w; that divergence is not
    // inherited). usage_status is 1 (tsort.c: usage(EXIT_TSOPT_FAILURE)).
    let p = match argopt::parse("tsort", args, &[], io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    // --help/--version: the branded text is the product dispatcher's job;
    // the library answers with GNU's success status.
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    let args = &p.positionals;
    // Operands: at most one (the "-" operand names standard input, exactly
    // like omitting the operand).
    if args.len() > 1 {
        let _ = writeln!(
            io.stderr,
            "tsort: extra operand '{}'",
            args[1].to_string_lossy()
        );
        let _ = writeln!(io.stderr, "Try 'tsort --help' for more information.");
        return Ok(1);
    }
    let operand = args.first().map(|a| a.to_string_lossy().into_owned());
    let input_name = operand.clone().unwrap_or_else(|| "-".to_string());

    let bytes = match operand.as_deref() {
        None | Some("-") => {
            let mut buf = Vec::new();
            if io.stdin.read_to_end(&mut buf).is_err() {
                let _ = writeln!(io.stderr, "tsort: {input_name}: read error");
                return Ok(1);
            }
            buf
        }
        Some(name) => {
            let path = resolve(ctx, name);
            match std::fs::metadata(&path) {
                Ok(m) if m.is_dir() => {
                    let _ = writeln!(io.stderr, "tsort: {name}: read error: Is a directory");
                    return Ok(1);
                }
                _ => {}
            }
            match std::fs::read(&path) {
                Ok(buf) => buf,
                Err(_) => {
                    // GNU: error (EXIT_FAILURE, errno, "%s", quotef (file));
                    // the reason text mirrors the three errno shapes tsort
                    // can hit through its open path on this platform.
                    let reason = if path.exists() {
                        "Permission denied"
                    } else {
                        "No such file or directory"
                    };
                    let _ = writeln!(io.stderr, "tsort: {name}: {reason}");
                    return Ok(1);
                }
            }
        }
    };

    // Whitespace-separated tokens; the count must be even.
    let text = String::from_utf8_lossy(&bytes);
    let tokens: Vec<&str> = text.split_whitespace().collect();
    if tokens.len() % 2 != 0 {
        let _ = writeln!(
            io.stderr,
            "tsort: {input_name}: input contains an odd number of tokens"
        );
        return Ok(1);
    }

    // Node registry in first-mention order (GNU's tree walk order); an
    // inserted-in-mention-order index drives both the zero scan and the
    // loop-break choice. GNU seeds scan_zeros over the sorted tree, so
    // use a BTreeSet keyed by node name for the seed pass.
    let mut nodes: BTreeSet<String> = BTreeSet::new();
    // successor list per node, in RECORDING order (appended); emission
    // walks it backwards, which is GNU's prepend successor-list order.
    let mut graph: BTreeMap<String, Vec<String>> = BTreeMap::new();
    let mut seen_pairs: BTreeSet<(String, String)> = BTreeSet::new();

    for pair in tokens.chunks(2) {
        let node = pair[0].to_string();
        let dep = pair[1].to_string();
        if node == dep {
            // record_relation ignores identical members but registers the node.
            nodes.insert(node);
            continue;
        }
        if !seen_pairs.insert((node.clone(), dep.clone())) {
            continue;
        }
        graph.entry(node.clone()).or_default().push(dep.clone());
        nodes.insert(node);
        nodes.insert(dep);
    }

    let mut remaining: BTreeMap<String, i64> = BTreeMap::new();
    for n in &nodes {
        remaining.insert(n.clone(), 0);
    }
    for deps in graph.values() {
        for d in deps {
            *remaining.get_mut(d).unwrap() += 1;
        }
    }

    // Seed the FIFO in node (tree-walk) order.
    let mut ready: VecDeque<String> = nodes
        .iter()
        .filter(|n| remaining[*n] == 0)
        .cloned()
        .collect();

    let mut emitted = 0usize;
    let mut had_loop = false;
    while emitted < nodes.len() {
        if ready.is_empty() {
            // Break the cycle at the earliest-mentioned remaining node.
            had_loop = true;
            let start = nodes
                .iter()
                .find(|n| remaining[*n] > 0)
                .cloned()
                .unwrap_or_default();
            // DFS for the stderr cycle report, following first-recorded
            // successors until the start is reached again.
            let mut loop_members: Vec<String> = vec![start.clone()];
            let mut visited: BTreeSet<String> = BTreeSet::new();
            visited.insert(start.clone());
            let mut current = start.clone();
            loop {
                let Some(deps) = graph.get(&current) else {
                    break;
                };
                let mut advanced = false;
                for dep in deps {
                    if *dep == start {
                        current = start.clone();
                        advanced = true;
                        break;
                    }
                    if nodes.contains(dep) && !visited.contains(dep) {
                        visited.insert(dep.clone());
                        loop_members.push(dep.clone());
                        current = dep.clone();
                        advanced = true;
                        break;
                    }
                }
                if !advanced || current == start {
                    break;
                }
            }
            let _ = writeln!(io.stderr, "tsort: {input_name}: input contains a loop:");
            for member in &loop_members {
                let _ = writeln!(io.stderr, "tsort: {member}");
            }
            ready.push_back(start.clone());
            remaining.insert(start, 0);
        }
        let node = ready.pop_front().unwrap();
        writeln!(io.stdout, "{node}")?;
        emitted += 1;
        remaining.insert(node.clone(), -1);
        if let Some(deps) = graph.get(&node) {
            for dep in deps.iter().rev() {
                let e = remaining.get_mut(dep).unwrap();
                *e -= 1;
                if *e == 0 {
                    ready.push_back(dep.clone());
                }
            }
        }
    }

    Ok(if had_loop { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use std::sync::atomic::{AtomicU64, Ordering};

    static SEQ: AtomicU64 = AtomicU64::new(0);

    fn scratch(tag: &str) -> winux_core::test_support::ScratchCtx {
        let n = SEQ.fetch_add(1, Ordering::SeqCst);
        winux_core::test_support::ScratchCtx::new(&format!("{}-{}", tag, n))
    }
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let sc = scratch("tsort");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::tsort_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn chain_and_empty() {
        let (r, out, _) = run(&[], "a b\nb c\n");
        assert_eq!((r, out.as_str()), (0, "a\nb\nc\n"));
        let (r, out, _) = run(&[], "");
        assert_eq!((r, out.as_str()), (0, ""));
    }

    #[test]
    fn duplicate_edges_and_self_pairs() {
        let (r, out, _) = run(&[], "a b\na b\n");
        assert_eq!((r, out.as_str()), (0, "a\nb\n"));
        // a a registers the node, is not a loop
        let (r, out, _) = run(&[], "a a\n");
        assert_eq!((r, out.as_str()), (0, "a\n"));
        let (r, out, _) = run(&[], "a b\na b\nb c\n");
        assert_eq!((r, out.as_str()), (0, "a\nb\nc\n"));
    }

    #[test]
    fn cycles() {
        let (r, out, err) = run(&[], "a b\nb a\n");
        assert_eq!(r, 1);
        assert_eq!(out, "a\nb\n");
        assert!(err.contains("input contains a loop:"));
        assert!(err.contains("tsort: a"));
        assert!(err.contains("tsort: b"));
        let (r, out, _) = run(&[], "a b\nb c\nc a\n");
        assert_eq!(r, 1);
        assert_eq!(out, "a\nb\nc\n");
        // two independent loops report twice
        let (r, out, err) = run(&[], "a b\nb a\nc d\nd c\n");
        assert_eq!(r, 1);
        assert_eq!(out, "a\nb\nc\nd\n");
        assert_eq!(err.matches("input contains a loop:").count(), 2);
    }

    #[test]
    fn odd_tokens() {
        let (r, _, err) = run(&[], "a b c\n");
        assert_eq!(r, 1);
        assert!(err.contains("tsort: -: input contains an odd number of tokens"));
        let (r, _, err) = run(&[], "a\n");
        assert_eq!(r, 1);
        assert!(err.contains("odd number of tokens"));
    }

    #[test]
    fn fifo_order_and_successor_prepend() {
        // GNU: ready queue is FIFO seeded in node order
        let (r, out, _) = run(&[], "a b\nc d\n");
        assert_eq!((r, out.as_str()), (0, "a\nc\nb\nd\n"));
        // successors join in reverse recording order: m a then m z -> m z a
        let (r, out, _) = run(&[], "m a\nm z\n");
        assert_eq!((r, out.as_str()), (0, "m\nz\na\n"));
        // deeper: r s / r t / s u / t v -> r t s v u
        let (r, out, _) = run(&[], "r s\nr t\ns u\nt v\n");
        assert_eq!((r, out.as_str()), (0, "r\nt\ns\nv\nu\n"));
    }

    #[test]
    fn loop_break_drains_before_cycle() {
        // z w / a b / b c / c a: z and w drain first, then the a-b-c loop
        let (r, out, _) = run(&[], "z w\na b\nb c\nc a\n");
        assert_eq!(r, 1);
        assert_eq!(out, "z\nw\na\nb\nc\n");
        // same with the tail written before the loop in the input
        let (r, out, _) = run(&[], "x y\ny x\nm z\n");
        assert_eq!(r, 1);
        assert_eq!(out, "m\nz\nx\ny\n");
    }

    #[test]
    fn operands_and_files() {
        // extra operand is a usage error before reading anything
        let (r, _, err) = run(&["f", "g"], "a b\n");
        assert_eq!(r, 1);
        assert!(err.contains("tsort: extra operand 'g'"));
        // dash is stdin
        let (r, out, _) = run(&["-"], "a b\n");
        assert_eq!((r, out.as_str()), (0, "a\nb\n"));
        // missing file
        let (r, _, err) = run(&["tsort-nosuch-file"], "");
        assert_eq!(r, 1);
        assert!(err.contains("No such file or directory"));
        // whitespace-separated pairs
        let (r, out, _) = run(&[], "  a   b \n\n c  d\n");
        assert_eq!((r, out.as_str()), (0, "a\nc\nb\nd\n"));
    }
}
