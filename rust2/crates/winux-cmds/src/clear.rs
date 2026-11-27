//! `clear` — ncurses clear semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Emits `\x1b[H\x1b[2J` plus `\x1b[3J` (unless `-x` is set, #1462) to
//! `io.stdout`, with `-V` printing `ncurses 6.6.20251230`.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const CLEAR_HELP: &str = "\
Usage: clear [OPTION]
Clear the terminal screen.

  -x          do not try to clear scrollback
  -V          print curses-version
  -h, --help  display this help and exit
";

pub fn clear_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut no_scrollback = false;
    let mut print_curses_ver = false;
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if !no_more_opts {
            if s == "--" {
                no_more_opts = true;
                continue;
            }
            if s == "-h" || s == "--help" {
                io.stdout.write_all(CLEAR_HELP.as_bytes())?;
                return Ok(0);
            }
            if s == "--version" {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("clear", "WinuxCmd")
                )?;
                return Ok(0);
            }
            if s == "-V" {
                print_curses_ver = true;
                continue;
            }
            if s == "-x" {
                no_scrollback = true;
                continue;
            }
            if s.starts_with('-') && s.len() > 1 {
                let msg = if let Some(rest) = s.strip_prefix("--") {
                    argopt::unrecognized_option(ctx, &format!("--{rest}"))
                } else {
                    let c = s.chars().nth(1).unwrap();
                    argopt::invalid_option(ctx, &c.to_string())
                };
                let _ = usage_error(ctx, io, "clear", &msg);
                return Ok(1);
            }
        }
    }

    if print_curses_ver {
        writeln!(io.stdout, "ncurses 6.6.20251230")?;
        return Ok(0);
    }

    io.stdout.write_all(b"\x1b[H\x1b[2J")?;
    if !no_scrollback {
        io.stdout.write_all(b"\x1b[3J")?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_clear(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("clear-test");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = clear_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn xterm_sequence_and_x_flag() {
        let (rc, out, err) = run_clear(&[]);
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert_eq!(out, "\x1b[H\x1b[2J\x1b[3J");

        let (rc, out, _) = run_clear(&["-x"]);
        assert_eq!(rc, 0);
        assert_eq!(out, "\x1b[H\x1b[2J");

        let (rc, out, _) = run_clear(&["-V"]);
        assert_eq!(rc, 0);
        assert_eq!(out, "ncurses 6.6.20251230\n");
    }

    #[test]
    fn invalid_option_rejected() {
        let (rc, _, err) = run_clear(&["--bogus"]);
        assert_eq!(rc, 1);
        assert!(err.contains("unrecognized option '--bogus'"));
    }
}
