//! `head` — GNU coreutils head.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-head.sh, o-head.txt).
//!
//! Count argument grammar (head.c's `xdectoumax`-shaped parse): optional
//! leading `+` (same as bare K: head -n +2 == head -n 2, probed) or `-`
//! (elide-from-end: all but the last K records), digits, then an optional
//! multiplier suffix (b 512, kB 1000, K 1024, MB/M 1024^2, ... R/Q; the
//! probed `-n 1b` / `-c 1b` rows cover the b suffix). `-n -0` / `-c -0`
//! print everything (all but last 0, probed). A malformed count dies with
//! `invalid number of lines|bytes: '<arg>'` (curly quotes, rc 1).
//!
//! Records are lines, or NUL-delimited chunks with -z. Inputs are read
//! whole (the -K form needs the tail anyway); files are processed
//! sequentially with the header written before each (probed: first header
//! has no leading blank, later headers are preceded by a blank line;
//! `-` prints `standard input`). Headers appear when more than one input
//! is read or -v is given; -q never prints them.
//!
//! Errors (rc 1, later files still processed): a failed open reports
//! `cannot open 'X' for reading: <reason>` (straight quotes, probed); a
//! directory operand opens on POSIX and dies at the read, which Windows
//! recovers from a metadata probe (`error reading 'X': Is a directory`).

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use crate::fsutil::{gnu_open_reason, resolve};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('c'),
        long: "bytes",
        takes_value: true,
    },
    OptSpec {
        short: Some('n'),
        long: "lines",
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

/// The parsed `[+-]NUM[suffix]` count argument.
#[derive(Debug, PartialEq)]
pub(crate) enum Count {
    /// print the first K records
    First(u128),
    /// print all but the last K records
    Elide(u128),
}

/// GNU multiplier suffixes (coreutils xstrtol table): decimal `kB`-style
/// pairs and binary single letters; `b` is 512, `c` is 1.
fn suffix_value(rest: &str) -> Option<u128> {
    match rest {
        "b" => Some(512),
        "c" => Some(1),
        "KiB" | "K" | "k" => Some(1024),
        "MiB" | "M" => Some(1024u128.pow(2)),
        "GiB" | "G" => Some(1024u128.pow(3)),
        "TiB" | "T" => Some(1024u128.pow(4)),
        "PiB" | "P" => Some(1024u128.pow(5)),
        "EiB" | "E" => Some(1024u128.pow(6)),
        "ZiB" | "Z" => Some(1024u128.pow(7)),
        "YiB" | "Y" => Some(1024u128.pow(8)),
        "RiB" | "R" => Some(1024u128.pow(9)),
        "QiB" | "Q" => Some(1024u128.pow(10)),
        "kB" | "KB" => Some(1000),
        "MB" => Some(1000u128.pow(2)),
        "GB" => Some(1000u128.pow(3)),
        "TB" => Some(1000u128.pow(4)),
        "PB" => Some(1000u128.pow(5)),
        "EB" => Some(1000u128.pow(6)),
        "ZB" => Some(1000u128.pow(7)),
        "YB" => Some(1000u128.pow(8)),
        "RB" => Some(1000u128.pow(9)),
        "QB" => Some(1000u128.pow(10)),
        _ => None,
    }
}

/// Parse head's `[+-]NUM[SUFFIX]`. None on any malformed input (GNU
/// xstrtol failure shape; the caller supplies the lines/bytes wording).
pub(crate) fn parse_count(text: &str) -> Option<Count> {
    let (neg, rest) = match text.strip_prefix('-') {
        Some(r) => (true, r),
        None => (false, text.strip_prefix('+').unwrap_or(text)),
    };
    let digits_end = rest
        .find(|c: char| !c.is_ascii_digit())
        .unwrap_or(rest.len());
    if digits_end == 0 {
        return None; // no digits at all
    }
    let value: u128 = rest[..digits_end].parse().ok()?;
    let scaled = if digits_end == rest.len() {
        value
    } else {
        value.checked_mul(suffix_value(&rest[digits_end..])?)?
    };
    Some(if neg {
        Count::Elide(scaled)
    } else {
        Count::First(scaled)
    })
}

/// End offset of every record: lines end after each `delim` byte; byte
/// mode (-c) counts every byte as its own record.
pub(crate) fn record_bounds(input: &[u8], delim: u8, bytes_mode: bool) -> Vec<usize> {
    let mut bounds = Vec::new();
    if bytes_mode {
        bounds.extend(1..=input.len());
    } else {
        for (i, &b) in input.iter().enumerate() {
            if b == delim {
                bounds.push(i + 1);
            }
        }
    }
    bounds
}

/// Emit `all but the last k` records.
fn emit_elide(
    input: &[u8],
    k: u128,
    delim: u8,
    bytes_mode: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    if k == 0 {
        return out.write_all(input);
    }
    let bounds = record_bounds(input, delim, bytes_mode);
    let count = bounds.len() as u128;
    if count <= k {
        return Ok(()); // everything is elided
    }
    let end = bounds[(count - k - 1) as usize];
    out.write_all(&input[..end])
}

/// Emit the first `k` records. `k == 0` prints nothing.
fn emit_first(
    input: &[u8],
    k: u128,
    delim: u8,
    bytes_mode: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    if k == 0 {
        return Ok(());
    }
    let bounds = record_bounds(input, delim, bytes_mode);
    let count = bounds.len() as u128;
    if count < k {
        return out.write_all(input);
    }
    let end = bounds[(k - 1) as usize];
    out.write_all(&input[..end])
}

// `--help` body verbatim from `env head --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const HEAD_HELP_BODY: &str = r#"Usage: head [OPTION]... [FILE]...
Print the first 10 lines of each FILE to standard output.
With more than one FILE, precede each with a header giving the file name.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -c, --bytes=[-]NUM       print the first NUM bytes of each file;
                             with the leading '-', print all but the last
                             NUM bytes of each file
  -n, --lines=[-]NUM       print the first NUM lines instead of the first 10;
                             with the leading '-', print all but the last
                             NUM lines of each file
  -q, --quiet, --silent    never print headers giving file names
  -v, --verbose            always print headers giving file names
  -z, --zero-terminated    line delimiter is NUL, not newline
      --help        display this help and exit
      --version     output version information and exit

NUM may have a multiplier suffix:
b 512, kB 1000, K 1024, MB 1000*1000, M 1024*1024,
GB 1000*1000*1000, G 1024*1024*1024, and so on for T, P, E, Z, Y, R, Q.
Binary prefixes can be used, too: KiB=K, MiB=M, and so on.

"#;

pub fn head_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // obsolescent -NUM folds into --lines, but only for real operands: an
    // argument consumed as a value by a preceding -c/-n/--bytes/--lines
    // must pass through untouched (head -c -2 parses -2 as a count)
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
        if s == "-c" || s == "-n" || s == "--bytes" || s == "--lines" {
            skip_next_value = true;
        } else if s.len() > 1 && s.starts_with('-') && !s.starts_with("--") {
            let last = s.chars().last();
            if last == Some('c') || last == Some('n') {
                skip_next_value = true;
            }
        }
        norm.push(a.clone());
    }
    let p = match argopt::parse("head", &norm, OPTS, io, ctx, 1) {
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
                    HEAD_HELP_BODY,
                    gnuhelp::footer("head", "head invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("head", "David MacKenzie and Jim Meyering")
                )?;
                0
            }
        });
    }

    let bytes_mode = p.has("bytes");
    let (word, default_count) = if bytes_mode {
        ("bytes", Count::First(10))
    } else {
        ("lines", Count::First(10))
    };
    let mut count = default_count;
    if let Some(v) = p.value_of("bytes").or_else(|| p.value_of("lines")) {
        match parse_count(v) {
            Some(c) => count = c,
            None => {
                let _ = writeln!(
                    io.stderr,
                    "head: invalid number of {}: \u{2018}{}\u{2019}",
                    word, v
                );
                return Ok(1);
            }
        }
    }
    // probed head -v -n 1: an explicit -n keeps -v working; the count may
    // come from either option in any order (last wins, like GNU's globals)
    let quiet = p.has("quiet") || p.has("silent");
    let verbose = p.has("verbose");
    let delim: u8 = if p.has("zero-terminated") { 0 } else { b'\n' };

    let files: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };
    let print_headers = (files.len() > 1 || verbose) && !quiet;

    let mut first_header = true;
    let mut failed = false;
    for file in &files {
        let bytes = if file == "-" {
            let mut buf = Vec::new();
            match io.stdin.read_to_end(&mut buf) {
                Ok(_) => buf,
                Err(_) => {
                    let _ = writeln!(
                        io.stderr,
                        "head: error reading 'standard input': Is a directory"
                    );
                    failed = true;
                    continue;
                }
            }
        } else {
            match std::fs::File::open(resolve(ctx, file)) {
                Ok(mut f) => {
                    let mut buf = Vec::new();
                    match f.read_to_end(&mut buf) {
                        Ok(_) => buf,
                        Err(_) => {
                            // POSIX head opens the operand then reads it: a
                            // directory dies at the read (probed via stdin;
                            // Windows fails the open, so probe metadata)
                            let reason = if std::fs::metadata(resolve(ctx, file))
                                .map(|m| m.is_dir())
                                .unwrap_or(false)
                            {
                                "Is a directory".to_string()
                            } else {
                                "read error".to_string()
                            };
                            let _ = writeln!(io.stderr, "head: error reading '{file}': {reason}");
                            failed = true;
                            continue;
                        }
                    }
                }
                Err(e) => {
                    if std::fs::metadata(resolve(ctx, file))
                        .map(|m| m.is_dir())
                        .unwrap_or(false)
                    {
                        // POSIX opens the directory then dies at the read
                        let _ = writeln!(io.stderr, "head: error reading '{file}': Is a directory");
                        failed = true;
                        continue;
                    }
                    let reason = gnu_open_reason(ctx, file, &e, false);
                    let _ = writeln!(
                        io.stderr,
                        "head: cannot open '{file}' for reading: {reason}"
                    );
                    failed = true;
                    continue;
                }
            }
        };
        if print_headers {
            if first_header {
                first_header = false;
            } else {
                let _ = write!(io.stdout, "\n");
            }
            let name = if file == "-" {
                "standard input"
            } else {
                file.as_str()
            };
            let _ = writeln!(io.stdout, "==> {name} <==");
        }
        let r = match count {
            Count::First(k) => emit_first(&bytes, k, delim, bytes_mode, io.stdout),
            Count::Elide(k) => emit_elide(&bytes, k, delim, bytes_mode, io.stdout),
        };
        if r.is_err() {
            let _ = writeln!(io.stderr, "head: write error");
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("head");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: head [OPTION]... [FILE]...\n"));
        assert!(out.contains("  -c, --bytes=[-]NUM"));
        assert!(out.ends_with("info '(coreutils) head invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("head (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie and Jim Meyering.\n"));
    }

    #[test]
    fn parse_count_shapes() {
        assert_eq!(parse_count("3"), Some(Count::First(3)));
        assert_eq!(parse_count("+3"), Some(Count::First(3)));
        assert_eq!(parse_count("-3"), Some(Count::Elide(3)));
        assert_eq!(parse_count("0"), Some(Count::First(0)));
        assert_eq!(parse_count("1b"), Some(Count::First(512)));
        assert_eq!(parse_count("-2K"), Some(Count::Elide(2048)));
        assert_eq!(parse_count("x"), None);
        assert_eq!(parse_count("1x"), None);
        assert_eq!(parse_count("-"), None);
        assert_eq!(parse_count(""), None);
        assert_eq!(parse_count("-1b2"), None);
    }

    #[test]
    fn defaults_and_counts() {
        let twelve: String = (1..=12).map(|i| format!("{i}\n")).collect();
        let (r, out, _) = run(&[], &twelve);
        assert_eq!(r, 0);
        let first10: String = (1..=10).map(|i| format!("{i}\n")).collect();
        assert_eq!(out, first10);

        assert_eq!(run(&["-n", "2"], "a\nb\nc\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "+2"], "a\nb\nc\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "-2"], "a\nb\nc\nd\ne\n").1, "a\nb\nc\n");
        // GNU quirk probed: head -n -0 prints everything
        assert_eq!(run(&["-n", "-0"], "a\nb\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "99"], "a\nb\n").1, "a\nb\n");
        assert_eq!(run(&["-n", "0"], "a\nb\n").1, "");
        assert_eq!(run(&["-n", "x"], "a\n").0, 1);
        assert!(run(&["-n", "x"], "a\n")
            .2
            .contains("invalid number of lines"));
        // no trailing newline on the last kept record
        assert_eq!(run(&[], "abc").1, "abc");
    }

    #[test]
    fn byte_counts() {
        assert_eq!(run(&["-c", "2"], "abcde").1, "ab");
        assert_eq!(run(&["-c", "-2"], "abcde").1, "abc");
        assert_eq!(run(&["-c", "+2"], "abcde").1, "ab");
        assert_eq!(run(&["-c", "0"], "abcde").1, "");
        assert_eq!(run(&["-c", "1b"], "abcde").1, "abcde");
        assert!(run(&["-c", "x"], "abc")
            .2
            .contains("invalid number of bytes"));
        assert_eq!(run(&["--bytes=1"], "ab\n").1, "a");
        assert_eq!(run(&["--lines=1"], "ab\n").1, "ab\n");
        assert_eq!(run(&["-3"], "a\nb\nc\nd\n").1, "a\nb\nc\n");
    }

    #[test]
    fn zero_terminated() {
        assert_eq!(run(&["-z", "-n", "2"], "a\0b\0c\0").1, "a\0b\0");
        assert_eq!(run(&["-z", "-n", "-1"], "a\0b\0").1, "a\0");
    }

    #[test]
    fn file_headers() {
        let sc = ScratchCtx::new("head-files");
        std::fs::write(sc.path("f1"), "1\n2\n3\n").unwrap();
        std::fs::write(sc.path("f2"), "4\n5\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["f1", "f2"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "==> f1 <==\n1\n2\n3\n\n==> f2 <==\n4\n5\n");
        // -q suppresses; -v forces on even for one file; '-' says stdin
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-q", "f1", "f2"].iter().map(Into::into).collect();
        head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "1\n2\n3\n4\n5\n");
        let mut io = MemIo::new("x\n");
        let owned: Vec<OsString> = ["-v"].iter().map(Into::into).collect();
        head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "==> standard input <==\nx\n");
        let mut io = MemIo::new("x\n");
        let owned: Vec<OsString> = ["-v", "-"].iter().map(Into::into).collect();
        head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "==> standard input <==\nx\n");
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["f1"].iter().map(Into::into).collect();
        head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "1\n2\n3\n");
    }

    #[test]
    fn errors_and_mixed_operands() {
        let sc = ScratchCtx::new("head-err");
        std::fs::write(sc.path("f2"), "b\n").unwrap();
        std::fs::create_dir(sc.path("adir")).unwrap();
        // missing file: rc 1, message, later files continue
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["missing", "f2"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "==> f2 <==\nb\n");
        assert!(io
            .err()
            .starts_with("head: cannot open 'missing' for reading:"));
        // directory operand dies at the read like POSIX
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["adir"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io
            .err()
            .contains("head: error reading 'adir': Is a directory"));
        // mixed stdin and files keep header order
        let mut io = MemIo::new("s\n");
        let owned: Vec<OsString> = ["-v", "f2", "-"].iter().map(Into::into).collect();
        let r = head_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "==> f2 <==\nb\n\n==> standard input <==\ns\n");
        assert_eq!(run(&["--bogus"], "x\n").0, 1);
    }
}
