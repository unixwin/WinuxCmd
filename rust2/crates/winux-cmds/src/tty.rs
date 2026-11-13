//! `tty` — GNU coreutils tty.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-tty.sh).
//!
//! Prints the file name of the terminal connected to standard input:
//! `not a tty` and exit 1 when stdin is not a console (the only
//! Windows-honest reading — GNU isatty(0) is false there too, and the
//! gate probes confirm `tty </dev/null` -> "not a tty", rc 1). A console
//! stdin answers `con` (the 1.x C++ applet's name for the Windows
//! console; GNU would print the tty device path, which Windows does not
//! have — recorded divergence, stdout byte-gated on the not-a-tty side
//! only in the gate).
//!
//! Exit status: 0 when stdin is a terminal, 1 when it is not, 2 usage
//! error (probed: `tty extra` -> rc 2), 3 write failure on stdout (GNU
//! documents 3 for "write error"; the driver maps a broken stdout handle
//! to 125 before this command can observe it, so 3 is unreachable here
//! and documented as such).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('s'),
        long: "silent",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "quiet",
        takes_value: false,
    },
];

// `--help` body verbatim from `env tty --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail (probed w5-tty.sh case 14).
const TTY_HELP_BODY: &str = r#"Usage: tty [OPTION]...
Print the file name of the terminal connected to standard input.

  -s, --silent, --quiet   print nothing, only return an exit status
      --help        display this help and exit
      --version     output version information and exit

"#;

/// True when the injected stdin handle is a console (tty.c's isatty(0)).
///
/// The process-console probe is the one piece of state the Io bundle
/// cannot carry (a `dyn Read` has no handle identity), so this reads the
/// CRT stdin handle directly — exactly the tier the 1.x C++ applet uses
/// (tty.cpp GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE))). In the gate
/// the driver's stdin is redirected on every side, so every gate row
/// observes the not-a-tty branch.
#[cfg(windows)]
fn stdin_is_console() -> bool {
    #[link(name = "kernel32")]
    extern "system" {
        fn GetStdHandle(which: u32) -> *mut core::ffi::c_void;
        fn GetConsoleMode(handle: *mut core::ffi::c_void, mode: *mut u32) -> i32;
    }
    const STD_INPUT_HANDLE: u32 = 0xFFFF_FFF6u32 as u32;
    let mut mode: u32 = 0;
    unsafe { GetConsoleMode(GetStdHandle(STD_INPUT_HANDLE), &mut mode) != 0 }
}

#[cfg(not(windows))]
fn stdin_is_console() -> bool {
    false
}

pub fn tty_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("tty", args, OPTS, io, ctx, 2) {
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
                    TTY_HELP_BODY,
                    gnuhelp::footer("tty", "tty invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("tty", "David MacKenzie")
                )?;
                0
            }
        });
    }

    // Probed (w5-tty.sh case 5): a single extra operand is the usage
    // diagnostic with exit status 2 (tty.c's main uses usage(EXIT_FAILURE)
    // after quotef'ing the operand). GNU's diagnostic quotes with Unicode
    // quotes; stderr wording is recorded by the gate, not gated.
    if let Some(extra) = p.positionals.first() {
        let _ = winux_core::usage_error(
            io,
            "tty",
            &format!("extra operand '{}'", extra.to_string_lossy()),
        );
        return Ok(2);
    }

    let silent = p.has("silent") || p.has("quiet");
    let is_tty = stdin_is_console();
    if silent {
        return Ok(if is_tty { 0 } else { 1 });
    }
    if is_tty {
        writeln!(io.stdout, "con")?;
        Ok(0)
    } else {
        writeln!(io.stdout, "not a tty")?;
        Ok(1)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("tty");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = tty_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    // The unit-test process's stdin is never a console (cargo test pipes
    // it), so the not-a-tty branch is deterministic here — the same branch
    // every gate row observes.

    #[test]
    fn not_a_tty_default_and_status() {
        let (r, out, err) = run(&[], "");
        assert_eq!(r, 1);
        assert_eq!(out, "not a tty\n");
        assert!(err.is_empty());
    }

    #[test]
    fn silent_prints_nothing_rc_only() {
        let (r, out, err) = run(&["-s"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(err.is_empty());
        let (r, out, _) = run(&["--silent"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "");
        let (r, out, _) = run(&["--quiet"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "");
    }

    #[test]
    fn extra_operand_is_usage_status_2() {
        // Probed: `tty extra` -> "tty: extra operand 'extra'", rc 2.
        let (r, out, err) = run(&["extra"], "");
        assert_eq!(r, 2);
        assert!(out.is_empty());
        assert!(err.contains("tty: extra operand 'extra'"));
        assert!(err.contains("Try 'tty --help' for more information."));
    }

    #[test]
    fn dashdash_stops_options_then_operand_rules() {
        // Probed: `tty --` with no operand behaves like plain tty (rc 1,
        // "not a tty"); `tty -` reports the dash as an extra operand.
        let (r, out, _) = run(&["--"], "");
        assert_eq!(r, 1);
        assert_eq!(out, "not a tty\n");
        let (r, _, err) = run(&["-"], "");
        assert_eq!(r, 2);
        assert!(err.contains("extra operand '-'"));
    }

    #[test]
    fn bogus_options_are_usage_errors() {
        // Probed: `tty --bogus` rc 2, `tty -z` rc 2 (both stderr-only).
        let (r, out, err) = run(&["--bogus"], "");
        assert_eq!(r, 2);
        assert!(out.is_empty());
        assert!(err.contains("unrecognized option '--bogus'"));
        let (r, _, err) = run(&["-z"], "");
        assert_eq!(r, 2);
        assert!(err.contains("invalid option -- 'z'"));
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: tty [OPTION]...\n"));
        assert!(
            out.contains("  -s, --silent, --quiet   print nothing, only return an exit status\n")
        );
        assert!(out.ends_with("info '(coreutils) tty invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("tty (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie.\n"));
    }

    #[test]
    fn help_short_circuits_later_operands() {
        // #1573 sequential contract: the help value ends the scan.
        let (r, out, _) = run(&["--help", "extra"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: tty"));
    }

    #[test]
    fn stdin_ignored_on_success_surface() {
        // The command never reads stdin bytes; a pending buffer stays.
        let (r, out, _) = run(&[], "unused stdin content");
        assert_eq!(r, 1);
        assert_eq!(out, "not a tty\n");
    }
}
