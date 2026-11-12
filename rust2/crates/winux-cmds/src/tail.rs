//! `tail` — GNU coreutils tail.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-tail.sh, o-tail.txt).
//!
//! Count argument grammar (tail.c's parse): a leading `+` means start at
//! record K (1-based; `+0` prints everything, probed), a leading `-` is
//! stripped (`-n -K` == `-n K` — unlike head, probed: `-n -0` prints
//! NOTHING), bare K means the last K records. Suffixes match head
//! (b 512, kB 1000, K 1024, MB/M 1024^2, ...). `-n -0` / `-c -0` print
//! nothing. Records are lines, or NUL-delimited chunks with -z. Inputs
//! are read whole; files are processed sequentially with the header
//! written before each (first header has no leading blank, later headers
//! are preceded by a blank line; `-` prints `standard input`). Headers
//! appear when more than one input is read or -v is given; -q never.
//!
//! Errors (rc 1, later files still processed): `cannot open 'X' for
//! reading: <reason>`; a directory operand dies at the read with
//! `error reading 'X': Is a directory` (recovered from a metadata probe,
//! like GNU's open-then-read order).
//!
//! Windows subset (documented deviation): `-f`/`-F`/`--retry` do NOT
//! follow (interactive long-line territory). The option is accepted, the
//! tail output is emitted, then `tail: cannot follow end of file: not
//! supported` goes to stderr and the status is 1; GNU (and the 1.x C++
//! applet) would keep running instead. Gate rows never exercise it.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use crate::fsutil::{gnu_open_reason, resolve};
use crate::gnuhelp;
use crate::head::{parse_count, Count};
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('c'),
        long: "bytes",
        takes_value: true,
    },
    // GNU's -f takes an OPTIONAL argument ([=name|=descriptor]); the
    // follow modes themselves are an unsupported subset, so a plain flag
    // keeps `-f -n 1` parsing correct (getopt optional args never steal
    // the next argv word)
    OptSpec {
        short: Some('f'),
        long: "follow",
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
    let bounds = crate::head::record_bounds(input, delim, bytes_mode);
    let count = bounds.len() as u128;
    if count < k {
        return out.write_all(input);
    }
    let start = bounds[(count - k - 1) as usize];
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
        if matches!(
            s.as_str(),
            "-c" | "-n"
                | "-f"
                | "-s"
                | "--bytes"
                | "--lines"
                | "--follow"
                | "--pid"
                | "--sleep-interval"
                | "--max-unchanged-stats"
        ) {
            skip_next_value = true;
        } else if s.len() > 1 && s.starts_with('-') && !s.starts_with("--") {
            let last = s.chars().last();
            if matches!(last, Some('c') | Some('n') | Some('f') | Some('s')) {
                skip_next_value = true;
            }
        }
        norm.push(a.clone());
    }
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
    let mut count = if bytes_mode {
        TailCount::Last(10)
    } else {
        TailCount::Last(10)
    };
    if let Some(v) = p.value_of("bytes").or_else(|| p.value_of("lines")) {
        match parse_tail_count(&v) {
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
    let quiet = p.has("quiet") || p.has("silent");
    let verbose = p.has("verbose");
    let follow = p.has("follow");
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
                        "tail: error reading 'standard input': Is a directory"
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
                            let reason = if std::fs::metadata(resolve(ctx, file))
                                .map(|m| m.is_dir())
                                .unwrap_or(false)
                            {
                                "Is a directory".to_string()
                            } else {
                                "read error".to_string()
                            };
                            let _ = writeln!(io.stderr, "tail: error reading '{file}': {reason}");
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
                        let _ = writeln!(io.stderr, "tail: error reading '{file}': Is a directory");
                        failed = true;
                        continue;
                    }
                    let reason = gnu_open_reason(ctx, file, &e, false);
                    let _ = writeln!(
                        io.stderr,
                        "tail: cannot open '{file}' for reading: {reason}"
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
            TailCount::Last(k) => emit_last(&bytes, k, delim, bytes_mode, io.stdout),
            TailCount::FromStart(k) => emit_from(&bytes, k, delim, bytes_mode, io.stdout),
        };
        if r.is_err() {
            let _ = writeln!(io.stderr, "tail: write error");
            failed = true;
        }
    }
    // documented Windows subset: no following (see module docs)
    if follow {
        let _ = writeln!(io.stderr, "tail: cannot follow end of file: not supported");
        return Ok(1);
    }
    Ok(if failed { 1 } else { 0 })
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
        let r = tail_cmd(&owned, &mut io.split(), &ctx).unwrap();
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
        // documented subset: -f dumps then refuses with rc 1
        let (r, out, err) = run(&["-f", "-n", "1"], "a\nb\n");
        assert_eq!(r, 1);
        assert_eq!(out, "b\n");
        assert!(err.contains("cannot follow end of file"));
        assert_eq!(run(&["--bogus"], "x\n").0, 1);
    }
}
