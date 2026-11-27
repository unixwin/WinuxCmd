//! `reset` — ncurses tset/reset semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Emits `\x1bc` (and `\x1b[?25h\x1b[0m` under `-I`) to `io.stderr` (#1462),
//! leaving `io.stdout` clean for `-q` and `-s` (`TERM=...;` / csh `setenv`).
//! Uses `ctx.env` for `TERM` and `SHELL` with zero process globals.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const RESET_HELP: &str = "\
Usage: reset [options] [terminal]
Reset terminal to its default state.

  -I          emit initialization strings
  -q          display terminal type only, do not reset
  -Q          do not output control key settings
  -r          display terminal type on stderr
  -s          output TERM set command
  -V          print curses-version
  -h, --help  display this help and exit
";

pub fn reset_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut print_curses_ver = false;
    let mut quiet = false;
    let mut initialize = false;
    let mut shell_output = false;
    let mut report_stderr = false;
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;
    let mut i = 0;

    while i < args.len() {
        let s = args[i].to_string_lossy();
        if no_more_opts {
            positionals.push(s.into_owned());
            i += 1;
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            i += 1;
            continue;
        }
        if s == "-h" || s == "--help" {
            io.stdout.write_all(RESET_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("reset", "WinuxCmd")
            )?;
            return Ok(0);
        }
        if s == "-V" {
            print_curses_ver = true;
            i += 1;
            continue;
        }
        if s == "-q" {
            quiet = true;
            i += 1;
            continue;
        }
        if s == "-I" {
            initialize = true;
            i += 1;
            continue;
        }
        if s == "-s" {
            shell_output = true;
            i += 1;
            continue;
        }
        if s == "-r" {
            report_stderr = true;
            i += 1;
            continue;
        }
        if s == "-Q" || s == "-c" || s == "-w" {
            i += 1;
            continue;
        }
        if matches!(s.as_ref(), "-e" | "-i" | "-k" | "-m") {
            // Options that take an argument in ncurses reset; skip value if present.
            if i + 1 < args.len() {
                i += 2;
            } else {
                i += 1;
            }
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "reset", &msg);
            return Ok(1);
        }
        positionals.push(s.into_owned());
        i += 1;
    }

    if print_curses_ver {
        writeln!(io.stdout, "ncurses 6.6.20251230")?;
        return Ok(0);
    }

    let term = if let Some(first) = positionals.first() {
        first.clone()
    } else if let Some(env_term) = ctx.env.get("TERM") {
        env_term.to_string_lossy().into_owned()
    } else {
        "xterm".to_owned()
    };

    if quiet {
        writeln!(io.stdout, "{term}")?;
        return Ok(0);
    }

    io.stderr.write_all(b"\x1bc")?;
    if initialize {
        io.stderr.write_all(b"\x1b[?25h\x1b[0m")?;
    }
    if report_stderr {
        writeln!(io.stderr, "Terminal type is {term}.")?;
    }
    if shell_output {
        let shell = ctx
            .env
            .get("SHELL")
            .map(|v| v.to_string_lossy().into_owned())
            .unwrap_or_default();
        if shell.ends_with("csh") {
            writeln!(io.stdout, "set noglob;")?;
            writeln!(io.stdout, "setenv TERM {term};")?;
            writeln!(io.stdout, "unset noglob;")?;
        } else {
            writeln!(io.stdout, "TERM={term};")?;
        }
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_reset(args: &[&str], env: &[(&str, &str)]) -> (u32, String, String) {
        let mut sc = ScratchCtx::new("reset-test");
        for (k, v) in env {
            sc.env.set(k, v);
        }
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = reset_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn basic_quiet_report_and_shell_modes() {
        let (rc, out, err) = run_reset(&[], &[]);
        assert_eq!(rc, 0);
        assert!(out.is_empty());
        assert_eq!(err, "\x1bc");

        let (rc, out, err) = run_reset(&["-q", "vt100"], &[]);
        assert_eq!(rc, 0);
        assert_eq!(out, "vt100\n");
        assert!(err.is_empty());

        let (rc, out, err) = run_reset(&["-r", "vt100"], &[]);
        assert_eq!(rc, 0);
        assert!(out.is_empty());
        assert!(err.contains("\x1bc"));
        assert!(err.contains("Terminal type is vt100."));

        let (rc, out, _) = run_reset(&["-s", "vt100"], &[("SHELL", "/bin/sh")]);
        assert_eq!(rc, 0);
        assert_eq!(out, "TERM=vt100;\n");

        let (rc, out, _) = run_reset(&["-s"], &[("TERM", "tmux"), ("SHELL", "/bin/tcsh")]);
        assert_eq!(rc, 0);
        assert_eq!(out, "set noglob;\nsetenv TERM tmux;\nunset noglob;\n");
    }

    #[test]
    fn initialize_and_version() {
        let (rc, _, err) = run_reset(&["-I"], &[]);
        assert_eq!(rc, 0);
        assert_eq!(err, "\x1bc\x1b[?25h\x1b[0m");

        let (rc, out, _) = run_reset(&["-V"], &[]);
        assert_eq!(rc, 0);
        assert_eq!(out, "ncurses 6.6.20251230\n");
    }
}
