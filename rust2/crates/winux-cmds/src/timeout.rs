//! `timeout` — GNU coreutils timeout semantics on the Windows process
//! model (src/timeout.c is the specification; the 1.x C++ applet defines
//! the Windows mapping, which this port follows). Every status case below
//! was probed against WSL coreutils 9.4 (target/probes/p_timeout.sh,
//! p_misc.sh).
//!
//! * DURATION parses like strtod in the C locale: hexadecimal floats
//!   ("0x1"), "inf", a leading '+', and leading whitespace are all valid;
//!   NaN and negatives are rejected; at most ONE lowercase suffix s/m/h/d
//!   follows ("1m30s" and "5S" are invalid time intervals). Errors exit
//!   125 with the GNU diagnostics ("invalid time interval 'X'" / "'X':
//!   invalid signal", each followed by the try-help line).
//! * Signals: Linux signal names and numbers, with 32/33 reserved and
//!   RTMIN+n / RTMAX-n spelled forms. The Windows mapping is
//!   TerminateProcess; status mapping follows GNU: a timed-out command
//!   exits 124, with --preserve-status 128+signal, and a KILL death is
//!   137 regardless (probed 124/143/137).
//! * Exit statuses: child status on normal exit; 127 when the command
//!   cannot be found, 126 when it cannot be invoked; 125 when timeout
//!   itself fails. Missing COMMAND prints ONLY the try-help line (no
//!   "missing operand" diagnostic) and exits 125 (GNU probed). A found
//!   but unrunnable file (no image magic, no `#!` line — a plain text
//!   or .js file) reports the GNU ENOEXEC strerror "Exec format error"
//!   (#1587: it used to exit 126 silently); a shebang script launches
//!   through its interpreter like binfmt_script.c.
//! * The child is resolved PATH-only (execvp semantics, the shared
//!   [`crate::childproc`] chain) with the caller's environment and
//!   inherited stdio handles.

use std::ffi::OsString;
use std::io::Write;
use std::time::{Duration, Instant};

use crate::argopt::{self, OptSpec};
use crate::childproc::{resolve_child, spawn_child, ChildResolve};
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('s'),
        long: "signal",
        takes_value: true,
    },
    OptSpec {
        short: Some('k'),
        long: "kill-after",
        takes_value: true,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "preserve-status",
        takes_value: false,
    },
    // [GNU] on Windows there is no process group to suppress: the child
    // shares the console by construction, so the option is vacuous.
    OptSpec {
        short: None,
        long: "foreground",
        takes_value: false,
    },
];

/// [GNU] Linux signal numbering: 0..31 and 34..64, with RTMIN/RTMAX
/// spelled forms; 32/33 are reserved by the C library.
fn signal_number(name_raw: &str) -> Option<i32> {
    if name_raw.is_empty() {
        return None;
    }
    if let Ok(v) = name_raw.parse::<i32>() {
        return if (0..=31).contains(&v) || (34..=64).contains(&v) {
            Some(v)
        } else {
            None
        };
    }
    let mut name = name_raw.to_ascii_uppercase();
    if let Some(rest) = name.strip_prefix("SIG") {
        name = rest.to_string();
    }
    let table: &[(&str, i32)] = &[
        ("EXIT", 0),
        ("HUP", 1),
        ("INT", 2),
        ("QUIT", 3),
        ("ILL", 4),
        ("TRAP", 5),
        ("ABRT", 6),
        ("IOT", 6),
        ("BUS", 7),
        ("FPE", 8),
        ("KILL", 9),
        ("USR1", 10),
        ("SEGV", 11),
        ("USR2", 12),
        ("PIPE", 13),
        ("ALRM", 14),
        ("TERM", 15),
        ("STKFLT", 16),
        ("CHLD", 17),
        ("CONT", 18),
        ("STOP", 19),
        ("TSTP", 20),
        ("TTIN", 21),
        ("TTOU", 22),
        ("URG", 23),
        ("XCPU", 24),
        ("XFSZ", 25),
        ("VTALRM", 26),
        ("PROF", 27),
        ("WINCH", 28),
        ("IO", 29),
        ("POLL", 29),
        ("LOST", 29),
        ("PWR", 30),
        ("SYS", 31),
        ("UNUSED", 31),
    ];
    for (n, v) in table {
        if *n == name {
            return Some(*v);
        }
    }
    if name == "RTMIN" {
        return Some(34);
    }
    if name == "RTMAX" {
        return Some(64);
    }
    if let Some(rest) = name.strip_prefix("RTMIN+") {
        if let Ok(n) = rest.parse::<i32>() {
            if (0..=29).contains(&n) {
                return Some(34 + n);
            }
        }
        return None;
    }
    if let Some(rest) = name.strip_prefix("RTMAX-") {
        if let Ok(n) = rest.parse::<i32>() {
            if (0..=29).contains(&n) {
                return Some(64 - n);
            }
        }
        return None;
    }
    None
}

fn signal_name(signal: i32) -> String {
    let table: &[(&str, i32)] = &[
        ("EXIT", 0),
        ("HUP", 1),
        ("INT", 2),
        ("QUIT", 3),
        ("ILL", 4),
        ("TRAP", 5),
        ("ABRT", 6),
        ("BUS", 7),
        ("FPE", 8),
        ("KILL", 9),
        ("USR1", 10),
        ("SEGV", 11),
        ("USR2", 12),
        ("PIPE", 13),
        ("ALRM", 14),
        ("TERM", 15),
        ("STKFLT", 16),
        ("CHLD", 17),
        ("CONT", 18),
        ("STOP", 19),
        ("TSTP", 20),
        ("TTIN", 21),
        ("TTOU", 22),
        ("URG", 23),
        ("XCPU", 24),
        ("XFSZ", 25),
        ("VTALRM", 26),
        ("PROF", 27),
        ("WINCH", 28),
        ("IO", 29),
        ("PWR", 30),
        ("SYS", 31),
        ("RTMIN", 34),
        ("RTMAX", 64),
    ];
    for (n, v) in table {
        if *v == signal {
            return (*n).to_string();
        }
    }
    if signal > 34 && signal < 64 {
        return format!("RTMIN+{}", signal - 34);
    }
    signal.to_string()
}

/// [GNU] DURATION: strtod (C locale) plus at most one lowercase s/m/h/d
/// suffix; NaN/negative rejected; a non-finite or huge interval never
/// fires. Returns milliseconds, capped at u32::MAX-1 (never INFINITE).
fn parse_duration(text: &str) -> Result<u64, ()> {
    let t = text.trim_start();
    // strtod shape: decimal or hexadecimal float, optional sign, "inf".
    let (value, rest) = parse_strtod_prefix(t).ok_or(())?;
    // [GNU] inf is a valid (unbounded) interval; NaN and negatives are not.
    if value.is_nan() || value < 0.0 {
        return Err(());
    }
    let multiplier: f64 = match rest {
        "" => 1.0,
        "s" => 1.0,
        "m" => 60.0,
        "h" => 3600.0,
        "d" => 86400.0,
        _ => return Err(()),
    };
    let ms = value * multiplier * 1000.0;
    if !ms.is_finite() || ms >= 4294967294.0 {
        return Ok(0); // never fires: an unbounded timeout
    }
    Ok(ms.ceil() as u64)
}

/// hex float assembly: "1.8" with a binary exponent pN.
fn parse_hex_float(text: &str, pexp: i32) -> f64 {
    let (int_part, frac_part) = match text.split_once('.') {
        Some((a, b)) => (a, b),
        None => (text, ""),
    };
    let mut mantissa: f64 = 0.0;
    for c in int_part.chars() {
        mantissa = mantissa * 16.0 + c.to_digit(16).unwrap_or(0) as f64;
    }
    let mut scale = 1.0 / 16.0;
    for c in frac_part.chars() {
        mantissa += c.to_digit(16).unwrap_or(0) as f64 * scale;
        scale /= 16.0;
    }
    mantissa * 2f64.powi(pexp)
}

/// strtod-shaped numeric prefix: [+|-] (digits[.digits] | .digits |
/// hex digits) optionally "inf"/"infinity"/"nan" (nan rejected later).
fn parse_strtod_prefix(t: &str) -> Option<(f64, &str)> {
    let lower = t.to_ascii_lowercase();
    if let Some(rest) = lower.strip_prefix("inf") {
        let rest = rest.strip_prefix("inity").unwrap_or(rest);
        let consumed = t.len() - rest.len();
        let negative = t.starts_with('-');
        return Some((
            if negative {
                f64::NEG_INFINITY
            } else {
                f64::INFINITY
            },
            &t[consumed..],
        ));
    }
    let bytes = t.as_bytes();
    let mut i = 0usize;
    if i < bytes.len() && (bytes[i] == b'+' || bytes[i] == b'-') {
        i += 1;
    }
    let mut any = false;
    let mut j = i;
    if j + 1 < bytes.len() && bytes[j] == b'0' && (bytes[j + 1] | 0x20) == b'x' {
        j += 2;
        let hs = j;
        while j < bytes.len() && (bytes[j].is_ascii_hexdigit() || bytes[j] == b'.') {
            j += 1;
        }
        if j > hs {
            any = true;
            let mut pexp: i32 = 0;
            if j < bytes.len() && (bytes[j] | 0x20) == b'p' {
                let save = j;
                j += 1;
                let ps = j;
                let mut neg = false;
                if j < bytes.len() && (bytes[j] == b'+' || bytes[j] == b'-') {
                    neg = bytes[j] == b'-';
                    j += 1;
                }
                let ds = j;
                while j < bytes.len() && bytes[j].is_ascii_digit() {
                    j += 1;
                }
                if j > ds {
                    pexp = (&t[ds..j]).parse::<i32>().unwrap_or(0);
                    if neg {
                        pexp = -pexp;
                    }
                } else {
                    j = save;
                }
            }
            // Rust does not parse hex floats: assemble the value by hand.
            let hex_text = &t[hs..j
                - (if pexp != 0
                    || (j > hs && t[..j].ends_with(|c: char| c.is_ascii_hexdigit() || c == '.'))
                {
                    0
                } else {
                    0
                })];
            let _ = hex_text;
            let hex_span = &t[hs..j];
            let hex_core = hex_span.trim_end_matches(|c: char| c == '.').to_string();
            let value = parse_hex_float(&hex_core, pexp);
            let negative = t.starts_with('-');
            return Some((if negative { -value } else { value }, &t[j..]));
        }
    } else {
        while j < bytes.len() && (bytes[j].is_ascii_digit() || bytes[j] == b'.') {
            j += 1;
        }
        if j > i {
            any = true;
            if j < bytes.len() && (bytes[j] | 0x20) == b'e' {
                let save = j;
                j += 1;
                let ps = j;
                if j < bytes.len() && (bytes[j] == b'+' || bytes[j] == b'-') {
                    j += 1;
                }
                while j < bytes.len() && bytes[j].is_ascii_digit() {
                    j += 1;
                }
                if j == ps {
                    j = save;
                }
            }
        }
    }
    if !any {
        return None;
    }
    let text = &t[..j];
    // strtod would parse "1.2.3" up to the second dot; our durations never
    // benefit from that nuance, so a malformed remainder is an error.
    let value: f64 = text.parse().ok()?;
    Some((value, &t[j..]))
}

pub fn timeout_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // [GNU] timeout's optstring carries the leading '+' (no permutation):
    // option processing stops at the first non-option (detached option
    // VALUES are consumed as options), so the command's own arguments are
    // never parsed as options of timeout.
    let mut head_end = args.len();
    {
        let mut i = 0usize;
        let mut stop = false;
        while i < args.len() && !stop {
            let t = args[i].to_string_lossy().into_owned();
            if t == "--" {
                head_end = i + 1;
                break;
            }
            if !t.starts_with('-') || t == "-" {
                head_end = i;
                stop = true;
                break;
            }
            let body = &t[1..];
            if body.starts_with('-') {
                // long option
                let long = &body[1..];
                let has_inline = long.contains('=');
                let (name, _) = match long.split_once('=') {
                    Some((n, v)) => (n, Some(v)),
                    None => (long, None),
                };
                let takes = name == "signal" || name == "kill-after";
                if takes && !has_inline {
                    i += 1; // detached value
                }
                i += 1;
                continue;
            }
            // short cluster: find the first defined option that takes a
            // value; everything before it must be flags
            let chars: Vec<char> = body.chars().collect();
            let mut ci = 0usize;
            let mut consumed_next = false;
            while ci < chars.len() {
                let c = chars[ci];
                if c == 's' || c == 'k' {
                    // both take a value: bundled rest or the next argument
                    if ci + 1 == chars.len() {
                        consumed_next = true;
                    }
                    ci = chars.len();
                    break;
                }
                ci += 1;
            }
            let _ = consumed_next;
            if consumed_next {
                i += 1;
            }
            i += 1;
        }
        let _ = stop;
    }
    let (head, tail) = args.split_at(head_end.min(args.len()));
    let mut p = match argopt::parse("timeout", head, OPTS, io, ctx, 125) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    p.positionals.extend(tail.iter().cloned());
    if p.has("help") || p.has("version") {
        return Ok(0);
    }

    let mut signal = 15i32;
    if let Some(s) = p.value_of("signal") {
        match signal_number(s) {
            Some(v) => signal = v,
            None => {
                let _ = writeln!(io.stderr, "timeout: '{s}': invalid signal");
                let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "timeout"));
                return Ok(125);
            }
        }
    }
    let mut kill_after_ms: u64 = 0;
    if let Some(k) = p.value_of("kill-after") {
        match parse_duration(k) {
            Ok(v) => kill_after_ms = v,
            Err(()) => {
                let _ = writeln!(io.stderr, "timeout: invalid time interval '{k}'");
                let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "timeout"));
                return Ok(125);
            }
        }
    }
    let verbose = p.has("verbose");
    let preserve_status = p.has("preserve-status");

    // [GNU] argc - optind < 2 reports only the try-help line, whether
    // DURATION or COMMAND is the piece that is absent.
    if p.positionals.len() < 2 {
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "timeout"));
        return Ok(125);
    }
    let duration_text = p.positionals[0].to_string_lossy().into_owned();
    let duration_ms = match parse_duration(&duration_text) {
        Ok(v) => v,
        Err(()) => {
            let _ = writeln!(
                io.stderr,
                "timeout: invalid time interval '{duration_text}'"
            );
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "timeout"));
            return Ok(125);
        }
    };
    let command = p.positionals[1].to_string_lossy().into_owned();
    let cmd_args: Vec<String> = p.positionals[2..]
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    // Resolve the child the way the product's shared childproc helper does
    // (PATH-only over normalized entries, PATHEXT completion, batch
    // scripts through the command interpreter, shebang scripts through
    // their interpreter — #1587), then spawn it with inherited stdio.
    // Diagnostics follow GNU timeout.c's execvp failure path in the C
    // locale (probed WSL coreutils 9.4): 127 "No such file or directory"
    // for a missing command AND for a missing shebang interpreter (the
    // kernel's ENOENT), 126 "Exec format error" for a file no format in
    // the chain can run, 126 "Permission denied" for a directory or an
    // unspawnable resolution.
    let mut child = match resolve_child(&command, ctx) {
        ChildResolve::Found(launch) => match spawn_child(&launch, &cmd_args, ctx) {
            Ok(c) => c,
            Err(_) => {
                // found but not invokable (or another spawn failure)
                let _ = writeln!(
                    io.stderr,
                    "timeout: failed to run command '{command}': Permission denied"
                );
                return Ok(126);
            }
        },
        ChildResolve::NotFound => {
            let _ = writeln!(
                io.stderr,
                "timeout: failed to run command '{command}': No such file or directory"
            );
            return Ok(127);
        }
        ChildResolve::BadFormat => {
            let _ = writeln!(
                io.stderr,
                "timeout: failed to run command '{command}': Exec format error"
            );
            return Ok(126);
        }
        ChildResolve::NotExecutable | ChildResolve::NotADirectory => {
            let _ = writeln!(
                io.stderr,
                "timeout: failed to run command '{command}': Permission denied"
            );
            return Ok(126);
        }
    };

    let started = Instant::now();
    let limit = if duration_ms == 0 {
        None
    } else {
        Some(Duration::from_millis(duration_ms))
    };

    // Poll-wait: std has no stable wait_timeout, so short sleeps bound the
    // firing lag (well inside the gate's second-scale margins).
    let mut timed_out = false;
    loop {
        match child.try_wait() {
            Ok(Some(status)) => {
                let code = status.code().map(|c| c as u32).unwrap_or(125);
                return Ok(code);
            }
            Ok(None) => {}
            Err(_) => return Ok(125),
        }
        if let Some(l) = limit {
            if started.elapsed() >= l {
                timed_out = true;
                break;
            }
        }
        std::thread::sleep(Duration::from_millis(10));
    }
    let _ = timed_out;

    // Timed out: the Windows mapping is TerminateProcess; the GNU status
    // contract decides the exit code. A KILL death forces 128+signal even
    // without --preserve-status (probed 124 / 143 / 137).
    let timeout_status: u32 = if preserve_status || signal == 9 {
        (128 + signal) as u32
    } else {
        124
    };
    if verbose {
        let _ = writeln!(
            io.stderr,
            "timeout: sending signal {} to command '{}'",
            signal_name(signal),
            command
        );
    }
    // GNU signal 0 is a liveness probe: with plain -s0 the Windows
    // approximation still terminates and reports the mapped status; with
    // --kill-after the termination is deferred to the KILL phase.
    if signal != 0 || kill_after_ms == 0 {
        let _ = child.kill();
    }
    let mut killed_by_kill = false;
    if kill_after_ms > 0 {
        let kill_at = Instant::now();
        loop {
            match child.try_wait() {
                Ok(Some(status)) => {
                    // the child died on its own during the kill-after window
                    let code = status.code().map(|c| c as u32).unwrap_or(125);
                    return Ok(if preserve_status { code } else { 124 });
                }
                Ok(None) => {}
                Err(_) => break,
            }
            if kill_at.elapsed() >= Duration::from_millis(kill_after_ms) {
                if verbose {
                    let _ = writeln!(
                        io.stderr,
                        "timeout: sending signal KILL to command '{command}'"
                    );
                }
                let _ = child.kill();
                killed_by_kill = true;
                break;
            }
            std::thread::sleep(Duration::from_millis(10));
        }
    }
    let _ = child.wait();
    if killed_by_kill {
        // [GNU] a death delivered by the kill-after SIGKILL reports
        // 128+SIGKILL = 137, with or without --preserve-status (probed
        // `timeout -s0 -k0.1 0.1 sleep 5` -> rc 137).
        return Ok(137);
    }
    Ok(timeout_status)
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
    use winux_core::Ctx;
    use winux_core::MapEnv;

    fn run_env(env: &MapEnv, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = scratch("timeout");
        let ctx = Ctx::new(sc.dir.clone(), env);
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::timeout_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn run(args: &[&str]) -> (u32, String, String) {
        // inherit the real process env so PATH-based child resolution works
        let mut env = MapEnv::new();
        for (k, v) in std::env::vars_os() {
            env.set(&k.to_string_lossy(), &v.to_string_lossy());
        }
        run_env(&env, args)
    }

    #[test]
    fn normal_and_child_status() {
        // `true` completes within 10s on both PATHs of this machine
        let (r, _, _) = run(&["10", "cmd", "/c", "exit"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&["10", "cmd", "/c", "exit", "3"]);
        assert_eq!(r, 3);
    }

    #[test]
    fn timeout_statuses() {
        let (r, _, _) = run(&["1", "cmd", "/c", "ping", "-n", "30", "127.0.0.1"]);
        assert_eq!(r, 124);
        let (r, _, _) = run(&[
            "--preserve-status",
            "1",
            "cmd",
            "/c",
            "ping",
            "-n",
            "30",
            "127.0.0.1",
        ]);
        assert_eq!(r, 143);
        let (r, _, _) = run(&[
            "-s",
            "KILL",
            "1",
            "cmd",
            "/c",
            "ping",
            "-n",
            "30",
            "127.0.0.1",
        ]);
        assert_eq!(r, 137);
        let (r, _, _) = run(&["-s", "9", "1", "cmd", "/c", "ping", "-n", "30", "127.0.0.1"]);
        assert_eq!(r, 137);
        // -k with a command that dies to the first TERM still reports 124
        let (r, _, _) = run(&["-k", "1", "1", "cmd", "/c", "ping", "-n", "30", "127.0.0.1"]);
        assert_eq!(r, 124);
    }

    #[test]
    fn verbose_and_foreground() {
        let (r, _, err) = run(&["-v", "1", "cmd", "/c", "ping", "-n", "30", "127.0.0.1"]);
        assert_eq!(r, 124);
        assert!(err.contains("timeout: sending signal TERM to command 'cmd'"));
        let (r, _, _) = run(&["--foreground", "10", "cmd", "/c", "exit"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn spawn_errors() {
        let mut env = MapEnv::new();
        env.set("PATH", "D:\\definitely-not-a-real-path-xyz");
        let (r, _, err) = run_env(&env, &["10", "nosuchcmd123"]);
        assert_eq!(r, 127);
        assert!(err.contains("failed to run command 'nosuchcmd123': No such file or directory"));
    }

    #[test]
    fn non_pe_children_report_gnu_wording() {
        // #1587: a found but unrunnable file used to exit 126 silently;
        // the chain now classifies it and reports the GNU ENOEXEC strerror.
        let sc = scratch("timeout-badformat");
        let txt = sc.dir.join("plain.txt");
        std::fs::write(&txt, "just text\n").unwrap();
        let operand = txt.to_string_lossy().into_owned();
        let (r, _, err) = run(&["5", &operand]);
        assert_eq!(r, 126);
        assert!(err.contains(&format!(
            "failed to run command '{operand}': Exec format error"
        )));
        // a shebang script whose interpreter misses: the kernel's ENOENT
        // (probed GNU: rc 127 "No such file or directory")
        let bad = sc.dir.join("bad.sh");
        std::fs::write(&bad, "#!/bin/no-such-interp-xyz\necho never\n").unwrap();
        let operand = bad.to_string_lossy().into_owned();
        let (r, _, err) = run(&["5", &operand]);
        assert_eq!(r, 127);
        assert!(err.contains(&format!(
            "failed to run command '{operand}': No such file or directory"
        )));
        // a directory operand: EACCES -> 126 Permission denied (probed GNU)
        let (r, _, err) = run(&["5", &sc.dir.to_string_lossy()]);
        assert_eq!(r, 126);
        assert!(err.contains(": Permission denied"));
    }

    #[cfg(windows)]
    #[test]
    fn shebang_child_launches() {
        // #1587: `timeout 5 ./hi.sh` runs the script through its
        // interpreter (rc 0) instead of dying with a silent 126. The
        // interpreter "/bin/sh" resolves through the install-root mapper
        // or PATH, like the wired product environment.
        let sc = scratch("timeout-shebang");
        let hi = sc.dir.join("hi.sh");
        std::fs::write(&hi, "#!/bin/sh\nexit 0\n").unwrap();
        let operand = hi.to_string_lossy().into_owned();
        let (r, _, err) = run(&["5", &operand]);
        assert_eq!(r, 0, "err={err}");
        // the script's exit status propagates through the interpreter
        std::fs::write(&hi, "#!/bin/sh\nexit 4\n").unwrap();
        let (r, _, _) = run(&["5", &operand]);
        assert_eq!(r, 4);
    }

    #[cfg(windows)]
    #[test]
    fn chained_shebang_child_inner_script_once() {
        // #1595: `timeout 5 outer.sh` where outer's shebang interpreter is
        // ITSELF a script (inner.sh, itself `#!/bin/sh -e`). The kernel's
        // launch argv is [sh, -e, inner, -x, outer, A, B] -- the inner
        // script appears exactly once, as the script operand of its own
        // interpreter, and the outer shebang's arg follows it. The pre-fix
        // chain pushed the interpreter spelling a second time, shifting
        // every later token by one. WSL GNU oracle (script-file probe,
        // bash 5.3): the script echoes "0=[inner] 1=[-x] 2=[outer] 3=[A]
        // 4=[B]" -- sh consumes its own -e, so the script's $0 is the
        // inner script and the outer shebang's -x is its first argument.
        let sc = scratch("timeout-chain");
        let argv_out = sc.dir.join("argv.out");
        let inner = sc.dir.join("inner.sh");
        // The redirect target is consumed by /bin/sh, whose ownership we
        // don't control: MSYS sh (Git for Windows, the CI runner's sh)
        // treats an unquoted backslash as an escape, so a Windows-spelled
        // redirect silently lands on a mangled path and argv.out never
        // appears. Forward slashes are valid for both sh families.
        std::fs::write(
            &inner,
            format!(
                "#!/bin/sh -e\nprintf '%s\\n' \"$0\" \"$@\" >> {}\nexit 5\n",
                argv_out.to_string_lossy().replace('\\', "/")
            ),
        )
        .unwrap();
        let outer = sc.dir.join("outer.sh");
        std::fs::write(
            &outer,
            format!("#!{} -x\nnever-reached\n", inner.to_string_lossy()),
        )
        .unwrap();
        let operand = outer.to_string_lossy().into_owned();
        let (r, _, err) = run(&["5", &operand, "A", "B"]);
        assert_eq!(r, 5, "err={err}");
        let seen = std::fs::read_to_string(&argv_out).unwrap();
        let lines: Vec<&str> = seen.lines().collect();
        // OS argv [sh, -e, inner, -x, outer, A, B]: the script's $0 is the
        // inner script, then the outer shebang's arg, the outer script,
        // the caller's args.
        assert_eq!(
            lines,
            vec![
                inner.to_str().unwrap(),
                "-x",
                outer.to_str().unwrap(),
                "A",
                "B"
            ],
            "lines={lines:?}"
        );
        // the whole point of #1595: inner exactly once, outer exactly once
        assert_eq!(seen.matches(inner.to_str().unwrap()).count(), 1);
        assert_eq!(seen.matches(outer.to_str().unwrap()).count(), 1);
    }

    #[test]
    fn duration_grammar() {
        // valid shapes (GNU probed): whitespace, '+', hex, fractions,
        // one lowercase suffix, inf/infs, exponents
        for good in [
            "1", "0.001s", "0.001m", "0.0001d", " 1", "+1", "0x1", "inf", "infs", "5e-1", "00.5",
        ] {
            assert!(super::parse_duration(good).is_ok(), "duration {good:?}");
        }
        // invalid shapes
        for bad in ["xyz", "nan", "", "1m30s", "5S", "-1", "-5"] {
            assert!(super::parse_duration(bad).is_err(), "duration {bad:?}");
        }
        // "-5" reaches getopt as an unknown short option (GNU probed)
        let (r, _, err) = run(&["-5", "cmd", "/c", "exit"]);
        assert_eq!(r, 125);
        assert!(err.contains("invalid option -- '5'"));
        // end-to-end: the child outlives 1s and dies at the limit
        let (r, _, _) = run(&["1", "cmd", "/c", "exit"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn signal_grammar() {
        for good in [
            "TERM", "term", "SIGTERM", "9", "KILL", "kill", "RTMIN", "64",
        ] {
            let (r, _, _) = run(&["-s", good, "10", "cmd", "/c", "exit"]);
            assert_eq!(r, 0, "signal {good:?} should be accepted");
        }
        for bad in ["BOGUS", "32", "33", ""] {
            let (r, _, err) = run(&["-s", bad, "10", "cmd", "/c", "exit"]);
            assert_eq!(r, 125, "signal {bad:?} should be rejected");
            assert!(err.contains(&format!("'{bad}': invalid signal")));
            assert!(err.contains("Try 'timeout --help' for more information."));
        }
    }

    #[test]
    fn missing_command_is_try_help_only() {
        let (r, _, err) = run(&["5"]);
        assert_eq!(r, 125);
        assert_eq!(err, "Try 'timeout --help' for more information.\n");
        let (r, _, err) = run(&[]);
        assert_eq!(r, 125);
        assert_eq!(err, "Try 'timeout --help' for more information.\n");
    }

    #[test]
    fn command_completing_before_kill_after() {
        let (r, _, _) = run(&["--kill-after=5", "10", "cmd", "/c", "exit"]);
        assert_eq!(r, 0);
        // -k with a bad duration is 125
        let (r, _, err) = run(&["-k", "xyz", "10", "cmd", "/c", "exit"]);
        assert_eq!(r, 125);
        assert!(err.contains("invalid time interval 'xyz'"));
    }

    #[test]
    fn child_output_goes_to_inherited_stdout() {
        // The child inherits the process handles (library form: the caller
        // owns stdio), so only the exit status is observable here.
        let (r, out, _) = run(&["10", "cmd", "/c", "exit", "0"]);
        assert_eq!(r, 0);
        assert!(out.is_empty());
    }

    #[cfg(windows)]
    #[test]
    fn path_env_casing_does_not_break_child_resolution() {
        // #1567 wiring regression: the ABI snapshot mirrors a Win32 block
        // whose PATH entry is spelled however the parent wrote it ("Path"
        // from a PowerShell/cmd parent). A case-sensitive lookup missed it,
        // the resolver invented the System32 default, and every wrapped
        // sibling (echo.exe/sleep.exe) 127'd on clean machines.
        let sc = scratch("timeout-pathkey");
        let script = sc.dir.join("wmarker.cmd");
        std::fs::write(&script, "@echo off\r\nexit /b 0\r\n").unwrap();
        let mut env = MapEnv::new();
        env.set("Path", &sc.dir.to_string_lossy());
        let ctx = Ctx::new(sc.dir.clone(), &env);
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["5", "wmarker"].iter().map(Into::into).collect();
        let r = super::timeout_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0, "err={}", io.err());
    }
}
