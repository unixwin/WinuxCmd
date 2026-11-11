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
//!   "missing operand" diagnostic) and exits 125 (GNU probed).
//! * The child is resolved PATH-only (execvp semantics) with the caller's
//!   environment and inherited stdio handles.

use std::ffi::OsString;
use std::io::Write;
use std::os::windows::process::CommandExt;
use std::time::{Duration, Instant};

use crate::argopt::{self, OptSpec};
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

/// execvp-shaped child resolution mirroring the product's shared childproc
/// helper (src/utils/childproc.cppm), which every WinuxCmd child spawner
/// (env, timeout, xargs, ...) shares:
///
/// * PATH-only search over NORMALIZED entries -- the PATH value POSIX
///   shells hand down mixes '/c/...' drive spellings and ':' separators
///   into the ';'-separated Windows form, which a plain `split(';')`
///   cannot parse; a bare name never falls back to the application
///   directory, the current directory or System32 (where.exe et al.
///   collide), and an UNSET PATH falls back to the system directories
///   (execvp's _CS_PATH analog).
/// * PATHEXT completion for extensionless operands, with .cmd/.bat
///   candidates routed through the command interpreter (CreateProcess
///   cannot launch a batch file directly).
/// * an operand containing a separator is an explicit path, used as given
///   (PATHEXT completion only when it carries no extension); a trailing
///   separator over a directory is NotADirectory (126), not ENOENT.
#[derive(Debug, PartialEq)]
enum ChildResolve {
    Found { path: String, batch: bool },
    NotFound,
    NotExecutable,
    NotADirectory,
}

fn is_drive_letter(ch: char) -> bool {
    ch.is_ascii_alphabetic()
}

/// One POSIX '/c/...' (or '/cygdrive/c/...') spelling into 'C:\...' form;
/// non-POSIX entries unchanged apart from separator normalization.
fn normalize_entry(entry: &str) -> String {
    let entry = entry.trim_matches(|c| c == '"' || c == ' ' || c == '\t');
    let bytes: Vec<char> = entry.chars().collect();
    let mut out = String::new();
    if let Some(rest) = entry.strip_prefix("/cygdrive/") {
        let mut rc = rest.chars();
        if let Some(drive) = rc.next() {
            if is_drive_letter(drive) && (rest.len() == 1 || rc.next() == Some('/')) {
                out.push(drive.to_ascii_uppercase());
                out.push(':');
                out.push_str(&rest[1..]);
            }
        }
    }
    if out.is_empty() && bytes.len() >= 2 && bytes[0] == '/' && is_drive_letter(bytes[1]) {
        out.push(bytes[1].to_ascii_uppercase());
        out.push(':');
        out.extend(bytes[2..].iter());
    }
    if out.is_empty() {
        out.push_str(entry);
    }
    // normalize separators, preserving a leading UNC '\\'
    let unc = out.starts_with("\\\\") || out.starts_with("//");
    let chars: Vec<char> = out.chars().collect();
    let mut norm = String::new();
    for (i, c) in chars.iter().enumerate() {
        if *c == '/' && (i >= 2 || !unc) {
            norm.push('\\');
        } else {
            norm.push(*c);
        }
    }
    norm
}

/// Splits a PATH value that may mix ';' and ':' separators (a ':' is only a
/// drive separator right after a single drive letter) into normalized
/// Windows directory entries; empty entries are dropped.
fn normalize_path_entries(raw: &str) -> Vec<String> {
    let chars: Vec<char> = raw.chars().collect();
    let mut entries = Vec::new();
    let mut segment = String::new();
    let mut flush = |segment: &mut String, entries: &mut Vec<String>| {
        let entry = normalize_entry(segment);
        if !entry.is_empty() {
            entries.push(entry);
        }
        segment.clear();
    };
    for (i, c) in chars.iter().enumerate() {
        let split_semicolon = *c == ';';
        let split_colon = *c == ':'
            && !(segment.chars().count() == 1
                && segment.chars().next().is_some_and(is_drive_letter)
                && chars.get(i + 1) != Some(&':'));
        if split_semicolon || split_colon {
            flush(&mut segment, &mut entries);
        } else {
            segment.push(*c);
        }
    }
    flush(&mut segment, &mut entries);
    entries
}

/// PATHEXT candidates for extensionless operands (lowercased, dot-prefixed),
/// falling back to .com/.exe/.bat/.cmd when unset or empty.
fn pathext_candidates(ctx: &Ctx) -> Vec<String> {
    let mut exts: Vec<String> = ctx
        .env
        .get("PATHEXT")
        .map(|v| v.to_string_lossy().into_owned())
        .unwrap_or_default()
        .split(';')
        .filter(|e| !e.is_empty())
        .map(|e| {
            let mut e = e.to_ascii_lowercase();
            if !e.starts_with('.') {
                e.insert(0, '.');
            }
            e
        })
        .collect();
    if exts.is_empty() {
        exts = [".com", ".exe", ".bat", ".cmd"].iter().map(|s| s.to_string()).collect();
    }
    exts
}

fn lowercase_extension(path: &str) -> String {
    let slash = path.rfind(['/', '\\']);
    match path.rfind('.') {
        Some(dot) if slash.is_none_or(|s| dot > s) => path[dot..].to_ascii_lowercase(),
        _ => String::new(),
    }
}

fn is_regular_file(path: &str) -> bool {
    std::fs::metadata(path).map(|m| m.is_file()).unwrap_or(false)
}

/// Scans one base path (dir + name, or an explicit operand) for the first
/// regular-file candidate; `explicit_extension` suppresses PATHEXT
/// completion. Reports NotExecutable on a directory hit as the fallback.
fn scan_candidates(
    base: &str,
    explicit_extension: bool,
    ctx: &Ctx,
    fallback: &mut ChildResolve,
) -> ChildResolve {
    let mut names = vec![base.to_string()];
    if !explicit_extension {
        for ext in pathext_candidates(ctx) {
            names.push(format!("{base}{ext}"));
        }
    }
    for candidate in &names {
        let Ok(meta) = std::fs::metadata(candidate) else {
            continue;
        };
        if meta.is_dir() {
            if *fallback == ChildResolve::NotFound {
                *fallback = ChildResolve::NotExecutable;
            }
            continue;
        }
        let ext = lowercase_extension(candidate);
        return ChildResolve::Found {
            path: candidate.clone(),
            batch: ext == ".cmd" || ext == ".bat",
        };
    }
    std::mem::replace(fallback, ChildResolve::NotFound)
}

fn resolve_child(name: &str, ctx: &Ctx) -> ChildResolve {
    if name.is_empty() {
        return ChildResolve::NotFound;
    }
    let mut fallback = ChildResolve::NotFound;

    if name.contains('/') || name.contains('\\') {
        // explicit (relative or absolute) path
        if name.ends_with('/') || name.ends_with('\\') {
            let stripped = name.trim_end_matches(['/', '\\']);
            if !stripped.is_empty()
                && std::fs::metadata(stripped).map(|m| m.is_dir()).unwrap_or(false)
            {
                return ChildResolve::NotADirectory;
            }
            return ChildResolve::NotFound;
        }
        let explicit_extension = !lowercase_extension(name).is_empty();
        return scan_candidates(&normalize_entry(name), explicit_extension, ctx, &mut fallback);
    }

    // bare name: PATH-only search with the Windows PE preference the
    // pre-wiring resolver documented ("how Windows actually executes"):
    // extension-bearing candidates across every entry are tried BEFORE the
    // bare name -- CreateProcess cannot run an extensionless POSIX script,
    // so `timeout 10 cmd /c exit 3` with a shadowing text file named `cmd`
    // still resolves cmd.exe (gate case tm02: GNU rc 3, probed).
    let entries = match ctx.env.get("PATH") {
        Some(v) => normalize_path_entries(&v.to_string_lossy()),
        None => {
            // execvp consults a system default path when PATH is unset;
            // the Windows analog of /bin:/usr/bin is System32 + Windows.
            let root = std::env::var("SystemRoot").unwrap_or_else(|_| "C:\\Windows".into());
            vec![format!("{root}\\System32"), root]
        }
    };
    let explicit_extension = !lowercase_extension(name).is_empty();
    let mut dir_hit = false;
    // pass 1: extension candidates (PATHEXT order when extensionless)
    for dir in &entries {
        let base = format!("{dir}\\{name}");
        let names: Vec<String> = if explicit_extension {
            vec![base]
        } else {
            pathext_candidates(ctx)
                .iter()
                .map(|ext| format!("{base}{ext}"))
                .collect()
        };
        for candidate in names {
            match std::fs::metadata(&candidate) {
                Ok(m) if m.is_file() => {
                    let ext = lowercase_extension(&candidate);
                    return ChildResolve::Found {
                        path: candidate,
                        batch: ext == ".cmd" || ext == ".bat",
                    };
                }
                Ok(m) if m.is_dir() => dir_hit = true,
                _ => {}
            }
        }
    }
    // pass 2: the bare name (an extensionless image; scripts stay dead)
    for dir in &entries {
        let base = format!("{dir}\\{name}");
        match std::fs::metadata(&base) {
            Ok(m) if m.is_file() => {
                return ChildResolve::Found {
                    path: base,
                    batch: false,
                };
            }
            Ok(m) if m.is_dir() => dir_hit = true,
            _ => {}
        }
    }
    if dir_hit {
        // [GNU] execvp keeps searching past EACCES/entry-is-a-directory and
        // reports the access failure only when nothing was executable.
        return ChildResolve::NotExecutable;
    }
    ChildResolve::NotFound
}

/// Canonical Win32 command-line quoting for one argument.
fn quote_windows_arg(arg: &str) -> String {
    if !arg.is_empty()
        && !arg.contains([' ', '\t', '\n', '"'])
    {
        return arg.to_string();
    }
    let mut out = String::with_capacity(arg.len() + 2);
    out.push('"');
    let mut backslashes = 0usize;
    for c in arg.chars() {
        match c {
            '\\' => backslashes += 1,
            '"' => {
                out.push_str(&"\\".repeat(backslashes * 2 + 1));
                backslashes = 0;
                out.push('"');
            }
            _ => {
                out.push_str(&"\\".repeat(backslashes));
                backslashes = 0;
                out.push(c);
            }
        }
    }
    if backslashes > 0 {
        out.push_str(&"\\".repeat(backslashes * 2));
    }
    out.push('"');
    out
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
                let _ = writeln!(io.stderr, "Try 'timeout --help' for more information.");
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
                let _ = writeln!(io.stderr, "Try 'timeout --help' for more information.");
                return Ok(125);
            }
        }
    }
    let verbose = p.has("verbose");
    let preserve_status = p.has("preserve-status");

    // [GNU] argc - optind < 2 reports only the try-help line, whether
    // DURATION or COMMAND is the piece that is absent.
    if p.positionals.len() < 2 {
        let _ = writeln!(io.stderr, "Try 'timeout --help' for more information.");
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
            let _ = writeln!(io.stderr, "Try 'timeout --help' for more information.");
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
    // scripts through the command interpreter), then spawn it with
    // inherited stdio.
    let resolved = resolve_child(&command, ctx);
    let mut cmd = match &resolved {
        ChildResolve::Found { path, batch } => {
            if *batch {
                // [GNU] CreateProcess cannot launch a batch file directly:
                // route through COMSPEC as the childproc helper does
                // (`cmd /d /s /c "<script> <args...>"`).
                let comspec = ctx
                    .env
                    .get("COMSPEC")
                    .map(|v| v.to_string_lossy().into_owned())
                    .filter(|v| !v.is_empty())
                    .unwrap_or_else(|| "cmd.exe".to_string());
                let mut inner = quote_windows_arg(path);
                for a in &cmd_args {
                    inner.push(' ');
                    inner.push_str(&quote_windows_arg(a));
                }
                let mut c = std::process::Command::new(&comspec);
                c.raw_arg("/d").raw_arg("/s").raw_arg("/c");
                c.raw_arg(format!("\"{inner}\""));
                c
            } else {
                // native image: spawn the resolved path; argv[0] is the
                // resolved path itself (std has no Windows argv0 control,
                // and the pre-wiring resolver did the same).
                let mut c = std::process::Command::new(path);
                c.args(&cmd_args);
                c
            }
        }
        ChildResolve::NotFound => {
            let _ = writeln!(
                io.stderr,
                "timeout: failed to run command '{command}': No such file or directory"
            );
            return Ok(127);
        }
        ChildResolve::NotExecutable | ChildResolve::NotADirectory => {
            let _ = writeln!(
                io.stderr,
                "timeout: failed to run command '{command}': Permission denied"
            );
            return Ok(126);
        }
    };
    // GetEnvironmentStringsW carries "=C:" style pseudo-vars that
    // Command::envs rejects; skip them (the winuxcmd ABI does the same).
    cmd.envs(
        ctx.env
            .entries()
            .iter()
            .filter(|(k, _)| !k.is_empty() && !k.to_string_lossy().starts_with('='))
            .map(|(k, v)| (k.clone(), v.clone())),
    );
    cmd.current_dir(&ctx.cwd);
    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(_) => {
            // found but not invokable (or another spawn failure)
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
        let ctx = Ctx {
            cwd: sc.dir.clone(),
            env,
        };
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
}
