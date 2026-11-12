//! `sum` — GNU coreutils sum.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/p-sum.sh).
//!
//! Two algorithms:
//! * BSD (`-r`, the default): the classic 16-bit rotate-right-and-add
//!   checksum, block count in 1024-byte units, printed as
//!   `%05d %5ld NAME` (probed: empty file -> `00000     0`).
//! * SysV (`-s`, `--sysv`): the plain byte sum modulo 2^16, block count
//!   in 512-byte units, printed as `%d %ld NAME` (probed: empty file ->
//!   `0 0`, 600 bytes -> `... 2`).
//!
//! Both read the whole stream for the checksum and count blocks by the
//! bytes actually read, rounded up. Multiple files are processed
//! sequentially; a failed open reports `sum: NAME: <reason>` (quotef
//! shape) and later files still run, rc 1. `-` is stdin (the name still
//! prints as `-`, probed).

use std::ffi::OsString;
use std::io::Read;

use crate::argopt::{self, OptSpec};
use crate::fsutil::{gnu_open_reason, quotef, resolve};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('r'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "sysv",
        takes_value: false,
    },
];

/// BSD rotate-right-and-add checksum over the bytes (sum.c bsd_sum_file).
fn bsd_checksum(bytes: &[u8]) -> u16 {
    let mut checksum: u32 = 0;
    for &b in bytes {
        checksum = (checksum >> 1) + ((checksum & 1) << 15);
        checksum += b as u32;
        checksum &= 0xffff;
    }
    checksum as u16
}

/// SysV checksum: the byte sum folded at 16 bits until it fits
/// (sum.c sysv_sum_file; probed: 1500 'b' bytes -> 15930 = 15928 + 2).
fn sysv_checksum(bytes: &[u8]) -> u16 {
    let mut sum: u32 = bytes.iter().map(|&b| b as u32).sum();
    while sum > 0xffff {
        sum = (sum & 0xffff) + (sum >> 16);
    }
    sum as u16
}

// `--help` body verbatim from `env sum --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const SUM_HELP_BODY: &str = r#"Usage: sum [OPTION]... [FILE]...
Print or check BSD (16-bit) checksums.

With no FILE, or when FILE is -, read standard input.

  -r              use BSD sum algorithm (the default), use 1K blocks
  -s, --sysv      use System V sum algorithm, use 512 bytes blocks
      --help        display this help and exit
      --version     output version information and exit

"#;

pub fn sum_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("sum", args, OPTS, io, ctx, 1) {
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
                    SUM_HELP_BODY,
                    gnuhelp::footer("sum", "sum invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("sum", "Kayvan Aghaiepour and David MacKenzie")
                )?;
                0
            }
        });
    }

    let sysv = p.has("sysv");

    let files: Vec<String> = if p.positionals.is_empty() {
        vec!["-".to_string()]
    } else {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };

    // GNU prints the file name only when an operand was given (including
    // "-"); the implicit default operand prints the bare sums (probed)
    let name_shown = !p.positionals.is_empty();
    let mut failed = false;
    for file in &files {
        let bytes = if file == "-" {
            let mut buf = Vec::new();
            if io.stdin.read_to_end(&mut buf).is_err() {
                let _ = writeln!(io.stderr, "sum: {file}: read error");
                failed = true;
                continue;
            }
            buf
        } else {
            match std::fs::File::open(resolve(ctx, file)) {
                Ok(mut f) => {
                    let mut buf = Vec::new();
                    if f.read_to_end(&mut buf).is_err() {
                        let reason = if std::fs::metadata(resolve(ctx, file))
                            .map(|m| m.is_dir())
                            .unwrap_or(false)
                        {
                            "Is a directory".to_string()
                        } else {
                            "read error".to_string()
                        };
                        let _ = writeln!(io.stderr, "sum: {file}: {reason}");
                        failed = true;
                        continue;
                    }
                    buf
                }
                Err(e) => {
                    let reason = gnu_open_reason(ctx, file, &e, false);
                    let _ = writeln!(io.stderr, "sum: {}: {reason}", quotef(file));
                    failed = true;
                    continue;
                }
            }
        };
        if sysv {
            let checksum = sysv_checksum(&bytes);
            let blocks = bytes.len().div_ceil(512);
            if name_shown {
                writeln!(io.stdout, "{checksum} {blocks} {file}")?;
            } else {
                writeln!(io.stdout, "{checksum} {blocks}")?;
            }
        } else {
            let checksum = bsd_checksum(&bytes);
            let blocks = bytes.len().div_ceil(1024);
            if name_shown {
                writeln!(io.stdout, "{checksum:05} {blocks:>5} {file}")?;
            } else {
                writeln!(io.stdout, "{checksum:05} {blocks:>5}")?;
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("sum");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: sum [OPTION]... [FILE]...\n"));
        assert!(out.contains("  -s, --sysv      use System V sum algorithm"));
        assert!(out.ends_with("info '(coreutils) sum invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("sum (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Kayvan Aghaiepour and David MacKenzie.\n"));
    }

    #[test]
    fn checksum_algorithms() {
        // probed: 'abc' stdin -> BSD 16556 blocks 1; SysV 294 blocks 1
        assert_eq!(run(&[], "abc").1, "16556     1\n");
        assert_eq!(run(&["-s"], "abc").1, "294 1\n");
        assert_eq!(run(&["-r"], "abc").1, "16556     1\n");
        // probed: empty input
        assert_eq!(run(&[], "").1, "00000     0\n");
        assert_eq!(run(&["-s"], "").1, "0 0\n");
    }

    #[test]
    fn byte_sums_match_gnu() {
        // SysV checksum = byte sum folded at 16 bits: 'abc' = 294
        assert_eq!(sysv_checksum(b"abc"), 294);
        // the fold: 1500 'b' bytes = 147000 -> 15928 + (2) = 15930 (probed)
        assert_eq!(sysv_checksum(&[b'b'; 1500]), 15930);
        // BSD rotate-add on 'abc': hand-computed
        let mut c: u32 = 0;
        for b in b"abc" {
            c = (c >> 1) + ((c & 1) << 15) + *b as u32;
            c &= 0xffff;
        }
        assert_eq!(bsd_checksum(b"abc"), c as u16);
        assert_eq!(bsd_checksum(b"abc"), 16556);
    }

    #[test]
    fn explicit_dash_prints_name() {
        assert_eq!(run(&["-"], "ab").1, "32914     1 -\n");
        assert_eq!(run(&["-s", "-"], "ab").1, "195 1 -\n");
    }

    #[test]
    fn file_block_counts() {
        let sc = ScratchCtx::new("sum-files");
        std::fs::write(sc.path("big600"), vec![b'a'; 600]).unwrap();
        std::fs::write(sc.path("big1500"), vec![b'b'; 1500]).unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["big600"].iter().map(Into::into).collect();
        sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        // 600 bytes: 1 block of 1024 (BSD)
        assert_eq!(io.out().split_whitespace().nth(1), Some("1"));
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-s", "big600"].iter().map(Into::into).collect();
        sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        // 600 bytes: 2 blocks of 512 (SysV)
        assert_eq!(io.out().split_whitespace().nth(1), Some("2"));
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["big1500"].iter().map(Into::into).collect();
        sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        // 1500 bytes: 2 blocks of 1024 (BSD)
        assert_eq!(io.out().split_whitespace().nth(1), Some("2"));
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["-s", "big1500"].iter().map(Into::into).collect();
        sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        // 1500 bytes: 3 blocks of 512 (SysV)
        assert_eq!(io.out().split_whitespace().nth(1), Some("3"));
    }

    #[test]
    fn multi_file_and_errors() {
        let sc = ScratchCtx::new("sum-multi");
        std::fs::write(sc.path("f1"), "abc").unwrap();
        std::fs::write(sc.path("f2"), "ab").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = ["f1", "f2"].iter().map(Into::into).collect();
        let r = sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "16556     1 f1\n32914     1 f2\n");
        // missing file: quotef'd name, rc 1, later files continue
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["nosuch", "f1"].iter().map(Into::into).collect();
        let r = sum_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("sum: nosuch: No such file or directory"));
        assert_eq!(io.out(), "16556     1 f1\n");
        assert_eq!(run(&["--bogus"], "").0, 1);
    }
}
