//! `echo` — GNU coreutils 9.4 `src/echo.c` semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (probe 2026-11-12).
//!
//! Option handling is echo's own hand-rolled scan, NOT getopt:
//! * GNU mode (no `POSIXLY_CORRECT`): leading arguments that start with
//!   `-` and contain only `n`/`e`/`E` are consumed as flags; the first
//!   non-conforming argument stops the scan and is printed as the first
//!   operand (`echo -- -n x` prints `-- -n x`; `echo -0 x` prints `-0 x`).
//!   `-e`/`-E` toggle escape interpretation with the last occurrence
//!   winning (`-eE` literal, `-Ee` interpreted). `--help`/`--version` are
//!   recognized only as the SOLE argument (`echo -n --version` prints
//!   `--version` with no trailing newline).
//! * POSIX mode (`POSIXLY_CORRECT` present, empty value counts): exactly
//!   one leading `-n` is recognized, escapes are interpreted BY DEFAULT,
//!   and `--help`/`--version` are never recognized (`POSIXLY_CORRECT=1
//!   echo --help` prints `--help`).
//!
//! Escapes (GNU mode with `-e`, or any POSIX-mode invocation): `\a \b \c
//! \e \f \n \r \t \v \\`, octal `\0NNN` — measured rules: a leading `0`
//!   consumes up to THREE more octal digits (`\0777` -> one 0xFF byte,
//!   `\0101` -> `A`, `\08` -> NUL then `8`), while `\1`-`\7` consume up to
//!   two more (`\410` -> 264 -> one 0x08 byte), truncated to u8; hex
//!   `\xHH` takes 1-2 digits (`\xb` -> 0x0B), and a `\x` with no digits
//!   or an unknown escape prints backslash-escaped literally (`\xZ`,
//!   `\q`, trailing lone `\`). `\c` stops all output including the
//!   trailing newline and the remaining operands (`echo -e 'a\cb' more`
//!   prints `a`). `\u` is not an echo escape (prints literally).
//!
//! Operands are joined with a single space by position (an empty operand
//! still earns its separator: `echo "" x` prints ` x`).

use std::ffi::OsString;

use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

// `--help` body verbatim from `env echo --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const ECHO_HELP_BODY: &str = r#"Usage: echo [SHORT-OPTION]... [STRING]...
  or:  echo LONG-OPTION
Echo the STRING(s) to standard output.

  -n             do not output the trailing newline
  -e             enable interpretation of backslash escapes
  -E             disable interpretation of backslash escapes (default)
      --help        display this help and exit
      --version     output version information and exit

If -e is in effect, the following sequences are recognized:

  \\      backslash
  \a      alert (BEL)
  \b      backspace
  \c      produce no further output
  \e      escape
  \f      form feed
  \n      new line
  \r      carriage return
  \t      horizontal tab
  \v      vertical tab
  \0NNN   byte with octal value NNN (1 to 3 digits)
  \xHH    byte with hexadecimal value HH (1 to 2 digits)

NOTE: your shell may have its own version of echo, which usually supersedes
the version described here.  Please refer to your shell's documentation
for details about the options it supports.

NOTE: printf(1) is a preferred alternative,
which does not have issues outputting option-like strings.

"#;

pub fn echo_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let posixly_correct = ctx.env.get("POSIXLY_CORRECT").is_some();

    // echo.c: allow_options = !posixly_correct
    //         || (DEFAULT_ECHO_TO_XPG == false && argc > 1 && argv[1] == "-n")
    // (argv[1] is args[0] here; the exact-match rule is why
    // `POSIXLY_CORRECT=1 echo -nn x` prints `-nn x` verbatim).
    let allow_options = !posixly_correct || (!args.is_empty() && args[0] == "-n");

    // The GNU build keeps DEFAULT_ECHO_TO_XPG = false: escape decoding
    // runs when do_v9 || posixly_correct, so POSIX mode decodes by default
    // even though its flags are mostly unrecognized.
    let mut display_return = true;
    let mut do_v9 = false;
    let mut first = 0usize;

    if allow_options {
        // Sole --help/--version, no abbreviation, before the flag scan.
        if args.len() == 1 {
            let sole = args[0].to_string_lossy();
            if sole == "--help" {
                write!(
                    io.stdout,
                    "{}{}",
                    ECHO_HELP_BODY,
                    gnuhelp::footer("echo", "echo invocation")
                )?;
                return Ok(0);
            }
            if sole == "--version" {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("echo", "Brian Fox and Chet Ramey")
                )?;
                return Ok(0);
            }
        }
        // The flag scan: every leading arg whose chars are all e/E/n is
        // consumed as flags; the first other arg ends the scan and stays
        // the first operand (echo.c `goto just_echo`).
        while first < args.len() {
            let s = args[first].to_string_lossy();
            let Some(rest) = s.strip_prefix('-') else {
                break;
            };
            if rest.is_empty() {
                break; // a lone `-` is an operand (echo.c i == 0)
            }
            let mut is_flags = true;
            for c in rest.chars() {
                if c != 'n' && c != 'e' && c != 'E' {
                    is_flags = false;
                    break;
                }
            }
            if !is_flags {
                break; // the arg itself becomes the first operand
            }
            for c in rest.chars() {
                match c {
                    'n' => display_return = false,
                    'e' => do_v9 = true,
                    _ => do_v9 = false, // 'E'
                }
            }
            first += 1;
        }
    }

    let decode = do_v9 || posixly_correct;

    let mut out = Vec::new();
    for (i, arg) in args.iter().enumerate().skip(first) {
        if i > first {
            out.push(b' ');
        }
        let s = arg.to_string_lossy();
        if decode {
            if decode_escapes(s.as_bytes(), &mut out) {
                // \c seen: echo.c `return EXIT_SUCCESS` — no further output
                // at all (no trailing newline, later operands dropped).
                io.stdout.write_all(&out)?;
                return Ok(0);
            }
        } else {
            out.extend_from_slice(s.as_bytes());
        }
    }
    if display_return {
        out.push(b'\n');
    }
    io.stdout.write_all(&out)?;
    Ok(0)
}

/// Decode echo escapes from `bytes` into `out`. Returns true when a `\c`
/// terminated the output (everything after it, including later operands
/// and the trailing newline, is suppressed).
fn decode_escapes(bytes: &[u8], out: &mut Vec<u8>) -> bool {
    let mut i = 0usize;
    while i < bytes.len() {
        if bytes[i] != b'\\' || i + 1 >= bytes.len() {
            out.push(bytes[i]);
            i += 1;
            continue;
        }
        let c = bytes[i + 1];
        match c {
            b'a' => out.push(0x07),
            b'b' => out.push(0x08),
            b'c' => return true,
            b'e' => out.push(0x1b),
            b'f' => out.push(0x0c),
            b'n' => out.push(0x0a),
            b'r' => out.push(0x0d),
            b't' => out.push(0x09),
            b'v' => out.push(0x0b),
            b'\\' => out.push(b'\\'),
            b'0'..=b'7' => {
                // echo.c: `\0` consumes up to THREE more octal digits (the
                // first digit falls through the same two-more-digits loop
                // that `\1`-`\7` use), `\1`-`\7` up to two more; the value
                // truncates to one byte.
                let max_total = if c == b'0' { 4 } else { 3 };
                let mut value: u32 = u32::from(c - b'0');
                let mut consumed = 1usize;
                while consumed < max_total
                    && i + 1 + consumed < bytes.len()
                    && (b'0'..=b'7').contains(&bytes[i + 1 + consumed])
                {
                    value = value * 8 + u32::from(bytes[i + 1 + consumed] - b'0');
                    consumed += 1;
                }
                out.push((value & 0xFF) as u8);
                // the common `i += 2` below covers `\` + the first digit
                i += consumed - 1;
            }
            b'x' => {
                let mut value: u32 = 0;
                let mut digits = 0usize;
                while digits < 2
                    && i + 2 + digits < bytes.len()
                    && bytes[i + 2 + digits].is_ascii_hexdigit()
                {
                    value = value * 16
                        + u32::from((bytes[i + 2 + digits] as char).to_digit(16).unwrap_or(0));
                    digits += 1;
                }
                if digits == 0 {
                    // echo.c not_an_escape: `\x` with no hex digits prints
                    // backslash and `x` literally.
                    out.push(b'\\');
                    out.push(b'x');
                } else {
                    out.push((value & 0xFF) as u8);
                    i += digits;
                }
            }
            _ => {
                // Unknown escape or trailing lone backslash: literal.
                out.push(b'\\');
                out.push(c);
            }
        }
        i += 2;
    }
    false
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    use super::*;

    fn run_env(env: &[(&str, &str)], args: &[&str]) -> (u32, Vec<u8>, String) {
        let mut sc = ScratchCtx::new("echo");
        for (k, v) in env {
            sc.env.set(k, v);
        }
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = echo_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.output.clone(), io.err())
    }

    fn run(args: &[&str]) -> (u32, Vec<u8>, String) {
        run_env(&[], args)
    }

    #[test]
    fn plain_operands_and_spaces() {
        let (r, out, err) = run(&[]);
        assert_eq!((r, err_str(&out)), (0u32, "\n".to_string()));
        assert!(err.is_empty());
        let (_, out, _) = run(&["hello"]);
        assert_eq!(err_str(&out), "hello\n");
        // separator by position even for empty operands
        let (_, out, _) = run(&["", "x"]);
        assert_eq!(err_str(&out), " x\n");
        let (_, out, _) = run(&["a", "b"]);
        assert_eq!(err_str(&out), "a b\n");
    }

    fn err_str(b: &[u8]) -> String {
        String::from_utf8_lossy(b).into_owned()
    }

    #[test]
    fn n_flag_suppresses_newline() {
        let (_, out, _) = run(&["-n", "hello"]);
        assert_eq!(err_str(&out), "hello");
        let (_, out, _) = run(&["-n"]);
        assert_eq!(err_str(&out), "");
        // last -n among flags wins (no reset flag)
        let (_, out, _) = run(&["-n", "-e", "-n", "z"]);
        assert_eq!(err_str(&out), "z");
    }

    #[test]
    fn flag_scan_stops_at_first_non_flag() {
        // `--` is an operand: printed verbatim, later -n not a flag
        let (_, out, _) = run(&["--", "-n", "x"]);
        assert_eq!(err_str(&out), "-- -n x\n");
        let (_, out, _) = run(&["-0", "x"]);
        assert_eq!(err_str(&out), "-0 x\n");
        // a lone `-` is an operand
        let (_, out, _) = run(&["-"]);
        assert_eq!(err_str(&out), "-\n");
        // non-flag char stops the scan; the arg itself prints
        let (_, out, _) = run(&["-n-", "x"]);
        assert_eq!(err_str(&out), "-n- x\n");
        // flags after an operand are operands
        let (_, out, _) = run(&["x", "-n"]);
        assert_eq!(err_str(&out), "x -n\n");
    }

    #[test]
    fn e_and_E_last_wins() {
        // the operand text carries a literal backslash-t, like the shell
        // single-quoted 'a\tb' probe; -e decodes it, -E leaves it literal
        let (_, out, _) = run(&["-e", "a\\tb"]);
        assert_eq!(err_str(&out), "a\tb\n");
        let (_, out, _) = run(&["-eE", "a\\tb"]);
        assert_eq!(err_str(&out), "a\\tb\n");
        let (_, out, _) = run(&["-Ee", "a\\tb"]);
        assert_eq!(err_str(&out), "a\tb\n");
        let (_, out, _) = run(&["-E", "a\\tb"]);
        assert_eq!(err_str(&out), "a\\tb\n");
    }

    #[test]
    fn escapes_table() {
        let (_, out, _) = run(&["-e", "\\a\\b\\e\\f\\n\\r\\t\\v\\\\"]);
        let want = vec![
            0x07u8, 0x08, 0x1b, 0x0c, b'\n', b'\r', b'\t', 0x0b, b'\\', b'\n',
        ];
        assert_eq!(out, want);
        // unknown escape is literal
        let (_, out, _) = run(&["-e", "\\q"]);
        assert_eq!(err_str(&out), "\\q\n");
        // trailing lone backslash is literal
        let (_, out, _) = run(&["-e", "a\\"]);
        assert_eq!(err_str(&out), "a\\\n");
        // \u is not an echo escape
        let (_, out, _) = run(&["-e", "\\u0041"]);
        assert_eq!(err_str(&out), "\\u0041\n");
    }

    #[test]
    fn octal_rules_measured() {
        // leading 0 consumes up to three MORE digits
        let (_, out, _) = run(&["-e", "\\0101"]);
        assert_eq!(out, b"A\n");
        let (_, out, _) = run(&["-e", "\\0777"]);
        assert_eq!(out, [0xFFu8, b'\n']);
        let (_, out, _) = run(&["-e", "\\08"]);
        assert_eq!(out, [0u8, b'8', b'\n']);
        // 1-7 consumes up to three digits total
        let (_, out, _) = run(&["-e", "\\410"]);
        assert_eq!(out, [0x08u8, b'\n']);
        let (_, out, _) = run(&["-e", "\\123"]);
        assert_eq!(out, [0x53u8, b'\n']); // 'S'
    }

    #[test]
    fn hex_rules_measured() {
        let (_, out, _) = run(&["-e", "\\x41\\x42"]);
        assert_eq!(out, b"AB\n");
        let (_, out, _) = run(&["-e", "a\\xb"]);
        assert_eq!(out, [b'a', 0x0b, b'\n']);
        // no hex digits: literal
        let (_, out, _) = run(&["-e", "\\xZ"]);
        assert_eq!(err_str(&out), "\\xZ\n");
        let (_, out, _) = run(&["-e", "a\\"]);
        assert_eq!(err_str(&out), "a\\\n");
    }

    #[test]
    fn c_stops_all_output() {
        let (r, out, _) = run(&["-e", "abc\\c"]);
        assert_eq!(r, 0);
        assert_eq!(out, b"abc");
        // later operands suppressed too
        let (_, out, _) = run(&["-e", "a\\cb", "more"]);
        assert_eq!(out, b"a");
    }

    #[test]
    fn sole_help_version() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(
            out.starts_with(b"Usage: echo [SHORT-OPTION]... [STRING]...\n"),
            "got {}",
            err_str(&out)
        );
        assert!(out.ends_with(b"info '(coreutils) echo invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with(b"echo (GNU coreutils) 9.4\n"));
        assert!(out.ends_with(b"Written by Brian Fox and Chet Ramey.\n"));
        // not sole: operands
        let (_, out, _) = run(&["--help", "extra"]);
        assert_eq!(err_str(&out), "--help extra\n");
        let (_, out, _) = run(&["-n", "--version"]);
        assert_eq!(err_str(&out), "--version");
    }

    #[test]
    fn posix_mode() {
        let env = [("POSIXLY_CORRECT", "1")];
        // escapes interpreted by default
        let (_, out, _) = run_env(&env, &["a\\tb"]);
        assert_eq!(err_str(&out), "a\tb\n");
        // only an exact leading -n is a flag
        let (_, out, _) = run_env(&env, &["-nn", "-n", "x"]);
        assert_eq!(err_str(&out), "-nn -n x\n");
        let (_, out, _) = run_env(&env, &["x", "-n"]);
        assert_eq!(err_str(&out), "x -n\n");
        // -e/-E are operands in POSIX mode
        let (_, out, _) = run_env(&env, &["-e", "-E", "a\\tb"]);
        assert_eq!(err_str(&out), "-e -E a\tb\n");
        // -E does NOT disable escapes in POSIX mode
        let (_, out, _) = run_env(&env, &["-Ee", "a\\tb"]);
        assert_eq!(err_str(&out), "-Ee a\tb\n");
        // --help/--version never recognized
        let (_, out, _) = run_env(&env, &["--help"]);
        assert_eq!(err_str(&out), "--help\n");
        let (_, out, _) = run_env(&env, &["--version"]);
        assert_eq!(err_str(&out), "--version\n");
        // exact -n works
        let (_, out, _) = run_env(&env, &["-n", "-e", "q\\tr"]);
        assert_eq!(err_str(&out), "q\tr");
    }

    #[test]
    fn posix_empty_value_counts() {
        let (_, out, _) = run_env(&[("POSIXLY_CORRECT", "")], &["a\\tb"]);
        assert_eq!(err_str(&out), "a\tb\n");
    }

    #[test]
    fn utf8_passes_through_with_escapes() {
        // multi-byte text passes through byte-identical; escapes still fire
        let (_, out, _) = run(&["-e", "h\u{e9}llo\\tx"]);
        assert_eq!(out, "h\u{e9}llo\tx\n".as_bytes());
    }
}
