//! `nohup` — GNU coreutils nohup semantics on the Windows process model
//! (probe base: coreutils 9.4, target/probe/g1.sh; C++ value model:
//! `src/commands/nohup.cpp` on main).
//!
//! GNU contract (nohup.c:115-199):
//! * `nohup COMMAND [ARG]...`; a missing operand prints "nohup: missing
//!   operand" plus the try-help line and exits **125** — or **127** when
//!   POSIXLY_CORRECT is set non-empty (probed; C++ nohup_usage_error_
//!   exit_code).
//! * The child is resolved execvp-style (PATH-only, PATHEXT completion,
//!   batch files through COMSPEC — the shared [`crate::childproc`]
//!   resolver, extracted from timeout.rs) and spawned with inherited
//!   stdio, the caller's environment and cwd. Failure to resolve:
//!   "nohup: failed to run command 'C': No such file or directory" rc 127
//!   (probed GNU-equal wording); other spawn failures are rc 126.
//! * The child's exit status is the run's status; a child killed without
//!   a code maps to 125.
//! * GNU has no -h short option: `-h` is "invalid option -- 'h'" rc 1
//!   (argopt H_REJECT_MARKER); --help/--version print the probed GNU
//!   text.
//!
//! Windows value model / documented deferral:
//! * The terminal faces (stdin -> /dev/null, stdout -> append nohup.out
//!   with the "ignoring input and appending output to 'nohup.out'"
//!   notices, stderr -> stdout) require observing console-ness of the
//!   caller's stdio. The library shape contract forbids process stdio
//!   access (winux-core: "the only place process stdio is touched is the
//!   CLI driver"), and the injected Io handles carry no console fact, so
//!   this port runs with the faces OFF. In the differential gate both
//!   sides run under redirected pipes/files, where GNU nohup takes the
//!   same no-redirect path, so every gate row is unaffected; the wiring
//!   wave (REPLACEMENT-RECIPE.md) must supply a driver-side console probe
//!   before this command replaces the C++ applet for interactive use.
//!   The C++ applet additionally prints "nohup: ignoring input" when
//!   neither output stream needed redirection; that notice belongs to the
//!   same deferred faces.

use std::ffi::OsString;
use std::io::Write as _;
use std::os::windows::process::CommandExt;

use crate::argopt::{self, OptSpec};
use crate::childproc::{quote_windows_arg, resolve_child, ChildResolve};
use crate::gnuhelp;
use winux_core::{win32_env_get, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    // GNU nohup has no options at all besides --help/--version; -h is
    // rejected like getopt_long does.
    argopt::H_REJECT_SPEC,
];

const NOHUP_HELP_BODY: &str = r#"Usage: nohup COMMAND [ARG]...
  or:  nohup OPTION
Run COMMAND, ignoring hangup signals.

      --help        display this help and exit
      --version     output version information and exit

If standard input is a terminal, redirect it from an unreadable file.
If standard output is a terminal, append output to 'nohup.out' if possible,
'$HOME/nohup.out' otherwise.
If standard error is a terminal, redirect it to standard output.
To save output to FILE, use 'nohup COMMAND > FILE'.

NOTE: your shell may have its own version of nohup, which usually supersedes
the version described here.  Please refer to your shell's documentation
for details about the options it supports.

Exit status:
  125  if the nohup command itself fails
  126  if COMMAND is found but cannot be invoked
  127  if COMMAND cannot be found
  -    the exit status of COMMAND otherwise

"#;

fn usage_exit_status(ctx: &Ctx) -> u32 {
    // GNU nohup.c: POSIXLY_CORRECT promotes the usage exit 125 to 127;
    // any non-NULL value counts, the empty string included (probed:
    // POSIXLY_CORRECT= nohup -> rc 127).
    match ctx.env.get("POSIXLY_CORRECT") {
        Some(_) => 127,
        None => 125,
    }
}

pub fn nohup_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // GNU nohup.c compiles getopt with optstring "+" — NO permutation: only
    // LEADING options are parsed, and the first operand ends option
    // processing (probed: `nohup true --bogus` runs true rc 0, while
    // `nohup --bogus true` dies with usage 125). Split the leading option
    // segment here and hand only that to argopt; everything after the
    // first operand (or after `--`) is the command verbatim.
    let mut opt_len = args.len();
    for (i, a) in args.iter().enumerate() {
        let s = a.to_string_lossy();
        if s == "--" {
            // GNU getopt consumes the `--`; operands resume after it.
            opt_len = i;
            break;
        }
        if s == "-" || !s.starts_with('-') {
            opt_len = i;
            break;
        }
    }
    let (leading, command_args): (Vec<OsString>, Vec<OsString>) = {
        let split = opt_len.min(args.len());
        // when the scan stopped on "--", drop it from the operands
        let consumed_dashdash = split < args.len() && args[split].to_string_lossy() == "--";
        let operands_from = if consumed_dashdash { split + 1 } else { split };
        (args[..split].to_vec(), args[operands_from..].to_vec())
    };
    let p = match argopt::parse("nohup", &leading, OPTS, io, ctx, usage_exit_status(ctx)) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(status) = argopt::rejected_short_h(io, "nohup", &p, usage_exit_status(ctx)) {
        return Ok(status);
    }
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
                    NOHUP_HELP_BODY,
                    gnuhelp::footer("nohup", "nohup invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("nohup", "Jim Meyering")
                )?;
                0
            }
        });
    }

    if command_args.is_empty() {
        let _ = writeln!(io.stderr, "nohup: missing operand");
        let _ = writeln!(io.stderr, "Try 'nohup --help' for more information.");
        return Ok(usage_exit_status(ctx));
    }

    let command = command_args[0].to_string_lossy().into_owned();
    let cmd_args: Vec<String> = command_args[1..]
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    let mut cmd = match resolve_child(&command, ctx) {
        ChildResolve::Found { path, batch } => {
            if batch {
                // CreateProcess cannot launch a batch file directly: route
                // through COMSPEC like the childproc helper does.
                let comspec = win32_env_get(ctx.env, "COMSPEC")
                    .map(|v| v.to_string_lossy().into_owned())
                    .filter(|v| !v.is_empty())
                    .unwrap_or_else(|| "cmd.exe".to_string());
                let mut inner = quote_windows_arg(&path);
                for a in &cmd_args {
                    inner.push(' ');
                    inner.push_str(&quote_windows_arg(a));
                }
                let mut c = std::process::Command::new(&comspec);
                c.raw_arg("/d").raw_arg("/s").raw_arg("/c");
                c.raw_arg(format!("\"{inner}\""));
                c
            } else {
                let mut c = std::process::Command::new(&path);
                c.args(&cmd_args);
                c
            }
        }
        ChildResolve::NotFound => {
            let _ = writeln!(
                io.stderr,
                "nohup: failed to run command '{command}': No such file or directory"
            );
            return Ok(127);
        }
        ChildResolve::NotExecutable | ChildResolve::NotADirectory => {
            let _ = writeln!(
                io.stderr,
                "nohup: failed to run command '{command}': Permission denied"
            );
            return Ok(126);
        }
    };

    // GetEnvironmentStringsW carries "=C:" style pseudo-vars that
    // Command::envs rejects; skip them (the timeout port does the same).
    cmd.envs(
        ctx.env
            .entries()
            .iter()
            .filter(|(k, _)| !k.is_empty() && !k.to_string_lossy().starts_with('='))
            .map(|(k, v)| (k.clone(), v.clone())),
    );
    cmd.current_dir(&ctx.cwd);

    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(_) => {
            let _ = writeln!(
                io.stderr,
                "nohup: failed to run command '{command}': Permission denied"
            );
            return Ok(126);
        }
    };
    match child.wait() {
        Ok(status) => Ok(status.code().map(|c| c.max(0) as u32).unwrap_or(125)),
        Err(_) => Ok(125),
    }
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};
    use winux_core::{Ctx, MapEnv};

    fn run_with(env: &MapEnv, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("nohup");
        let ctx = Ctx {
            cwd: sc.dir.clone(),
            env,
        };
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::nohup_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn run(args: &[&str]) -> (u32, String, String) {
        run_with(&MapEnv::new(), args)
    }

    #[test]
    fn missing_operand_and_posixly_correct() {
        let (r, _, err) = run(&[]);
        assert_eq!(r, 125);
        assert!(err.contains("nohup: missing operand"));
        assert!(err.contains("Try 'nohup --help' for more information."));
        let mut env = MapEnv::new();
        env.set("POSIXLY_CORRECT", "1");
        let (r, _, _) = run_with(&env, &[]);
        assert_eq!(r, 127);
        // the empty value counts as set too (GNU probed rc 127)
        let mut env = MapEnv::new();
        env.set("POSIXLY_CORRECT", "");
        let (r, _, _) = run_with(&env, &[]);
        assert_eq!(r, 127);
    }

    #[test]
    fn child_status_passthrough() {
        // cmd resolves from System32; /c exit N propagates N
        let (r, _, _) = run(&["cmd", "/c", "exit", "7"]);
        assert_eq!(r, 7);
        let (r, _, _) = run(&["cmd", "/c", "exit", "0"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn child_stdout_reaches_the_process_sink() {
        // A spawned child inherits the PROCESS stdio (Stdio::inherit), not
        // the injected buffers — the same property timeout.rs documents.
        // In the gate the driver's process stdout IS the .out artifact, so
        // child output still lands in the compared bytes; here we can only
        // assert the child ran cleanly.
        let (r, _, _) = run(&["cmd", "/d", "/s", "/c", "echo hi"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn not_found_and_not_executable() {
        let (r, _, err) = run(&["nosuchcmd123-xyz"]);
        assert_eq!(r, 127);
        assert!(err.contains(
            "nohup: failed to run command 'nosuchcmd123-xyz': No such file or directory"
        ));
        // a directory operand: 126 NotADirectory -> Permission denied
        let (r, _, err) = run(&["C:\\\\"]);
        assert_eq!(r, 126);
        assert!(err.contains("failed to run command"));
    }

    #[test]
    fn plus_optstring_no_permutation() {
        // GNU nohup optstring "+": the first operand ends option parsing.
        // `nohup true --bogus` runs true (rc 0); `nohup --bogus true` dies
        // with usage 125; `nohup -5 true` invalid option 125; `nohup --
        // ...` consumes the dashdash.
        // operands after the first are verbatim (no usage error); what the
        // child does with them is its business — only rc 125 is wrong here
        let (r, _, err) = run(&["cmd", "/c", "exit", "0", "--bogus"]);
        assert_ne!(r, 125, "err={err}");
        let (r, _, err) = run(&["--bogus", "cmd", "/c", "exit", "0"]);
        assert_eq!(r, 125);
        assert!(err.contains("unrecognized option '--bogus'"));
        let (r, _, err) = run(&["-5", "cmd", "/c", "exit", "0"]);
        assert_eq!(r, 125);
        assert!(err.contains("invalid option -- '5'"));
        let (r, _, _) = run(&["--", "cmd", "/c", "exit", "3"]);
        assert_eq!(r, 3);
        let (r, _, _) = run(&["--"]);
        assert_eq!(r, 125);
        // --help/--version are leading-option surfaces (GNU probed:
        // `nohup --version extra` prints version rc 0)
        let (r, out, _) = run(&["--version", "extra"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("nohup (GNU coreutils) 9.4"));
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, _) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: nohup COMMAND [ARG]...\n"));
        assert!(out.contains("NOTE: your shell may have its own version of nohup"));
        assert!(out.contains("Exit status:\n  125  if the nohup command itself fails"));
        assert!(out.ends_with("info '(coreutils) nohup invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("nohup (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Jim Meyering.\n"));
        // GNU nohup has no -h; the usage exit is 125 like other option
        // errors (nohup.c usage() -> USED_EXIT_STATUS)
        let (r, _, err) = run(&["-h"]);
        assert_eq!(r, 125);
        assert!(err.contains("invalid option -- 'h'"));
    }
}
