//! `runcon` — GNU coreutils runcon.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-runcon.sh).
//!
//! Honest boundary, documented up front: Windows has no SELinux, so
//! process security contexts cannot be read or set. The probed GNU
//! non-SELinux host behavior is the oracle this port follows:
//! * bare `runcon` (no CONTEXT, no COMMAND) prints the CURRENT security
//!   context — the oracle prints whatever getcon(3) returns (here the
//!   string `kernel`). Windows has no current context; the port reports
//!   the 1.x applet's not-supported diagnostic and rc 1 (win-mode row in
//!   the gate; GNU prints `kernel`, rc 0).
//! * a CONTEXT without a COMMAND is an argument error before anything
//!   SELinux: `runcon: no command specified` + try-help, rc 125 (probed
//!   for both `runcon true` and `runcon ctx`).
//! * a COMMAND (optionally after -c/-u/-r/-t/-l or a leading CONTEXT)
//!   hits the kernel check: `runcon: runcon may be used only on a
//!   SELinux kernel`, rc 125 (probed for -t and -c -u forms).
//! * option errors exit 125 too (runcon.c maps usage failures to the
//!   125 "runcon itself failed" status — probed `--bogus` rc 125).
//! Since the SELinux-kernel branch is the only reachable terminal state
//! on this platform, rc 125 + the kernel message IS the honest port, not
//! a shim: it is byte-identical in stdout (empty) and status to GNU on
//! every non-SELinux host.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('c'),
        long: "compute",
        takes_value: false,
    },
    OptSpec {
        short: Some('u'),
        long: "user",
        takes_value: true,
    },
    OptSpec {
        short: Some('r'),
        long: "role",
        takes_value: true,
    },
    OptSpec {
        short: Some('t'),
        long: "type",
        takes_value: true,
    },
    OptSpec {
        short: Some('l'),
        long: "range",
        takes_value: true,
    },
];

// `--help` body verbatim from `env runcon --help` under WSL coreutils 9.4
// (including the Exit status section).
const RUNCON_HELP_BODY: &str = r#"Usage: runcon CONTEXT COMMAND [args]
  or:  runcon [ -c ] [-u USER] [-r ROLE] [-t TYPE] [-l RANGE] COMMAND [args]
Run a program in a different SELinux security context.
With neither CONTEXT nor COMMAND, print the current security context.

Mandatory arguments to long options are mandatory for short options too.
  CONTEXT            Complete security context
  -c, --compute      compute process transition context before modifying
  -t, --type=TYPE    type (for same role as parent)
  -u, --user=USER    user identity
  -r, --role=ROLE    role
  -l, --range=RANGE  levelrange
      --help        display this help and exit
      --version     output version information and exit

Exit status:
  125  if the runcon command itself fails
  126  if COMMAND is found but cannot be invoked
  127  if COMMAND cannot be found
  -    the exit status of COMMAND otherwise

"#;

/// Exit status for "the runcon command itself fails" (runcon --help).
const RC_SELINUX_FAIL: u32 = 125;

pub fn runcon_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("runcon", args, OPTS, io, ctx, RC_SELINUX_FAIL) {
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
                    RUNCON_HELP_BODY,
                    gnuhelp::footer("runcon", "runcon invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("runcon", "Russell Coker")
                )?;
                0
            }
        });
    }

    let has_context_modifiers = p.flags.iter().any(|f| f == "compute")
        || p.values
            .iter()
            .any(|(k, _)| matches!(k.as_str(), "user" | "role" | "type" | "range"));

    // runcon.c main flow, argument-shape part (probed): an invocation
    // with NO positional prints the current security context — modifiers
    // alone (`runcon -t type_t`) do NOT demand a command, they print the
    // (modified) current context too, rc 0. The "no command specified"
    // error only fires when a CONTEXT positional lacks a COMMAND.
    if p.positionals.is_empty() {
        // Honest boundary: Windows has no SELinux current context (see
        // module head). The 1.x applet's diagnostic stands in for GNU's
        // `getcon` result; GNU on the gate host prints `kernel`, rc 0.
        let _ = writeln!(
            io.stderr,
            "runcon: failed to get current context: SELinux is not supported on Windows"
        );
        return Ok(1);
    }
    if !has_context_modifiers && p.positionals.len() < 2 {
        // the only positional was a CONTEXT: no COMMAND given
        let _ = winux_core::usage_error(io, "runcon", "no command specified");
        return Ok(RC_SELINUX_FAIL);
    }

    // A COMMAND is present: on any non-SELinux kernel GNU stops here
    // (probed rc 125, empty stdout; stderr wording recorded only).
    let _ = writeln!(
        io.stderr,
        "runcon: runcon may be used only on a SELinux kernel"
    );
    Ok(RC_SELINUX_FAIL)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("runcon");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = runcon_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn context_without_command_is_125() {
        // probed: `runcon ctx` and `runcon true` both -> "no command
        // specified" + try-help, rc 125 (the lone positional is a CONTEXT)
        for a in [vec!["ctx"], vec!["true"]] {
            let (r, out, err) = run(&a);
            assert_eq!(r, 125, "{a:?}");
            assert!(out.is_empty());
            assert!(err.contains("runcon: no command specified"));
            assert!(err.contains("Try 'runcon --help' for more information."));
        }
    }

    #[test]
    fn command_hits_selinux_kernel_branch() {
        // probed: -t and -c -u forms -> kernel message rc 125, stdout empty
        let (r, out, err) = run(&["-t", "type_t", "true"]);
        assert_eq!(r, 125);
        assert!(out.is_empty());
        assert!(err.contains("runcon: runcon may be used only on a SELinux kernel"));
        let (r, out, err) = run(&["-c", "-u", "u", "true"]);
        assert_eq!(r, 125);
        assert!(out.is_empty());
        assert!(err.contains("runcon: runcon may be used only on a SELinux kernel"));
        let (r, out, err) = run(&["ctx", "true"]);
        assert_eq!(r, 125);
        assert!(out.is_empty());
        assert!(err.contains("runcon: runcon may be used only on a SELinux kernel"));
    }

    #[test]
    fn bare_invocation_boundary() {
        // win-mode row: GNU prints the getcon() context ("kernel"), rc 0;
        // the port reports the missing-context diagnostic, rc 1. Modifiers
        // alone land here too (probed: `runcon -t type_t` prints "kernel",
        // rc 0 — no command demanded without a CONTEXT positional).
        let (r, out, err) = run(&[]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("runcon: failed to get current context"));
        let (r, out, _) = run(&["-t", "type_t"]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
    }

    #[test]
    fn option_errors_exit_125() {
        // probed: usage failures map to 125, not 1
        let (r, out, err) = run(&["--bogus"]);
        assert_eq!(r, 125);
        assert!(out.is_empty());
        assert!(err.contains("unrecognized option '--bogus'"));
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: runcon CONTEXT COMMAND [args]\n"));
        assert!(
            out.contains("With neither CONTEXT nor COMMAND, print the current security context.\n")
        );
        assert!(out.contains("  125  if the runcon command itself fails\n"));
        assert!(out.ends_with("info '(coreutils) runcon invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("runcon (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Russell Coker.\n"));
    }

    #[test]
    fn help_short_circuits_operands() {
        // #1573: help before operand validation
        let (r, out, _) = run(&["--help", "ctx"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: runcon"));
    }

    #[test]
    fn value_forms_of_modifiers() {
        // --user=U / --type=T (long value forms) reach the same branch
        let (r, _, err) = run(&["--type=type_t", "true"]);
        assert_eq!(r, 125);
        assert!(err.contains("SELinux kernel"));
        // modifiers with no positional print the current context (win-mode
        // boundary, probed "kernel" rc 0)
        let (r, _, _) = run(&["--user=u"]);
        assert_eq!(r, 1);
    }
}
