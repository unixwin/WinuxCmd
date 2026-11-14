//! `rev` — util-linux rev semantics (the WSL oracle binary is util-linux
//! 2.39.3, not coreutils; probed 2026-11-12).
//!
//! * Reverses each record characterwise. The record separator is `\n`
//!   (`-0`/`--zero`: NUL — accepted by util-linux 2.39.3 even though its
//!   `--help` lists only -h/-V, probed); a final record without the
//!   separator is reversed and written WITHOUT the separator appended.
//! * Reversal unit follows the locale (LC_ALL > LC_CTYPE > LANG, default
//!   C): C/POSIX locales reverse bytes; UTF-8 locales reverse whole
//!   UTF-8 character units (probed: `héllo` -> `olléh`, a 4-byte emoji
//!   stays one unit). Invalid byte sequences and non-ASCII input under a
//!   C locale make GNU rev 2.39.3 HANG (rc 124 under `timeout`, probed);
//!   this implementation reverses byte-wise there instead of hanging —
//!   documented deviation, and gate cases stay on the GNU-defined side
//!   (ASCII under C, valid UTF-8 under C.UTF-8).
//! * Operands are files (resolved through the shell-namespace boundary);
//!   no operands reads stdin. `-` is NOT stdin: GNU opens it literally
//!   (`rev: cannot open -: No such file or directory`). One failing
//!   operand reports the GNU wording and rc 1 but later operands are
//!   still processed (probed order: errors on stderr, output continues).
//! * Options are getopt_long GNU-shaped and permuted (options may follow
//!   operands; `rev -V extra` prints the version): `-h/--help`,
//!   `-V/--version`, `-0/--zero`. Unknown shorts/longs report
//!   `rev: invalid option -- 'x'` / `rev: unrecognized option '--x'` with
//!   rc 1.
//! * A directory operand makes GNU rev 2.39.3 hang as well (probed);
//!   Windows opens of directories fail outright with the GNU recovery
//!   reason instead — no gate row can cover that case honestly.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('h'),
        long: "help",
        takes_value: false,
    },
    OptSpec {
        short: Some('V'),
        long: "version",
        takes_value: false,
    },
    OptSpec {
        short: Some('0'),
        long: "zero",
        takes_value: false,
    },
];

// `--help` body verbatim from `env rev --help` under WSL util-linux 2.39.3
// (probed byte-for-byte via od; no GNU coreutils footer on this one).
const REV_HELP_BODY: &str = r#"Usage: rev [options] [file ...]

Reverse lines characterwise.

Options:
 -h, --help     display this help
 -V, --version  display version

For more details see rev(1).
"#;

const REV_VERSION: &str = "rev from util-linux 2.39.3\n";

pub fn rev_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("rev", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    // getopt order: the first of --help/--version encountered wins, even
    // when operands follow (util-linux permutes).
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == "help" || f.as_str() == "version")
    {
        return Ok(match which.as_str() {
            "help" => {
                write!(io.stdout, "{REV_HELP_BODY}")?;
                0
            }
            _ => {
                write!(io.stdout, "{REV_VERSION}")?;
                0
            }
        });
    }
    let zero = p.has("zero");
    let separator = if zero { 0u8 } else { b'\n' };
    let utf8 = !byte_locale(ctx);

    let mut failed = false;
    if p.positionals.is_empty() {
        let mut data = Vec::new();
        if std::io::copy(&mut io.stdin, &mut data).is_err() {
            let _ = writeln!(io.stderr, "rev: read error");
            return Ok(1);
        }
        reverse_into(&data, separator, utf8, &mut *io.stdout)?;
    } else {
        let out = &mut *io.stdout;
        // Operands stay LITERAL (the b2sum_wildcard digest-family contract):
        // util-linux rev receives shell-expanded operands and `rev '*.txt'`
        // reports `cannot open *.txt`; the 1.x body glob-expanded, a GNU
        // deviation the rust2 port does not carry.
        for operand in &p.positionals {
            let name = operand.to_string_lossy().into_owned();
            let mut data = Vec::new();
            match std::fs::File::open(fsutil::resolve(ctx, &name)) {
                Ok(mut f) => {
                    if std::io::copy(&mut f, &mut data).is_err() {
                        let _ = writeln!(io.stderr, "rev: {name}: read error");
                        failed = true;
                        continue;
                    }
                }
                Err(e) => {
                    // util-linux wording: `rev: cannot open <name>: <strerror>`
                    let reason = fsutil::gnu_open_reason(ctx, &name, &e, false);
                    let _ = writeln!(io.stderr, "rev: cannot open {name}: {reason}");
                    failed = true;
                    continue;
                }
            }
            reverse_into(&data, separator, utf8, out)?;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// True when the effective LC_CTYPE locale (LC_ALL > LC_CTYPE > LANG,
/// default C) means single-byte C/POSIX semantics: byte-wise reversal.
fn byte_locale(ctx: &Ctx) -> bool {
    let val = |key: &str| {
        ctx.env
            .get(key)
            .map(|v| v.to_string_lossy().into_owned())
            .unwrap_or_default()
    };
    let lc_all = val("LC_ALL");
    let effective = if !lc_all.is_empty() {
        lc_all
    } else {
        let lc_ctype = val("LC_CTYPE");
        if !lc_ctype.is_empty() {
            lc_ctype
        } else {
            val("LANG")
        }
    };
    // setlocale(LC_ALL, "") with none of the three set falls back to "C"
    effective.is_empty() || effective == "C" || effective == "POSIX"
}

/// Reverse each record of `data` into `out`. A record's trailing
/// separator stays at the end; the body before it is reversed.
fn reverse_into(
    data: &[u8],
    separator: u8,
    utf8: bool,
    out: &mut dyn std::io::Write,
) -> std::io::Result<()> {
    let mut start = 0usize;
    while start < data.len() {
        let end = match data[start..].iter().position(|&b| b == separator) {
            Some(off) => start + off + 1, // include the separator
            None => data.len(),
        };
        let record = &data[start..end];
        let body = if record.last() == Some(&separator) && end > start {
            &record[..record.len() - 1]
        } else {
            record
        };
        let reversed = reverse_units(body, utf8);
        out.write_all(&reversed)?;
        if record.len() != body.len() {
            out.write_all(&[separator])?;
        }
        start = end;
    }
    Ok(())
}

/// Reverse `body` in units: bytes under a C locale or for invalid UTF-8,
/// whole UTF-8 characters otherwise (the C++ applet's utf8_unit_length
/// recovery: a truncated or malformed sequence degenerates to 1 byte).
fn reverse_units(body: &[u8], utf8: bool) -> Vec<u8> {
    if !utf8 {
        return body.iter().rev().copied().collect();
    }
    let mut out = Vec::with_capacity(body.len());
    let mut i = body.len();
    while i > 0 {
        // find the start of the unit that ends at i: walk back over
        // continuation bytes up to the lead byte
        let mut start = i - 1;
        let mut back = 0usize;
        while start > 0 && (body[start] & 0xC0) == 0x80 && back < 3 {
            start -= 1;
            back += 1;
        }
        if utf8_unit_length(body, start) == i - start {
            out.extend_from_slice(&body[start..i]);
            i = start;
        } else {
            // invalid sequence: the last byte stands alone
            out.push(body[i - 1]);
            i -= 1;
        }
    }
    out
}

/// Length of the UTF-8 character starting at `pos` (1 for invalid or
/// truncated lead bytes, mirroring the C++ applet).
fn utf8_unit_length(bytes: &[u8], pos: usize) -> usize {
    let c = bytes[pos];
    let len = if c < 0x80 {
        1
    } else if c & 0xE0 == 0xC0 {
        2
    } else if c & 0xF0 == 0xE0 {
        3
    } else if c & 0xF8 == 0xF0 {
        4
    } else {
        1
    };
    if pos + len > bytes.len() {
        return 1;
    }
    for k in 1..len {
        if bytes[pos + k] & 0xC0 != 0x80 {
            return 1;
        }
    }
    len
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    use super::*;

    fn run_env(
        env: &[(&str, &str)],
        args: &[&str],
        stdin: &[u8],
    ) -> (u32, Vec<u8>, String, ScratchCtx) {
        let mut sc = ScratchCtx::new("rev");
        for (k, v) in env {
            sc.env.set(k, v);
        }
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        io.input = std::io::Cursor::new(stdin.to_vec());
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = rev_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.output.clone(), io.err(), sc)
    }

    fn run(args: &[&str], stdin: &[u8]) -> (u32, Vec<u8>, String) {
        let (r, out, err, _sc) = run_env(&[], args, stdin);
        (r, out, err)
    }

    #[test]
    fn ascii_lines_reverse() {
        let (r, out, err) = run(&[], b"abc\n\nxy z\n");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert_eq!(out, b"cba\n\nz yx\n");
        // final record without newline: reversed, no separator added
        let (r, out, _) = run(&[], b"ab");
        assert_eq!(r, 0);
        assert_eq!(out, b"ba");
        // empty stdin: rc 0, empty out
        let (r, out, _) = run(&[], b"");
        assert_eq!((r, out.len()), (0, 0));
    }

    #[test]
    fn utf8_units_under_utf8_locale() {
        // With an explicit UTF-8 locale (the WSL host default LANG=C.UTF-8
        // is not visible to the Windows side, so gate rows and these tests
        // pin LC_ALL), é (2 bytes) and a 4-byte emoji reverse as units
        // (probed GNU). Without any LC_* the effective locale is C.
        let env = [("LC_ALL", "C.UTF-8")];
        let (r, out, _, _) = run_env(&env, &[], "h\u{e9}llo\n".as_bytes());
        assert_eq!(r, 0);
        assert_eq!(out, "oll\u{e9}h\n".as_bytes());
        let (r, out, _, _) = run_env(&env, &[], "f\u{1f600}g\n".as_bytes());
        assert_eq!(r, 0);
        assert_eq!(out, "g\u{1f600}f\n".as_bytes());
        let (r, out, _, _) = run_env(&env, &[], "ab\u{e9}\n".as_bytes());
        assert_eq!(r, 0);
        assert_eq!(out, "\u{e9}ba\n".as_bytes());
        // LC_CTYPE fallback when LC_ALL is absent
        let (r, out, _, _) = run_env(&[("LC_CTYPE", "C.UTF-8")], &[], "ab\u{e9}\n".as_bytes());
        assert_eq!(r, 0);
        assert_eq!(out, "\u{e9}ba\n".as_bytes());
    }

    #[test]
    fn c_locale_reverses_bytes() {
        let (r, out, _, _) = run_env(&[("LC_ALL", "C")], &[], "ab\u{e9}\n".as_bytes());
        assert_eq!(r, 0);
        // byte-wise: the two é bytes (0xc3 0xa9) swap order
        assert_eq!(out, [0xa9u8, 0xc3, b'b', b'a', b'\n']);
    }

    #[test]
    fn invalid_sequences_stay_bytes() {
        // GNU hangs here (documented deviation); we reverse byte-wise
        let input = [b'a', 0xff, 0xfe, b'b', b'\n'];
        let (r, out, _) = run(&[], &input);
        assert_eq!(r, 0);
        assert_eq!(out, [b'b', 0xfe, 0xff, b'a', b'\n']);
    }

    #[test]
    fn zero_separator() {
        let (r, out, _) = run(&["-0"], b"ab\0cd\0");
        assert_eq!(r, 0);
        assert_eq!(out, b"ba\0dc\0");
        // missing final separator: reversed without separator
        let (r, out, _) = run(&["--zero"], b"ab\0cd");
        assert_eq!(r, 0);
        assert_eq!(out, b"ba\0dc");
        // \n is an ordinary character under -0
        let (r, out, _) = run(&["-0"], b"a\nb\0");
        assert_eq!(out, [b'b', b'\n', b'a', 0u8]);
    }

    #[test]
    fn help_version_and_options() {
        let (r, out, err) = run(&["--help"], b"");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert_eq!(out, REV_HELP_BODY.as_bytes());
        let (r, out, _) = run(&["-h"], b"");
        assert_eq!((r, out.as_slice()), (0, REV_HELP_BODY.as_bytes()));
        let (r, out, _) = run(&["--version"], b"");
        assert_eq!(r, 0);
        assert_eq!(out, REV_VERSION.as_bytes());
        let (r, out, _) = run(&["-V"], b"");
        assert_eq!(out, REV_VERSION.as_bytes());
        // help wins even with operands (getopt permute)
        let (r, out, _) = run(&["-V", "extra"], b"");
        assert_eq!(out, REV_VERSION.as_bytes());
        let _ = r;
        // unknown options
        let (r, _, err) = run(&["-q"], b"");
        assert_eq!(r, 1);
        assert!(err.contains("rev: invalid option -- 'q'"));
        assert!(err.contains("Try 'rev --help' for more information."));
        let (r, _, err) = run(&["--bogus"], b"");
        assert_eq!(r, 1);
        assert!(err.contains("rev: unrecognized option '--bogus'"));
    }

    #[test]
    fn files_and_errors() {
        let sc = ScratchCtx::new("revfiles");
        std::fs::write(sc.path("a.txt"), b"hello\n").unwrap();
        std::fs::write(sc.path("b.txt"), b"aa\n").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["a.txt".into()];
        let r = rev_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!((r, io.output.as_slice()), (0, b"olleh\n".as_slice()));
        // two files: concatenated, no headers
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = vec!["a.txt".into(), "b.txt".into()];
        let r = rev_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.output, b"olleh\naa\n".to_vec());
        // missing file: GNU wording, rc 1
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = vec!["nosuch".into()];
        let r = rev_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io
            .err()
            .contains("rev: cannot open nosuch: No such file or directory"));
        // dash operand is opened literally, not stdin
        let mut io = MemIo::new("xy\n");
        let owned: Vec<OsString> = vec!["-".into()];
        let r = rev_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("rev: cannot open -:"));
        assert!(io.output.is_empty());
    }

    #[test]
    fn locale_env_precedence() {
        // LC_ALL wins over LC_CTYPE
        assert!(!byte_locale(&ctx_of(&[
            ("LC_ALL", "C.UTF-8"),
            ("LC_CTYPE", "C"),
        ])));
        // LC_CTYPE wins over LANG
        assert!(byte_locale(&ctx_of(&[
            ("LC_CTYPE", "POSIX"),
            ("LANG", "C.UTF-8")
        ])));
        // LANG alone
        assert!(byte_locale(&ctx_of(&[("LANG", "C")])));
        assert!(!byte_locale(&ctx_of(&[("LANG", "C.UTF-8")])));
        // nothing set: default C
        assert!(byte_locale(&ctx_of(&[])));
    }

    fn ctx_of(kv: &[(&str, &str)]) -> Ctx<'static> {
        let mut me = winux_core::MapEnv::new();
        for (k, v) in kv {
            me.set(k, v);
        }
        let leaked: &'static winux_core::MapEnv = Box::leak(Box::new(me));
        Ctx {
            cwd: std::path::PathBuf::from("."),
            env: leaked,
        }
    }
}
