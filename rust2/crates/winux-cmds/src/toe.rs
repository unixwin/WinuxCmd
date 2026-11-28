//! `toe` and `tic` — ncurses terminfo utilities on Windows, ported for WinuxCmd 2.0 (#1488).
//!
//! Pure std-only implementations with explicit `Io`/`Ctx` and zero globals.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const TOE_HELP: &str = "\
Usage: toe [-ahsuUV] [-v n] [file...]
Table of terminfo entries.

  -h  list with a heading
  -V  print version
";

const TIC_HELP: &str = "\
Usage: tic [-e names] [-o dir] [-R name] [-v[n]] [-V] [-w[n]] [-1aCDcfGgIKLNrsTtUx] source-file
Terminfo compiler.
";

pub fn toe_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut heading = false;
    let mut no_more_opts = false;
    let mut i = 0usize;

    while i < args.len() {
        let s = args[i].to_string_lossy();
        if no_more_opts {
            i += 1;
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            i += 1;
            continue;
        }
        if s == "--help" {
            io.stdout.write_all(TOE_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("toe", "WinuxCmd")
            )?;
            return Ok(0);
        }
        if s == "-V" {
            writeln!(io.stdout, "ncurses 6.6.20251230")?;
            return Ok(0);
        }
        if s == "-h" {
            heading = true;
            i += 1;
            continue;
        }
        if matches!(s.as_ref(), "-a" | "-s" | "-u" | "-U" | "-v") {
            writeln!(
                io.stderr,
                "toe: option {s} is not supported on Windows (terminfo database unavailable)"
            )?;
            return Ok(1);
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "toe", &msg);
            return Ok(1);
        }
        i += 1;
    }

    if heading {
        writeln!(io.stdout, "Terminfo Entries")?;
    }
    writeln!(io.stdout, "windows-ansi Windows ANSI/VT console")?;
    writeln!(io.stdout, "xterm        xterm terminal emulator")?;
    writeln!(io.stdout, "vt100        DEC VT100")?;
    writeln!(io.stdout, "ansi         ANSI/VT100 terminal")?;
    Ok(0)
}

pub fn tic_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if no_more_opts {
            positionals.push(s.into_owned());
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            continue;
        }
        if s == "--help" || s == "-h" {
            io.stdout.write_all(TIC_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("tic", "WinuxCmd")
            )?;
            return Ok(0);
        }
        if s == "-V" {
            writeln!(io.stdout, "ncurses 6.6.20251230")?;
            return Ok(0);
        }
        if matches!(s.as_ref(), "-a" | "-c" | "-o" | "-v" | "-x") {
            writeln!(
                io.stderr,
                "tic: option {s} is not supported on Windows (terminfo compiler unavailable)"
            )?;
            return Ok(1);
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "tic", &msg);
            return Ok(1);
        }
        positionals.push(s.into_owned());
    }

    if positionals.is_empty() {
        writeln!(io.stderr, "tic: File name needed.  Usage:")?;
        writeln!(
            io.stderr,
            "\ttic [-e names] [-o dir] [-R name] [-v[n]] [-V] [-w[n]] [-1aCDcfGgIKLNrsTtUx] source-file"
        )?;
        return Ok(1);
    }

    writeln!(
        io.stderr,
        "tic: terminfo compilation not supported on Windows"
    )?;
    Ok(1)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn toe_lists_entries_and_heading() {
        let sc = ScratchCtx::new("toe-test");
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let rc = toe_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert!(io.out().contains("windows-ansi"));
        assert!(io.out().contains("xterm"));

        let mut io_h = MemIo::new("");
        let rc_h = toe_cmd(&[OsString::from("-h")], &mut io_h.split(), &ctx).unwrap();
        assert_eq!(rc_h, 0);
        assert!(io_h.out().starts_with("Terminfo Entries\n"));

        let mut io_v = MemIo::new("");
        let rc_v = toe_cmd(&[OsString::from("-V")], &mut io_v.split(), &ctx).unwrap();
        assert_eq!(rc_v, 0);
        assert_eq!(io_v.out(), "ncurses 6.6.20251230\n");

        let mut io_u = MemIo::new("");
        let rc_u = toe_cmd(&[OsString::from("-a")], &mut io_u.split(), &ctx).unwrap();
        assert_eq!(rc_u, 1);
        assert!(io_u.err().contains("not supported on Windows"));
    }

    #[test]
    fn tic_missing_file_and_unsupported_compile() {
        let sc = ScratchCtx::new("tic-test");
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let rc = tic_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("File name needed"));

        let mut io2 = MemIo::new("");
        let rc2 = tic_cmd(&[OsString::from("myterm.ti")], &mut io2.split(), &ctx).unwrap();
        assert_eq!(rc2, 1);
        assert!(io2.err().contains("not supported on Windows"));
    }
}
