//! `stdbuf` — run COMMAND with modified buffering operations for its standard streams (#1488).
//!
//! Port of `src/commands/stdbuf.cpp` using `childproc::resolve_child` /
//! `build_child_command` and explicit `Io`/`Ctx`.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt::{self, OptSpec};
use crate::childproc::{build_child_command, resolve_child, ChildResolve};
use crate::gnuhelp;
use winux_core::{tr, CmdResult, Ctx, Io};

const RC_STDBUF_FAIL: u32 = 125;

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('i'),
        long: "input",
        takes_value: true,
    },
    OptSpec {
        short: Some('o'),
        long: "output",
        takes_value: true,
    },
    OptSpec {
        short: Some('e'),
        long: "error",
        takes_value: true,
    },
];

const STDBUF_HELP_BODY: &str = r#"Usage: stdbuf OPTION... COMMAND
Run COMMAND, with modified buffering operations for its standard streams.

Mandatory arguments to long options are mandatory for short options too.
  -i, --input=MODE   adjust standard input stream buffering
  -o, --output=MODE  adjust standard output stream buffering
  -e, --error=MODE   adjust standard error stream buffering
      --help        display this help and exit
      --version     output version information and exit

If MODE is 'L' the corresponding stream will be line buffered.
This option is invalid with standard input.

If MODE is '0' the corresponding stream will be unbuffered.

Otherwise MODE is a number which may be followed by one of the following:
KB 1000, K 1024, MB 1000*1000, M 1024*1024, and so on for G, T, P, E, Z, Y, R, Q.
Binary prefixes can be used, too: KiB=K, MiB=M, and so on.
In this case the corresponding stream will be fully buffered with the buffer
size set to MODE bytes.

"#;

fn parse_buffer_suffix(suffix: &str) -> Option<usize> {
    let (base, power): (usize, u32) = match suffix {
        "" => (1, 0),
        "K" | "k" | "KiB" => (1024, 1),
        "KB" | "kB" => (1000, 1),
        "M" | "MiB" => (1024, 2),
        "MB" => (1000, 2),
        "G" | "GiB" => (1024, 3),
        "GB" => (1000, 3),
        "T" | "TiB" => (1024, 4),
        "TB" => (1000, 4),
        "P" | "PiB" => (1024, 5),
        "PB" => (1000, 5),
        "E" | "EiB" => (1024, 6),
        "EB" => (1000, 6),
        "Z" | "ZiB" => (1024, 7),
        "ZB" => (1000, 7),
        "Y" | "YiB" => (1024, 8),
        "YB" => (1000, 8),
        "R" | "RiB" => (1024, 9),
        "RB" => (1000, 9),
        "Q" | "QiB" => (1024, 10),
        "QB" => (1000, 10),
        _ => return None,
    };
    let mut res = 1usize;
    for _ in 0..power {
        res = res.checked_mul(base)?;
    }
    Some(res)
}

fn parse_buffer_size(s: &str) -> Option<usize> {
    if s.is_empty() {
        return None;
    }
    let digits = s.bytes().take_while(|b| b.is_ascii_digit()).count();
    if digits == 0 {
        return parse_buffer_suffix(s);
    }
    let value: usize = s[..digits].parse().ok()?;
    let mult = parse_buffer_suffix(&s[digits..])?;
    value.checked_mul(mult)
}

fn validate_mode(mode: &str) -> Option<String> {
    if mode == "L" {
        return Some("L".to_owned());
    }
    let sz = parse_buffer_size(mode)?;
    Some(if sz == 0 {
        "0".to_owned()
    } else {
        sz.to_string()
    })
}

fn split_at_first_positional(args: &[OsString]) -> usize {
    let mut i = 0usize;
    while i < args.len() {
        let s = args[i].to_string_lossy();
        if s == "--" {
            return args.len();
        }
        if s.starts_with("--") {
            let takes_sep_val = !s.contains('=')
                && matches!(s.as_ref(), "--input" | "--output" | "--error");
            i += if takes_sep_val { 2 } else { 1 };
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let mut consumes_next = false;
            let bytes = s.as_bytes();
            for idx in 1..bytes.len() {
                if matches!(bytes[idx], b'i' | b'o' | b'e') {
                    if idx + 1 == bytes.len() {
                        consumes_next = true;
                    }
                    break;
                }
            }
            i += if consumes_next { 2 } else { 1 };
            continue;
        }
        return i;
    }
    args.len()
}

pub fn stdbuf_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let split = split_at_first_positional(args).min(args.len());
    let mut p = match argopt::parse("stdbuf", &args[..split], OPTS, io, ctx, RC_STDBUF_FAIL) {
        Ok(p) => p,
        Err(rc) => return Ok(rc),
    };
    p.positionals.extend_from_slice(&args[split..]);

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
                    STDBUF_HELP_BODY,
                    gnuhelp::footer("stdbuf", "stdbuf invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("stdbuf", "Padraig Brady")
                )?;
                0
            }
        });
    }

    let in_raw = p.value_of("input");
    let out_raw = p.value_of("output");
    let err_raw = p.value_of("error");

    if let Some(m) = in_raw {
        if m.starts_with('L') {
            let _ = writeln!(io.stderr, "stdbuf: line buffering stdin is meaningless");
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "stdbuf"));
            return Ok(RC_STDBUF_FAIL);
        }
    }

    let mut resolved_i = None;
    let mut resolved_o = None;
    let mut resolved_e = None;

    for (raw, slot) in [
        (in_raw, &mut resolved_i),
        (out_raw, &mut resolved_o),
        (err_raw, &mut resolved_e),
    ] {
        if let Some(m) = raw {
            match validate_mode(m) {
                Some(v) => *slot = Some(v),
                None => {
                    let _ = writeln!(
                        io.stderr,
                        "stdbuf: {}",
                        tr!(
                            ctx,
                            "command.stdbuf.error.invalid_mode",
                            "invalid mode '{mode}'",
                            mode = m
                        )
                    );
                    return Ok(RC_STDBUF_FAIL);
                }
            }
        }
    }

    let Some(command) = p.positionals.first().map(|a| a.to_string_lossy().into_owned()) else {
        let _ = writeln!(
            io.stderr,
            "stdbuf: {}",
            tr!(ctx, "common.error.missing_operand", "missing operand")
        );
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "stdbuf"));
        return Ok(RC_STDBUF_FAIL);
    };

    if resolved_i.is_none() && resolved_o.is_none() && resolved_e.is_none() {
        let _ = writeln!(
            io.stderr,
            "stdbuf: {}",
            tr!(
                ctx,
                "command.stdbuf.error.mode_required",
                "you must specify a buffering mode option"
            )
        );
        let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "stdbuf"));
        return Ok(RC_STDBUF_FAIL);
    }

    let cmd_args: Vec<String> = p.positionals[1..]
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    let mut cmd = match resolve_child(&command, ctx) {
        ChildResolve::Found(launch) => build_child_command(&launch, &cmd_args, ctx),
        ChildResolve::NotFound => {
            let _ = writeln!(
                io.stderr,
                "stdbuf: failed to run command '{command}': No such file or directory"
            );
            return Ok(127);
        }
        ChildResolve::BadFormat => {
            let _ = writeln!(
                io.stderr,
                "stdbuf: failed to run command '{command}': Exec format error"
            );
            return Ok(126);
        }
        ChildResolve::NotExecutable | ChildResolve::NotADirectory => {
            let _ = writeln!(
                io.stderr,
                "stdbuf: failed to run command '{command}': Permission denied"
            );
            return Ok(126);
        }
    };

    if let Some(v) = &resolved_i {
        cmd.env("WINUX_STDBUF_I", v);
        cmd.env("_STDBUF_I", v);
    }
    if let Some(v) = &resolved_o {
        cmd.env("WINUX_STDBUF_O", v);
        cmd.env("_STDBUF_O", v);
    }
    if let Some(v) = &resolved_e {
        cmd.env("WINUX_STDBUF_E", v);
        cmd.env("_STDBUF_E", v);
    }

    match cmd.status() {
        Ok(st) => Ok(st.code().unwrap_or(1) as u32),
        Err(e) => {
            let _ = writeln!(
                io.stderr,
                "stdbuf: failed to run command '{command}': {e}"
            );
            Ok(126)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn validates_modes_and_missing_operands() {
        let sc = ScratchCtx::new("stdbuf-err");

        let mut io = MemIo::new("");
        let rc = stdbuf_cmd(
            &[OsString::from("-i"), OsString::from("L"), OsString::from("echo")],
            &mut io.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc, 125);
        assert!(io.err().contains("line buffering stdin is meaningless"));

        let mut io2 = MemIo::new("");
        let rc2 = stdbuf_cmd(&[OsString::from("echo")], &mut io2.split(), &sc.ctx()).unwrap();
        assert_eq!(rc2, 125);
        assert!(io2.err().contains("buffering mode option"));

        let mut io3 = MemIo::new("");
        let rc3 = stdbuf_cmd(
            &[OsString::from("-o"), OsString::from("bogus"), OsString::from("echo")],
            &mut io3.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc3, 125);
        assert!(io3.err().contains("invalid mode"));
    }

    #[test]
    fn parses_buffer_sizes() {
        assert_eq!(parse_buffer_size("0"), Some(0));
        assert_eq!(parse_buffer_size("00"), Some(0));
        assert_eq!(parse_buffer_size("KiB"), Some(1024));
        assert_eq!(parse_buffer_size("4K"), Some(4096));
        assert_eq!(parse_buffer_size("2KB"), Some(2000));
    }
}
