//! `tac` — GNU coreutils semantics, following src/tac.c (coreutils 9.4)
//! as the specification; every case below was probed byte-for-byte against
//! WSL coreutils 9.4 (target/gnu-probe/p8.sh, p9.sh).
//!
//! Records are separated by occurrences of the separator string (default
//! "\n"); the separator is attached to the record it FOLLOWS in the file,
//! or PRECEDES with -b. -r treats the separator as a glibc-BRE regex
//! (see bre.rs). The buffer algorithm reproduces tac_seekable on a
//! fully-buffered input (GNU reads backwards in growing chunks; for any
//! input that fits the buffer the carving is identical):
//!
//! * non-regex: scan backward for separator occurrences starting at
//!   past_end - sep_len (tac.c initializes match_start one match-length
//!   back so the last full separator position is probed first). An EMPTY
//!   separator scans for NUL bytes, never matches, and prints the input
//!   unchanged (GNU probed: `tac -s ''` is identity) — the sentinel
//!   semantics of tac.c's prepended separator.
//! * regex: search buf[..match_start) backward for the rightmost match
//!   start (longest at that start), no position backup; the first match
//!   landing exactly at the buffer end flushes nothing (tac.c's
//!   first_time rule for separator_ends_record), which is why
//!   `-r -s '[0-9]+'` on "a1b22c333" starts its output with "33c3...".
//! * `-s ''` with -r is `separator cannot be empty`, rc 1; an invalid
//!   regex fails before any file is read, rc 1.
//!
//! Files are processed sequentially, each fully reversed independently;
//! `-` is stdin; a missing file reports `failed to open 'X' for reading`
//! and rc 1 while later files still run. Output is binary-safe.

use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt::{self, OptSpec};
use crate::bre::Regex;
use crate::fsutil::resolve;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('b'),
        long: "before",
        takes_value: false,
    },
    OptSpec {
        short: Some('r'),
        long: "regex",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "separator",
        takes_value: true,
    },
];

/// Literal-separator carving (tac.c sentinel branch).
fn tac_literal(
    buf: &[u8],
    separator: &str,
    sep_ends_record: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    let sep = separator.as_bytes();
    // An empty separator uses the NUL-sentinel rule of tac.c: first_char
    // = '\0' never occurs, so every scan reaches the front unchanged.
    let sep_len = if sep.is_empty() { 1 } else { sep.len() };
    let first_char = if sep.is_empty() { 0u8 } else { sep[0] };
    let mut past_end = buf.len();
    // tac.c: initial match_start = past_end - (match_length - 1)
    let mut match_start = past_end as isize - (sep_len as isize - 1);
    loop {
        // Probe p = match_start-1 downward; every probe satisfies
        // p + sep_len <= past_end, so the slice is always in bounds.
        let mut found: Option<usize> = None;
        let mut p = match_start - 1;
        while p >= 0 {
            let at = p as usize;
            if buf[at] == first_char && &buf[at..at + sep_len] == sep {
                found = Some(at);
                break;
            }
            p -= 1;
        }
        match found {
            None => {
                // backed off the front with file_pos == 0
                out.write_all(&buf[..past_end])?;
                return Ok(());
            }
            Some(s) => {
                if sep_ends_record {
                    out.write_all(&buf[s + sep_len..past_end])?;
                    past_end = s + sep_len;
                } else {
                    out.write_all(&buf[s..past_end])?;
                    past_end = s;
                }
                // tac.c: match_start -= match_length - 1
                match_start = s as isize - (sep_len as isize - 1);
            }
        }
    }
}

/// Regex-separator carving (tac.c sentinel_length == 0 branch).
fn tac_regex(
    buf: &[u8],
    re: &Regex,
    sep_ends_record: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    let mut past_end = buf.len();
    let mut match_start = buf.len();
    let mut first_time = true;
    loop {
        // re_search(pattern, buf, i, i-1, 1-i): backward over buf[..i);
        // i == 0 ("range == 1") goes straight to the front.
        let found = if match_start == 0 {
            None
        } else {
            re.search_backward(buf, match_start, match_start - 1)
        };
        match found {
            None => {
                out.write_all(&buf[..past_end])?;
                return Ok(());
            }
            Some((s, e)) => {
                if sep_ends_record {
                    let match_end = e;
                    // first match at the exact buffer end flushes nothing
                    if !first_time || match_end != past_end {
                        out.write_all(&buf[match_end..past_end])?;
                    }
                    past_end = match_end;
                    first_time = false;
                } else {
                    out.write_all(&buf[s..past_end])?;
                    past_end = s;
                }
                match_start = s;
            }
        }
    }
}

// `--help` body verbatim from `env tac --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const TAC_HELP_BODY: &str = r#"Usage: tac [OPTION]... [FILE]...
Write each FILE to standard output, last line first.

With no FILE, or when FILE is -, read standard input.

Mandatory arguments to long options are mandatory for short options too.
  -b, --before             attach the separator before instead of after
  -r, --regex              interpret the separator as a regular expression
  -s, --separator=STRING   use STRING as the separator instead of newline
      --help        display this help and exit
      --version     output version information and exit

"#;

pub fn tac_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("tac", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    // GNU exits at the first --help/--version getopt_long encounters; the
    // ordered find reproduces that for `--version --help` (version wins).
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
                    TAC_HELP_BODY,
                    gnuhelp::footer("tac", "tac invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("tac", "Jay Lepreau and David MacKenzie")
                )?;
                0
            }
        });
    }
    let separator = p.value_of("separator").unwrap_or("\n").to_string();
    let regex_mode = p.has("regex");
    let sep_ends_record = !p.has("before");
    if regex_mode && separator.is_empty() {
        // tac.c: error (EXIT_FAILURE, 0, "separator cannot be empty")
        let _ = writeln!(io.stderr, "tac: separator cannot be empty");
        return Ok(1);
    }
    // Compile once up front so an invalid regex fails before any file is
    // read, like GNU (re_compile_pattern runs before processing).
    let compiled = if regex_mode {
        match Regex::compile(&separator) {
            Ok(re) => Some(re),
            Err(e) => {
                let _ = writeln!(io.stderr, "tac: {e}");
                return Ok(1);
            }
        }
    } else {
        None
    };

    // Operands: default is stdin ("-").
    let files: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };

    let mut failed = false;
    for file in &files {
        let bytes = if file == "-" {
            let mut buf = Vec::new();
            if std::io::copy(&mut io.stdin, &mut buf).is_err() {
                failed = true;
                continue;
            }
            buf
        } else {
            match std::fs::File::open(resolve(ctx, file)) {
                Ok(mut f) => {
                    let mut buf = Vec::new();
                    if f.read_to_end(&mut buf).is_err() {
                        let _ = writeln!(io.stderr, "tac: {file}: read error");
                        failed = true;
                        continue;
                    }
                    buf
                }
                Err(_) => {
                    let _ = writeln!(
                        io.stderr,
                        "tac: failed to open '{file}' for reading: No such file or directory"
                    );
                    failed = true;
                    continue;
                }
            }
        };
        let result = if regex_mode {
            tac_regex(
                &bytes,
                compiled.as_ref().unwrap(),
                sep_ends_record,
                io.stdout,
            )
        } else {
            tac_literal(&bytes, &separator, sep_ends_record, io.stdout)
        };
        if result.is_err() {
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    // [GNU] `--help`/`--version` print the probed coreutils text and exit 0
    // (strict rows ta24/ta27).
    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: tac [OPTION]... [FILE]...\n"));
        assert!(out.ends_with("info '(coreutils) tac invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("tac (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Jay Lepreau and David MacKenzie.\n"));
    }

    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let mut io = MemIo::new(stdin);
        let sc = ScratchCtx::new("tac");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::tac_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    // Every case mirrors a GNU probe (p8/p9) byte-for-byte.
    fn expect_out(args: &[&str], stdin: &str, want: &str) {
        let (r, out, _) = run(args, stdin);
        assert_eq!(r, 0, "rc for {args:?} (stdin {stdin:?})");
        assert_eq!(out, want, "stdout for {args:?} (stdin {stdin:?})");
    }

    #[test]
    fn default_newline() {
        expect_out(&[], "1\n2\n3\n", "3\n2\n1\n");
        // no trailing separator: the last record attaches bare (GNU probed)
        expect_out(&[], "1\n2\n3", "32\n1\n");
        expect_out(&[], "", "");
    }

    #[test]
    fn before_mode_quirks() {
        // GNU probed: the trailing separator is emitted FIRST as its own
        // record piece, and the head record has no leading separator.
        expect_out(&["-b"], "1\n2\n3\n", "\n\n3\n21");
        expect_out(&["-b", "-sX"], "aXbXc", "XcXba");
    }

    #[test]
    fn literal_separator() {
        expect_out(&["-sX"], "aXbXc", "cbXaX");
        expect_out(&["-s", "X"], "aXbXc", "cbXaX");
        // empty separator: identity (NUL sentinel never matches)
        expect_out(&["-s", ""], "aXbXc", "aXbXc");
    }

    #[test]
    fn regex_separator() {
        expect_out(&["-r", "-s", "."], "abc", "cba");
        expect_out(&["-r", "-s", "X"], "aXbYc", "bYcaX");
        // 'X|Y' is a literal in BRE: GNU prints the input unchanged
        expect_out(&["-r", "-s", "X|Y"], "aXbYc", "aXbYc");
        expect_out(&["-r", "-s", "[0-9]+"], "a1b22c333", "33c32b2a1");
        expect_out(&["-r", "-s", "[0-9]+"], "1a22b333c", "c33b32a21");
        expect_out(&["-r", "-s", "[XY]"], "aXbYc", "cbYaX");
        expect_out(&["-r", "-s", "X*Y"], "aXbYc", "caXbY");
        expect_out(&["-r", "-s", "X?"], "aXbXc", "cbXaX");
        expect_out(&["-r", "-s", "b+"], "abbbc", "cbbab");
        expect_out(&["-r", "-s", "[0-9][0-9]"], "a12b34c", "cb34a12");
    }

    #[test]
    fn regex_empty_separator_is_error() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("tac-err");
        let ctx = sc.ctx();
        let args: Vec<OsString> = vec!["-r".into(), "-s".into(), "".into()];
        let r = super::tac_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("separator cannot be empty"));
        // invalid regex: fails before reading anything, rc 1
        let mut io = MemIo::new("");
        let args: Vec<OsString> = vec!["-r".into(), "-s".into(), "[a".into()];
        let r = super::tac_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.out().is_empty());
    }

    #[test]
    fn file_errors_continue() {
        let sc = ScratchCtx::new("tac-files");
        std::fs::write(sc.path("a"), "1\n2\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["missing".into(), "a".into()];
        let r = super::tac_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("failed to open 'missing' for reading"));
        assert_eq!(io.out(), "2\n1\n");
    }

    #[test]
    fn stdin_dash() {
        let mut io = MemIo::new("x\ny\n");
        let sc = ScratchCtx::new("tac-dash");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["-".into()];
        let r = super::tac_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        let _ = sc;
        assert_eq!(io.out(), "y\nx\n");
    }
}
