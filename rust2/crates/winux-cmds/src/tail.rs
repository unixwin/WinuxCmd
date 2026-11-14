//! `tail` — GNU coreutils tail.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-tail.sh, o-tail.txt;
//! follow mode: rust2/gate/wire/tail-follow.sh, tail-follow.out).
//!
//! Count argument grammar (tail.c's parse): a leading `+` means start at
//! record K (1-based; `+0` prints everything, probed), a leading `-` is
//! stripped (`-n -K` == `-n K` — unlike head, probed: `-n -0` prints
//! NOTHING), bare K means the last K records. Suffixes match head
//! (b 512, kB 1000, K 1024, MB/M 1024^2, ...). `-n -0` / `-c -0` print
//! nothing; without --follow a zero last-count exits 0 before opening
//! anything (tail.c main). Records are lines, or NUL-delimited chunks
//! with -z. Regular files are read from the end (seek + backward scan,
//! so `tail -f big.log` never reads the whole log); stdin and devices are
//! read whole. The header is written before each input (first header has
//! no leading blank, later headers are preceded by a blank line; `-`
//! prints `standard input`). Headers appear when more than one input is
//! read or -v is given; -q never.
//!
//! Errors (rc 1, later files still processed): `cannot open 'X' for
//! reading: <reason>`; a directory operand gets its header and dies at
//! the read with `error reading 'X': Is a directory` (Linux opens a
//! directory and fails the read; recovered from a metadata probe).
//!
//! Follow mode (`-f`, `--follow[=descriptor|name]`, `-F`, `--retry`,
//! `-s`, `--pid`, `--max-unchanged-stats`) lives in `tail/follow.rs`;
//! its module doc lists the GNU rules and the Windows mechanics.

use std::ffi::OsString;
use std::fs::File;
use std::io::{Read, Seek, SeekFrom, Write};

use crate::argopt::{self, OptSpec};
use crate::fsutil::{gnu_open_reason, quotef, resolve};
use crate::gnuhelp;
use crate::head::{parse_count, Count};
use winux_core::{CmdError, CmdResult, Ctx, Io};

mod follow;
mod sys;

use follow::{Errnum, FollowMode, FollowOpts, Headers, Spec};
use sys::StdinKind;

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('c'),
        long: "bytes",
        takes_value: true,
    },
    // GNU's --follow takes an OPTIONAL argument ([=name|=descriptor]) and
    // -f none: scan_follow() strips the `=VALUE` spelling before argopt
    // sees it, so a plain flag keeps `-f -n 1` / `--follow f` parsing
    // correct (getopt optional args never steal the next argv word)
    OptSpec {
        short: Some('f'),
        long: "follow",
        takes_value: false,
    },
    // -F has no long form (tail.c optstring "c:n:fFqs:vz0123456789")
    OptSpec {
        short: Some('F'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "retry",
        takes_value: false,
    },
    OptSpec {
        short: Some('n'),
        long: "lines",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "pid",
        takes_value: true,
    },
    OptSpec {
        short: Some('q'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "silent",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "sleep-interval",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "max-unchanged-stats",
        takes_value: true,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero-terminated",
        takes_value: false,
    },
];

/// tail's count grammar on top of head's `[+-]K[suffix]` parse: `-K`
/// folds to `K` (tail strips the minus, probed), `+K` starts at record K
/// with K == 0 meaning everything.
#[derive(Debug, PartialEq)]
enum TailCount {
    Last(u128),
    FromStart(u128),
}

fn parse_tail_count(text: &str) -> Option<TailCount> {
    if let Some(rest) = text.strip_prefix('+') {
        return match parse_count(rest)? {
            Count::First(k) => Some(TailCount::FromStart(k)),
            Count::Elide(_) => None,
        };
    }
    match parse_count(text)? {
        Count::First(k) => Some(TailCount::Last(k)),
        // tail.c strips the leading '-': `-K` == `K`
        Count::Elide(k) => Some(TailCount::Last(k)),
    }
}

/// Offset where the last `k` records of `data` start (tail.c file_lines
/// on one buffer): a trailing partial record counts as a record.
fn last_records_start(data: &[u8], k: u128, delim: u8) -> usize {
    let mut lines = k;
    if lines == 0 {
        return data.len();
    }
    if data.last().is_some_and(|&b| b != delim) {
        lines -= 1;
    }
    for i in (1..=data.len()).rev() {
        if data[i - 1] != delim {
            continue;
        }
        if lines == 0 {
            return i;
        }
        lines -= 1;
    }
    0
}

/// Emit the last `k` records (k == 0 prints nothing).
fn emit_last(
    input: &[u8],
    k: u128,
    delim: u8,
    bytes_mode: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    if k == 0 {
        return Ok(());
    }
    let start = if bytes_mode {
        input.len() - (k.min(input.len() as u128) as usize)
    } else {
        last_records_start(input, k, delim)
    };
    out.write_all(&input[start..])
}

/// Emit records from the `k`'th (1-based) onward; k <= 1 prints everything.
fn emit_from(
    input: &[u8],
    k: u128,
    delim: u8,
    bytes_mode: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    if k <= 1 {
        return out.write_all(input);
    }
    let bounds = crate::head::record_bounds(input, delim, bytes_mode);
    let count = bounds.len() as u128;
    if count < k - 1 {
        return Ok(()); // fewer records than the skip: nothing
    }
    let start = bounds[(k - 2) as usize];
    out.write_all(&input[start..])
}

/// A dump failure: reading the input or writing stdout.
enum DumpErr {
    Read(std::io::Error),
    Write(std::io::Error),
}

const CHUNK: usize = 64 * 1024;

/// Copy `f` from its current position to EOF; returns the bytes copied.
fn copy_to_eof(f: &mut File, out: &mut dyn Write) -> Result<u64, DumpErr> {
    let mut buf = vec![0u8; CHUNK];
    let mut total = 0u64;
    loop {
        let n = match f.read(&mut buf) {
            Ok(0) => return Ok(total),
            Ok(n) => n,
            Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
            Err(e) => return Err(DumpErr::Read(e)),
        };
        out.write_all(&buf[..n]).map_err(DumpErr::Write)?;
        total += n as u64;
    }
}

/// tail.c file_lines over a seekable file: scan backward in CHUNK
/// blocks for the start of the last `k` records.
fn file_last_lines_start(f: &mut File, len: u64, k: u128, delim: u8) -> std::io::Result<u64> {
    let mut lines = k;
    let mut pos = len;
    let mut buf = vec![0u8; CHUNK];
    let mut first = true;
    while pos > 0 {
        let start = pos.saturating_sub(CHUNK as u64);
        let n = (pos - start) as usize;
        f.seek(SeekFrom::Start(start))?;
        f.read_exact(&mut buf[..n])?;
        if first {
            first = false;
            if buf[n - 1] != delim {
                lines -= 1;
            }
        }
        for i in (1..=n).rev() {
            if buf[i - 1] != delim {
                continue;
            }
            if lines == 0 {
                return Ok(start + i as u64);
            }
            lines -= 1;
        }
        pos = start;
    }
    Ok(0)
}

/// Initial dump of a regular file; returns GNU's `read_pos` (the offset
/// following starts from).
fn dump_regular(
    f: &mut File,
    count: &TailCount,
    delim: u8,
    bytes_mode: bool,
    out: &mut dyn Write,
) -> Result<u64, DumpErr> {
    let len = f.seek(SeekFrom::End(0)).map_err(DumpErr::Read)?;
    match *count {
        TailCount::Last(0) => Ok(len),
        TailCount::Last(k) => {
            let start = if bytes_mode {
                len - (k.min(len as u128) as u64)
            } else {
                file_last_lines_start(f, len, k, delim).map_err(DumpErr::Read)?
            };
            f.seek(SeekFrom::Start(start)).map_err(DumpErr::Read)?;
            Ok(start + copy_to_eof(f, out)?)
        }
        TailCount::FromStart(k) if bytes_mode => {
            // tail.c lseeks N-1 bytes in (even past EOF: read_pos then
            // exceeds the size and the first follow check reports a
            // truncation, like GNU)
            let skip = k.saturating_sub(1).min(i64::MAX as u128) as u64;
            f.seek(SeekFrom::Start(skip)).map_err(DumpErr::Read)?;
            Ok(skip + copy_to_eof(f, out)?)
        }
        TailCount::FromStart(k) => {
            f.seek(SeekFrom::Start(0)).map_err(DumpErr::Read)?;
            let mut skip = k.saturating_sub(1);
            let mut pos = 0u64;
            let mut buf = vec![0u8; CHUNK];
            while skip > 0 {
                let n = match f.read(&mut buf) {
                    Ok(0) => return Ok(pos),
                    Ok(n) => n,
                    Err(e) if e.kind() == std::io::ErrorKind::Interrupted => continue,
                    Err(e) => return Err(DumpErr::Read(e)),
                };
                pos += n as u64;
                let mut rest = None;
                for (i, &b) in buf[..n].iter().enumerate() {
                    if b == delim {
                        skip -= 1;
                        if skip == 0 {
                            rest = Some(i + 1);
                            break;
                        }
                    }
                }
                if let Some(r) = rest {
                    out.write_all(&buf[r..n]).map_err(DumpErr::Write)?;
                }
            }
            Ok(pos + copy_to_eof(f, out)?)
        }
    }
}

/// Dump an in-memory input (stdin, character devices).
fn dump_bytes(
    bytes: &[u8],
    count: &TailCount,
    delim: u8,
    bytes_mode: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    match *count {
        TailCount::Last(k) => emit_last(bytes, k, delim, bytes_mode, out),
        TailCount::FromStart(k) => emit_from(bytes, k, delim, bytes_mode, out),
    }
}

/// Broken-pipe class write errors (ERROR_NO_DATA / ERROR_BROKEN_PIPE are
/// what Windows pipes report; tee.rs epipe_like).
pub(crate) fn epipe_like(e: &std::io::Error) -> bool {
    e.kind() == std::io::ErrorKind::BrokenPipe || matches!(e.raw_os_error(), Some(232) | Some(109))
}

/// A stdout write failure ends the command: a broken pipe is returned as
/// the IO error (the host maps it to GNU's SIGPIPE status 141), anything
/// else is GNU's `tail: write error` with status 1.
fn write_failure(io: &mut Io, e: std::io::Error) -> CmdResult {
    if epipe_like(&e) {
        return Err(CmdError::Io(e));
    }
    let _ = writeln!(
        io.stderr,
        "tail: write error: {}",
        crate::fsutil::reason(&e)
    );
    Ok(1)
}

/// tail.c's long option names (for resolving abbreviations in the
/// follow pre-scan) and whether each takes a value.
const LONGS: &[(&str, bool)] = &[
    ("bytes", true),
    ("follow", false),
    ("lines", true),
    ("max-unchanged-stats", true),
    ("pid", true),
    ("quiet", false),
    ("retry", false),
    ("silent", false),
    ("sleep-interval", true),
    ("verbose", false),
    ("zero-terminated", false),
    ("help", false),
    ("version", false),
];

fn resolve_long(name: &str) -> Option<(&'static str, bool)> {
    if let Some(&(n, v)) = LONGS.iter().find(|(n, _)| *n == name) {
        return Some((n, v));
    }
    let mut cands = LONGS
        .iter()
        .filter(|(n, _)| !name.is_empty() && n.starts_with(name));
    match (cands.next(), cands.next()) {
        (Some(&(n, v)), None) => Some((n, v)),
        _ => None,
    }
}

/// The follow selection in argv order (tail.c parse_options): every
/// -f/--follow ASSIGNS the mode (bare = descriptor), -F is
/// `--follow=name` plus `--retry`.
#[derive(Debug, Default, PartialEq)]
struct FollowScan {
    forever: bool,
    by_name: bool,
    retry_from_f: bool,
}

/// argmatch over {descriptor, name}: exact or unique-prefix match.
fn follow_argmatch(v: &str) -> Result<bool, String> {
    let d = "descriptor".starts_with(v);
    let n = "name".starts_with(v);
    match (v.is_empty(), d, n) {
        (false, true, false) => Ok(false),
        (false, false, true) => Ok(true),
        _ => Err(format!(
            "tail: {} argument \u{2018}{v}\u{2019} for \u{2018}--follow\u{2019}\nValid arguments are:\n  - \u{2018}descriptor\u{2019}\n  - \u{2018}name\u{2019}\nTry 'tail --help' for more information.",
            if d && n { "ambiguous" } else { "invalid" }
        )),
    }
}

/// Walk argv like getopt to record the follow selection, rewriting
/// `--follow=VALUE` (any abbreviation) to a bare `--follow` for argopt.
fn scan_follow(args: &[OsString]) -> Result<(FollowScan, Vec<OsString>), String> {
    let mut scan = FollowScan::default();
    let mut out = Vec::with_capacity(args.len());
    let mut i = 0;
    while i < args.len() {
        let s = args[i].to_string_lossy().into_owned();
        if s == "--" {
            out.extend(args[i..].iter().cloned());
            break;
        }
        if let Some(rest) = s.strip_prefix("--") {
            let (name, val) = match rest.split_once('=') {
                Some((n, v)) => (n, Some(v)),
                None => (rest, None),
            };
            match resolve_long(name) {
                Some(("follow", _)) => {
                    scan.forever = true;
                    scan.by_name = match val {
                        Some(v) => follow_argmatch(v)?,
                        None => false,
                    };
                    out.push(OsString::from("--follow"));
                    i += 1;
                    continue;
                }
                Some((_, true)) if val.is_none() => {
                    out.push(args[i].clone());
                    if let Some(v) = args.get(i + 1) {
                        out.push(v.clone());
                    }
                    i += 2;
                    continue;
                }
                _ => {}
            }
        } else if s.len() > 1 && s.starts_with('-') {
            let chars: Vec<char> = s.chars().skip(1).collect();
            let mut takes_next = false;
            for (ci, c) in chars.iter().enumerate() {
                match c {
                    'f' => {
                        scan.forever = true;
                        scan.by_name = false;
                    }
                    'F' => {
                        scan.forever = true;
                        scan.by_name = true;
                        scan.retry_from_f = true;
                    }
                    'c' | 'n' | 's' => {
                        takes_next = ci + 1 == chars.len();
                        break;
                    }
                    _ => {}
                }
            }
            out.push(args[i].clone());
            if takes_next {
                if let Some(v) = args.get(i + 1) {
                    out.push(v.clone());
                }
                i += 2;
            } else {
                i += 1;
            }
            continue;
        }
        out.push(args[i].clone());
        i += 1;
    }
    Ok((scan, out))
}

/// xdectoumax over [0, max] with no suffixes (--pid,
/// --max-unchanged-stats): optional leading blanks and '+', digits to
/// the end; a value beyond `max` is EOVERFLOW.
fn parse_dec_max(text: &str, max: u128) -> Result<u128, bool> {
    let t = text.trim_start_matches([' ', '\t', '\n', '\x0b', '\x0c', '\r']);
    let t = t.strip_prefix('+').unwrap_or(t);
    if t.is_empty() || !t.bytes().all(|b| b.is_ascii_digit()) {
        return Err(false);
    }
    let mut v: u128 = 0;
    for b in t.bytes() {
        v = match v
            .checked_mul(10)
            .and_then(|v| v.checked_add((b - b'0') as u128))
        {
            Some(v) if v <= u64::MAX as u128 => v,
            _ => return Err(true),
        };
    }
    if v > max {
        return Err(true);
    }
    Ok(v)
}

/// xstrtod with cl_strtod (C locale strtod, whole string): leading
/// blanks, sign, decimal or hex mantissa, exponent, inf/infinity, nan.
fn parse_seconds(text: &str) -> Option<f64> {
    let t = text.trim_start_matches([' ', '\t', '\n', '\x0b', '\x0c', '\r']);
    let (neg, body) = match t.as_bytes().first() {
        Some(b'-') => (true, &t[1..]),
        Some(b'+') => (false, &t[1..]),
        _ => (false, t),
    };
    let lower = body.to_ascii_lowercase();
    let v = if lower == "inf" || lower == "infinity" {
        f64::INFINITY
    } else if lower == "nan" || (lower.starts_with("nan(") && lower.ends_with(')')) {
        f64::NAN
    } else if let Some(hex) = lower.strip_prefix("0x") {
        parse_hex_float(hex)?
    } else {
        let ok = !body.is_empty()
            && body
                .bytes()
                .all(|b| b.is_ascii_digit() || matches!(b, b'.' | b'e' | b'E' | b'+' | b'-'))
            && body
                .bytes()
                .next()
                .is_some_and(|b| b.is_ascii_digit() || b == b'.');
        if !ok {
            return None;
        }
        body.parse::<f64>().ok()?
    };
    Some(if neg { -v } else { v })
}

fn parse_hex_float(h: &str) -> Option<f64> {
    let (mant, exp) = match h.split_once('p') {
        Some((m, e)) => (m, Some(e)),
        None => (h, None),
    };
    let (ip, fp) = match mant.split_once('.') {
        Some((a, b)) => (a, b),
        None => (mant, ""),
    };
    if ip.is_empty() && fp.is_empty() {
        return None;
    }
    let mut v = 0f64;
    for c in ip.chars() {
        v = v * 16.0 + c.to_digit(16)? as f64;
    }
    let mut scale = 1.0 / 16.0;
    for c in fp.chars() {
        v += c.to_digit(16)? as f64 * scale;
        scale /= 16.0;
    }
    if let Some(e) = exp {
        let e: i32 = e.parse().ok()?;
        v *= 2f64.powi(e);
    }
    Some(v)
}

// `--help` body verbatim from `env tail --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const TAIL_HELP_BODY: &str = r#"Usage: tail [OPTION]... [FILE]...
Print the last 10 lines of each FILE to standard output.
With more than one FILE, precede each with a header giving the file name.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -c, --bytes=[+]NUM       output the last NUM bytes; or use -c +NUM to
                             output starting with byte NUM of each file
  -f, --follow[={name|descriptor}]
                           output appended data as the file grows;
                             an absent option argument means 'descriptor'
  -F                       same as --follow=name --retry
  -n, --lines=[+]NUM       output the last NUM lines, instead of the last 10;
                             or use -n +NUM to skip NUM-1 lines at the start
      --max-unchanged-stats=N
                           with --follow=name, reopen a FILE which has not
                             changed size after N (default 5) iterations
                             to see if it has been unlinked or renamed
                             (this is the usual case of rotated log files);
                             with inotify, this option is rarely useful
      --pid=PID            with -f, terminate after process ID, PID dies
  -q, --quiet, --silent    never output headers giving file names
      --retry              keep trying to open a file if it is inaccessible
  -s, --sleep-interval=N   with -f, sleep for approximately N seconds
                             (default 1.0) between iterations;
                             with inotify and --pid=P, check process P at
                             least once every N seconds
  -v, --verbose            always output headers giving file names
  -z, --zero-terminated    line delimiter is NUL, not newline
      --help        display this help and exit
      --version     output version information and exit

NUM may have a multiplier suffix:
b 512, kB 1000, K 1024, MB 1000*1000, M 1024*1024,
GB 1000*1000*1000, G 1024*1024*1024, and so on for T, P, E, Z, Y, R, Q.
Binary prefixes can be used, too: KiB=K, MiB=M, and so on.

With --follow (-f), tail defaults to following the file descriptor, which
means that even if a tail'ed file is renamed, tail will continue to track
its end.  This default behavior is not desirable when you really want to
track the actual name of the file, not the file descriptor (e.g., log
rotation).  Use --follow=name in that case.  That causes tail to track the
named file in a way that accommodates renaming, removal and creation.

"#;

pub fn tail_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    tail_with(args, io, ctx, &sys::stdin_kind)
}

/// `tail_cmd` with the stdin-kind probe injected (unit tests pin it; the
/// product asks the process stdin handle — the one piece of state a
/// `dyn Read` cannot carry, same tier as tty.rs).
fn tail_with(
    args: &[OsString],
    io: &mut Io,
    ctx: &Ctx,
    stdin_probe: &dyn Fn() -> StdinKind,
) -> CmdResult {
    // obsolescent -NUM folds into --lines (tail -5 == tail -n 5), but an
    // argument consumed as an option value passes through untouched
    let mut norm: Vec<OsString> = Vec::with_capacity(args.len());
    let mut skip_next_value = false;
    for a in args {
        let s = a.to_string_lossy().into_owned();
        if skip_next_value {
            norm.push(a.clone());
            skip_next_value = false;
            continue;
        }
        let digits = |r: &str| !r.is_empty() && r.bytes().all(|b| b.is_ascii_digit());
        if s.len() > 1 && s.starts_with('-') && !s.starts_with("--") && digits(&s[1..]) {
            norm.push(OsString::from(format!("--lines={}", &s[1..])));
            continue;
        }
        // (-f/--follow take no detached value: `-f -s -1` keeps -1 as
        // the -s value, not an obsolescent count)
        if matches!(
            s.as_str(),
            "-c" | "-n"
                | "-s"
                | "--bytes"
                | "--lines"
                | "--pid"
                | "--sleep-interval"
                | "--max-unchanged-stats"
        ) {
            skip_next_value = true;
        } else if s.len() > 1 && s.starts_with('-') && !s.starts_with("--") {
            let last = s.chars().last();
            if matches!(last, Some('c') | Some('n') | Some('s')) {
                skip_next_value = true;
            }
        }
        norm.push(a.clone());
    }
    let (scan, norm) = match scan_follow(&norm) {
        Ok(v) => v,
        Err(msg) => {
            let _ = writeln!(io.stderr, "{msg}");
            return Ok(1);
        }
    };
    let p = match argopt::parse("tail", &norm, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                write!(
                    io.stdout,
                    "{}{}",
                    TAIL_HELP_BODY,
                    gnuhelp::footer("tail", "tail invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version(
                        "tail",
                        "Paul Rubin, David MacKenzie, Ian Lance Taylor,\nand Jim Meyering"
                    )
                )?;
                0
            }
        });
    }

    let bytes_mode = p.has("bytes");
    let mut count = TailCount::Last(10);
    if let Some(v) = p.value_of("bytes").or_else(|| p.value_of("lines")) {
        match parse_tail_count(v) {
            Some(c) => count = c,
            None => {
                let word = if bytes_mode { "bytes" } else { "lines" };
                let _ = writeln!(
                    io.stderr,
                    "tail: invalid number of {}: \u{2018}{}\u{2019}",
                    word, v
                );
                return Ok(1);
            }
        }
    }
    // the follow-tuning values, validated in argv order like
    // parse_options (the first bad one dies); the last occurrence wins
    let mut pid: u128 = 0;
    let mut sleep_secs = 1.0f64;
    let mut max_unchanged: u128 = 5;
    for (k, v) in &p.values {
        let q = |v: &str| format!("\u{2018}{v}\u{2019}");
        match k.as_str() {
            "pid" => match parse_dec_max(v, i32::MAX as u128) {
                Ok(n) => pid = n,
                Err(overflow) => {
                    let tail = if overflow {
                        ": Value too large for defined data type"
                    } else {
                        ""
                    };
                    let _ = writeln!(io.stderr, "tail: invalid PID: {}{tail}", q(v));
                    return Ok(1);
                }
            },
            "max-unchanged-stats" => match parse_dec_max(v, u64::MAX as u128) {
                Ok(n) => max_unchanged = n,
                Err(overflow) => {
                    let tail = if overflow {
                        ": Value too large for defined data type"
                    } else {
                        ""
                    };
                    let _ = writeln!(
                        io.stderr,
                        "tail: invalid maximum number of unchanged stats between opens: {}{tail}",
                        q(v)
                    );
                    return Ok(1);
                }
            },
            "sleep-interval" => match parse_seconds(v) {
                Some(s) if s >= 0.0 => sleep_secs = s,
                _ => {
                    let _ = writeln!(io.stderr, "tail: invalid number of seconds: {}", q(v));
                    return Ok(1);
                }
            },
            _ => {}
        }
    }
    let quiet = p.has("quiet") || p.has("silent");
    let verbose = p.has("verbose");
    let forever = scan.forever;
    let mode = if scan.by_name {
        FollowMode::Name
    } else {
        FollowMode::Descriptor
    };
    let mut retry = p.has("retry") || scan.retry_from_f;
    let delim: u8 = if p.has("zero-terminated") { 0 } else { b'\n' };

    // parse_options' closing warnings
    if retry {
        if !forever {
            retry = false;
            let _ = writeln!(
                io.stderr,
                "tail: warning: --retry ignored; --retry is useful only when following"
            );
        } else if mode == FollowMode::Descriptor {
            let _ = writeln!(
                io.stderr,
                "tail: warning: --retry only effective for the initial open"
            );
        }
    }
    if pid != 0 && !forever {
        let _ = writeln!(
            io.stderr,
            "tail: warning: PID ignored; --pid=PID is useful only when following"
        );
    }

    let files: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };
    let found_hyphen = files.iter().any(|f| f == "-");
    // when following by name there must be a name
    if found_hyphen && forever && mode == FollowMode::Name {
        let _ = writeln!(io.stderr, "tail: cannot follow '-' by name");
        return Ok(1);
    }
    let stdin_kind = if found_hyphen {
        stdin_probe()
    } else {
        StdinKind::Unknown
    };
    if forever && found_hyphen {
        let blocking_stdin = pid == 0
            && mode == FollowMode::Descriptor
            && files.len() == 1
            && stdin_kind != StdinKind::Disk;
        if !blocking_stdin && stdin_kind == StdinKind::Console {
            let _ = writeln!(
                io.stderr,
                "tail: warning: following standard input indefinitely is ineffective"
            );
        }
    }
    // don't read anything if nothing will ever be output
    if count == TailCount::Last(0) && !forever {
        return Ok(0);
    }
    let print_headers = (files.len() > 1 || verbose) && !quiet;
    let mut headers = Headers::new(print_headers, files.len());

    let mut ok = true;
    let mut specs: Vec<Spec> = Vec::new();
    for file in &files {
        let mut spec = Spec::new(file);
        if file == "-" {
            headers
                .write_initial(io.stdout, "standard input")
                .map_err(CmdError::Io)?;
            let mut buf = Vec::new();
            if io.stdin.read_to_end(&mut buf).is_err() {
                let _ = writeln!(
                    io.stderr,
                    "tail: error reading 'standard input': Is a directory"
                );
                ok = false;
                spec.ignore = !retry;
                spec.errnum = Errnum::Err("Is a directory".into());
                specs.push(spec);
                continue;
            }
            if let Err(e) = dump_bytes(&buf, &count, delim, bytes_mode, io.stdout) {
                return write_failure(io, e);
            }
            spec.stdin = Some(stdin_kind);
            if forever && stdin_kind == StdinKind::Disk {
                if let Some((f, pos, dir)) = sys::stdin_follow_target() {
                    spec.size = pos;
                    if let Ok(st) = sys::handle_stat(&f) {
                        spec.id = st.id;
                        spec.regular = st.regular;
                    }
                    spec.watch_dir = Some(dir);
                    spec.fd = Some(f);
                }
            }
            specs.push(spec);
            continue;
        }
        let path = resolve(ctx, file);
        spec.path = Some(path.clone());
        spec.symlink = std::fs::symlink_metadata(&path)
            .map(|m| m.file_type().is_symlink())
            .unwrap_or(false);
        let is_dir = || {
            std::fs::metadata(&path)
                .map(|m| m.is_dir())
                .unwrap_or(false)
        };
        match File::open(&path) {
            Ok(mut f) => {
                let st = sys::handle_stat(&f).ok();
                if st.is_some_and(|s| s.is_dir) {
                    // a directory: Linux opens it, prints the header and
                    // fails the read
                    headers
                        .write_initial(io.stdout, file)
                        .map_err(CmdError::Io)?;
                    dir_failure(io, &mut spec, file, forever, retry, mode);
                    ok = false;
                    specs.push(spec);
                    continue;
                }
                headers
                    .write_initial(io.stdout, file)
                    .map_err(CmdError::Io)?;
                let regular = st.is_some_and(|s| s.regular);
                let dumped = if regular {
                    dump_regular(&mut f, &count, delim, bytes_mode, io.stdout)
                } else {
                    let mut buf = Vec::new();
                    match f.read_to_end(&mut buf) {
                        Ok(_) => dump_bytes(&buf, &count, delim, bytes_mode, io.stdout)
                            .map(|()| buf.len() as u64)
                            .map_err(DumpErr::Write),
                        Err(e) => Err(DumpErr::Read(e)),
                    }
                };
                match dumped {
                    Ok(pos) => {
                        if forever {
                            spec.size = pos;
                            if let Some(st) = st {
                                spec.id = st.id;
                                spec.regular = st.regular;
                            }
                            spec.watch_dir = path.parent().map(|d| d.to_path_buf());
                            spec.fd = Some(f);
                        }
                    }
                    Err(DumpErr::Write(e)) => return write_failure(io, e),
                    Err(DumpErr::Read(e)) => {
                        let _ = writeln!(
                            io.stderr,
                            "tail: error reading '{file}': {}",
                            crate::fsutil::reason(&e)
                        );
                        ok = false;
                        spec.ignore = !retry;
                        spec.errnum = Errnum::Err(crate::fsutil::reason(&e).to_string());
                    }
                }
            }
            Err(e) => {
                if is_dir() {
                    headers
                        .write_initial(io.stdout, file)
                        .map_err(CmdError::Io)?;
                    dir_failure(io, &mut spec, file, forever, retry, mode);
                    ok = false;
                    specs.push(spec);
                    continue;
                }
                let reason = gnu_open_reason(ctx, file, &e, false);
                let _ = writeln!(
                    io.stderr,
                    "tail: cannot open '{file}' for reading: {reason}"
                );
                ok = false;
                spec.tailable = !retry;
                spec.ignore = !retry;
                spec.errnum = Errnum::Err(reason);
                spec.watch_dir = path.parent().map(|d| d.to_path_buf());
            }
        }
        specs.push(spec);
    }

    if forever && follow::viable(&mut specs) > 0 {
        if let Err(e) = io.stdout.flush() {
            return write_failure(io, e);
        }
        let opts = FollowOpts {
            mode,
            retry,
            sleep_secs,
            pid: if pid == 0 { None } else { Some(pid as u32) },
            max_unchanged: max_unchanged as u64,
        };
        return follow::follow(&mut specs, &opts, &mut headers, ok, io, ctx);
    }
    Ok(if ok { 0 } else { 1 })
}

/// tail_file's directory leg: the read error, then (following) the
/// untailable-type diagnostic and the give-up decision.
fn dir_failure(
    io: &mut Io,
    spec: &mut Spec,
    file: &str,
    forever: bool,
    retry: bool,
    mode: FollowMode,
) {
    let _ = writeln!(io.stderr, "tail: error reading '{file}': Is a directory");
    if forever {
        let give_up = !(retry && mode == FollowMode::Name);
        let _ = writeln!(
            io.stderr,
            "tail: {}: cannot follow end of this type of file{}",
            quotef(file),
            if give_up {
                "; giving up on this name"
            } else {
                ""
            }
        );
        spec.tailable = false;
        spec.errnum = Errnum::Untailable;
    }
    // tail_file: a failed input is given up unless --retry
    spec.ignore = !retry;
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("tail");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = tail_with(&owned, &mut io.split(), &ctx, &|| StdinKind::Pipe).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: tail [OPTION]... [FILE]...\n"));
        assert!(out.contains("  -f, --follow[={name|descriptor}]"));
        assert!(out.ends_with("info '(coreutils) tail invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("tail (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("and Jim Meyering.\n"));
    }

    #[test]
    fn parse_tail_count_shapes() {
        assert_eq!(parse_tail_count("3"), Some(TailCount::Last(3)));
        // tail strips the leading '-' (unlike head): -3 == 3, probed
        assert_eq!(parse_tail_count("-3"), Some(TailCount::Last(3)));
        assert_eq!(parse_tail_count("-0"), Some(TailCount::Last(0)));
        assert_eq!(parse_tail_count("+3"), Some(TailCount::FromStart(3)));
        assert_eq!(parse_tail_count("+0"), Some(TailCount::FromStart(0)));
        assert_eq!(parse_tail_count("1b"), Some(TailCount::Last(512)));
        assert_eq!(parse_tail_count("x"), None);
        assert_eq!(parse_tail_count("-"), None);
    }

    #[test]
    fn defaults_and_counts() {
        let twelve: String = (1..=12).map(|i| format!("{i}\n")).collect();
        let last10: String = (3..=12).map(|i| format!("{i}\n")).collect();
        assert_eq!(run(&[], &twelve).1, last10);
        assert_eq!(run(&["-n", "2"], "a\nb\nc\n").1, "b\nc\n");
        assert_eq!(run(&["-n", "-2"], "a\nb\nc\n").1, "b\nc\n");
        assert_eq!(run(&["-n", "+2"], "a\nb\nc\n").1, "b\nc\n");
        // probed quirks: -n -0 prints nothing, -n +0 prints everything
        assert_eq!(run(&["-n", "-0"], "a\nb\n").1, "");
        assert_eq!(run(&["-n", "+0"], "a\nb\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "+5"], "a\nb\nc\n").1, "");
        assert_eq!(run(&["-n", "99"], "a\nb\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "0"], "a\nb\n").1, "");
        assert!(run(&["-n", "x"], "a\n")
            .2
            .contains("invalid number of lines"));
        assert_eq!(run(&["-3"], "a\nb\nc\nd\n").1, "b\nc\nd\n");
        assert_eq!(run(&[], "abc").1, "abc");
    }

    #[test]
    fn byte_counts() {
        assert_eq!(run(&["-c", "2"], "abcde").1, "de");
        assert_eq!(run(&["-c", "-2"], "abcde").1, "de");
        assert_eq!(run(&["-c", "+2"], "abcde").1, "bcde");
        assert_eq!(run(&["-c", "+0"], "ab").1, "ab");
        assert_eq!(run(&["-c", "-0"], "ab").1, "");
        assert_eq!(run(&["-c", "+99"], "ab\n").1, "");
        assert!(run(&["-c", "x"], "abc")
            .2
            .contains("invalid number of bytes"));
    }

    #[test]
    fn zero_terminated() {
        assert_eq!(run(&["-z", "-n", "2"], "a\0b\0c\0").1, "b\0c\0");
        assert_eq!(run(&["-z", "-n", "+2"], "a\0b\0c\0").1, "b\0c\0");
    }

    #[test]
    fn file_headers() {
        let sc = ScratchCtx::new("tail-files");
        std::fs::write(sc.path("f1"), "1\n2\n3\n").unwrap();
        std::fs::write(sc.path("f2"), "4\n5\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["f1", "f2"].iter().map(Into::into).collect();
        let r = tail_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "==> f1 <==\n1\n2\n3\n\n==> f2 <==\n4\n5\n");
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-q", "f1", "f2"].iter().map(Into::into).collect();
        tail_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "1\n2\n3\n4\n5\n");
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-v", "f1"].iter().map(Into::into).collect();
        tail_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "==> f1 <==\n1\n2\n3\n");
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-v", "-"].iter().map(Into::into).collect();
        tail_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "==> standard input <==\n");
    }

    #[test]
    fn errors_continue_and_follow_subset() {
        let sc = ScratchCtx::new("tail-err");
        std::fs::write(sc.path("f2"), "b\n").unwrap();
        std::fs::create_dir(sc.path("adir")).unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["missing", "f2"].iter().map(Into::into).collect();
        let r = tail_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "==> f2 <==\nb\n");
        assert!(io
            .err()
            .starts_with("tail: cannot open 'missing' for reading:"));
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["adir"].iter().map(Into::into).collect();
        let r = tail_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io
            .err()
            .contains("tail: error reading 'adir': Is a directory"));
        // -f on a pipe stdin: POSIX ignores -f, dump and exit 0
        let (r, out, err) = run(&["-f", "-n", "1"], "a\nb\n");
        assert_eq!(r, 0);
        assert_eq!(out, "b\n");
        assert!(err.is_empty());
        assert_eq!(run(&["--bogus"], "x\n").0, 1);
    }

    fn run_in(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = tail_with(&owned, &mut io.split(), &ctx, &|| StdinKind::Pipe).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn partial_last_record_counts() {
        // regression: the whole-buffer path indexed bounds[-1] (panic)
        assert_eq!(run(&["-n", "1"], "a\nb").1, "b");
        assert_eq!(run(&["-n", "2"], "a\nb\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "2"], "a\nb").1, "a\nb");
        assert_eq!(run(&["-n", "1"], "abc").1, "abc");
        assert_eq!(last_records_start(b"a\nb\nc\n", 2, b'\n'), 2);
        assert_eq!(last_records_start(b"a\nb\nc", 2, b'\n'), 2);
        assert_eq!(last_records_start(b"\n\n", 1, b'\n'), 1);
    }

    #[test]
    fn regular_file_dump_seeks_from_the_end() {
        let sc = ScratchCtx::new("tail-seek");
        // > one 64 KiB chunk so the backward scan crosses a boundary
        let body: String = (0..20000).map(|i| format!("line{i}\n")).collect();
        std::fs::write(sc.path("big"), &body).unwrap();
        std::fs::write(sc.path("p"), "x\ny\nzz").unwrap();
        assert_eq!(run_in(&sc, &["-n", "2", "big"]).1, "line19998\nline19999\n");
        let tail3: String = body
            .lines()
            .skip(20000 - 9000)
            .map(|l| format!("{l}\n"))
            .collect();
        assert_eq!(run_in(&sc, &["-n", "9000", "big"]).1, tail3);
        assert_eq!(run_in(&sc, &["-c", "4", "big"]).1, "999\n");
        assert_eq!(
            run_in(&sc, &["-n", "+19999", "big"]).1,
            "line19998\nline19999\n"
        );
        assert_eq!(run_in(&sc, &["-n", "1", "p"]).1, "zz");
        assert_eq!(run_in(&sc, &["-n", "+2", "p"]).1, "y\nzz");
        assert_eq!(run_in(&sc, &["-c", "+3", "p"]).1, "y\nzz");
        assert_eq!(run_in(&sc, &["-c", "+99", "p"]).1, "");
        assert_eq!(run_in(&sc, &["-c", "99", "p"]).1, "x\ny\nzz");
    }

    #[test]
    fn follow_scan_order_and_rewrite() {
        let os = |v: &[&str]| v.iter().map(OsString::from).collect::<Vec<_>>();
        let (s, out) = scan_follow(&os(&["--follow=name", "f"])).unwrap();
        assert_eq!(
            s,
            FollowScan {
                forever: true,
                by_name: true,
                retry_from_f: false
            }
        );
        assert_eq!(out, os(&["--follow", "f"]));
        // a later -f re-assigns descriptor mode, -F's retry sticks
        let (s, _) = scan_follow(&os(&["-F", "-f"])).unwrap();
        assert!(s.forever && !s.by_name && s.retry_from_f);
        let (s, out) = scan_follow(&os(&["--fo=n", "-qF"])).unwrap();
        assert!(s.by_name);
        assert_eq!(out, os(&["--follow", "-qF"]));
        // option values are not options; `--` ends the scan
        let (s, _) = scan_follow(&os(&["-n", "-f", "-s", "-F"])).unwrap();
        assert!(!s.forever);
        let (s, _) = scan_follow(&os(&["--", "-f"])).unwrap();
        assert!(!s.forever);
        let (s, _) = scan_follow(&os(&["--follow", "name"])).unwrap();
        assert!(s.forever && !s.by_name);
        assert!(scan_follow(&os(&["--follow=x"])).unwrap_err().starts_with(
            "tail: invalid argument \u{2018}x\u{2019} for \u{2018}--follow\u{2019}\n"
        ));
        assert!(scan_follow(&os(&["--follow="]))
            .unwrap_err()
            .starts_with("tail: ambiguous argument"));
    }

    #[test]
    fn value_parsers() {
        assert_eq!(parse_seconds("0.1"), Some(0.1));
        assert_eq!(parse_seconds(" 0.5"), Some(0.5));
        assert_eq!(parse_seconds("1e-1"), Some(0.1));
        assert_eq!(parse_seconds("0x10"), Some(16.0));
        assert_eq!(parse_seconds("0x1.8p1"), Some(3.0));
        assert_eq!(parse_seconds("inf"), Some(f64::INFINITY));
        assert_eq!(parse_seconds(".5"), Some(0.5));
        assert!(parse_seconds("nan").unwrap().is_nan());
        assert_eq!(parse_seconds("-1"), Some(-1.0));
        for bad in ["", "x", "0.1x", "1 ", "e5", "1e"] {
            assert_eq!(parse_seconds(bad), None, "{bad:?}");
        }
        assert_eq!(parse_dec_max("5", 10), Ok(5));
        assert_eq!(parse_dec_max(" +5", 10), Ok(5));
        assert_eq!(parse_dec_max("-1", 10), Err(false));
        assert_eq!(parse_dec_max("x", 10), Err(false));
        assert_eq!(parse_dec_max("11", 10), Err(true));
        assert_eq!(
            parse_dec_max("99999999999999999999999", u64::MAX as u128),
            Err(true)
        );
    }

    #[test]
    fn follow_option_diagnostics() {
        let sc = ScratchCtx::new("tail-fopt");
        std::fs::write(sc.path("f"), "a\n").unwrap();
        let (r, _, e) = run_in(&sc, &["-f", "-s", "x", "f"]);
        assert_eq!(
            (r, e.as_str()),
            (1, "tail: invalid number of seconds: \u{2018}x\u{2019}\n")
        );
        let (r, _, e) = run_in(&sc, &["-f", "--pid=99999999999", "f"]);
        assert_eq!(r, 1);
        assert!(e.ends_with(": Value too large for defined data type\n"));
        let (r, _, e) = run_in(&sc, &["-F", "-"]);
        assert_eq!((r, e.as_str()), (1, "tail: cannot follow '-' by name\n"));
        let (r, o, e) = run_in(&sc, &["--retry", "f"]);
        assert_eq!((r, o.as_str()), (0, "a\n"));
        assert!(e.contains("--retry ignored"));
        let (r, _, e) = run_in(&sc, &["--pid=5", "f"]);
        assert_eq!(r, 0);
        assert!(e.contains("PID ignored"));
        // zero count, no follow: nothing is opened
        assert_eq!(
            run_in(&sc, &["-n", "0", "missing"]),
            (0, String::new(), String::new())
        );
    }

    const DEAD: &str = "--pid=2147483647";

    #[test]
    fn follow_with_dead_writer_terminates_like_gnu() {
        let sc = ScratchCtx::new("tail-dead");
        std::fs::write(sc.path("f"), "a\nb\n").unwrap();
        std::fs::write(sc.path("g"), "x\n").unwrap();
        std::fs::create_dir(sc.path("dd")).unwrap();
        assert_eq!(
            run_in(&sc, &["-f", DEAD, "f"]),
            (0, "a\nb\n".into(), String::new())
        );
        let (r, o, _) = run_in(&sc, &["-f", DEAD, "f", "g"]);
        assert_eq!((r, o.as_str()), (0, "==> f <==\na\nb\n\n==> g <==\nx\n"));
        let (r, _, e) = run_in(&sc, &["-f", "missing"]);
        assert_eq!(r, 1);
        assert!(e.ends_with("tail: no files remaining\n"));
        // the exit status follows GNU's inotify/polling choice (p1.sh)
        assert_eq!(run_in(&sc, &["-f", DEAD, "missing", "f"]).0, 1);
        assert_eq!(run_in(&sc, &["-F", DEAD, "missing", "f"]).0, 0);
        assert_eq!(run_in(&sc, &["-F", DEAD, "missing"]).0, 1);
        let (r, o, e) = run_in(&sc, &["-F", DEAD, "dd", "f"]);
        assert_eq!((r, o.as_str()), (0, "==> dd <==\n\n==> f <==\na\nb\n"));
        assert!(e.ends_with("tail: dd: cannot follow end of this type of file\n"));
        let (r, _, e) = run_in(&sc, &["-f", DEAD, "dd"]);
        assert_eq!(r, 1);
        assert!(e.contains("; giving up on this name\ntail: no files remaining\n"));
        // -c +N past EOF leaves the offset past the size: GNU reports a
        // truncation at the first follow check
        let (r, o, e) = run_in(&sc, &["-f", "-c", "+99", DEAD, "f"]);
        assert_eq!((r, o.as_str()), (0, "a\nb\n"));
        assert_eq!(e, "tail: f: file truncated\n");
    }

    #[cfg(windows)]
    #[test]
    fn follow_live_append_truncate_and_pid_exit() {
        let sc = ScratchCtx::new("tail-live");
        let f = sc.path("f");
        std::fs::write(&f, "a\n").unwrap();
        // the writer stand-in: a child that lives ~2 s
        let mut child = std::process::Command::new("cmd")
            .args(["/c", "ping -n 3 127.0.0.1 >nul"])
            .spawn()
            .unwrap();
        let pid = format!("--pid={}", child.id());
        let path = f.clone();
        let writer = std::thread::spawn(move || {
            use std::io::Write as _;
            std::thread::sleep(std::time::Duration::from_millis(400));
            let mut h = std::fs::OpenOptions::new()
                .append(true)
                .open(&path)
                .unwrap();
            h.write_all(b"b\n").unwrap();
            drop(h);
            std::thread::sleep(std::time::Duration::from_millis(400));
            std::fs::write(&path, "").unwrap();
            std::thread::sleep(std::time::Duration::from_millis(400));
            std::fs::write(&path, "new\n").unwrap();
        });
        let (r, o, e) = run_in(&sc, &["-f", "-s", "0.1", &pid, "f"]);
        writer.join().unwrap();
        let _ = child.wait();
        assert_eq!(r, 0);
        assert_eq!(o, "a\nb\nnew\n");
        assert_eq!(e, "tail: f: file truncated\n");
    }
}
