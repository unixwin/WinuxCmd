//! `nohup` — GNU coreutils nohup semantics on the Windows process model
//! (probe base: coreutils 9.4, target/probe/g1.sh + #1587 probes; C++
//! value model: `src/commands/nohup.cpp` on main).
//!
//! GNU contract (nohup.c:115-199):
//! * `nohup COMMAND [ARG]...`; a missing operand prints "nohup: missing
//!   operand" plus the try-help line and exits **125** — or **127** when
//!   POSIXLY_CORRECT is set non-empty (probed; C++ nohup_usage_error_
//!   exit_code).
//! * The child is resolved execvp-style (PATH-only, PATHEXT completion,
//!   batch files through COMSPEC, `#!` scripts through their interpreter
//!   — the shared [`crate::childproc`] resolver) and spawned with
//!   inherited stdio, the caller's environment and cwd. Failure to
//!   resolve: "nohup: failed to run command 'C': No such file or
//!   directory" rc 127 (probed GNU-equal wording); a found file no
//!   format in the chain can run is 126 "Exec format error" (#1587: it
//!   used to be a silent 126, same as timeout); other spawn failures
//!   are rc 126.
//! * The child's exit status is the run's status; a child killed without
//!   a code maps to 125.
//! * GNU has no -h short option: `-h` is "invalid option -- 'h'" rc 125
//!   (probed; argopt's bare `-h` -> help mapping is argopt's own
//!   extension, so nohup rejects the lone form itself); --help/--version
//!   print the probed GNU text.
//!
//! Terminal faces (nohup.c:117-162, probed WSL coreutils 9.4):
//! * stdin is a tty -> the child reads from NUL ("unreadable file");
//! * stdout is a tty -> the child appends to `nohup.out` in the cwd,
//!   falling back to `$HOME/nohup.out` when the local open fails
//!   (GNU's two-step open; both failure diagnostics are printed);
//! * stderr is a tty -> the child writes to stdout's destination —
//!   the nohup.out file when stdout was redirected, otherwise a pipe
//!   this task pumps into the injected `io.stdout` (the Io value model's
//!   "stdout destination"; GNU's dup2 names the same sink).
//! * Notices: "ignoring input", "appending output to 'FILE'" /
//!   "ignoring input and appending output to 'FILE'", "redirecting
//!   stderr to stdout" / "ignoring input and redirecting stderr to
//!   stdout" — GNU's exact inner wording, program prefix `nohup: `.
//!
//! Console facts arrive injected through [`StdioMeta`] — the CLI driver
//! composes them from the process handles (GetConsoleMode), the ABI /
//! library default [`nohup_cmd`] sees [`StdioMeta::pipe`], honestly all
//! non-tty (the wiring contract from #1487 / the tty_with_meta lane).
//! The library never probes process stdio itself.
//!
//! Documented boundary vs GNU: `isatty` cannot distinguish a CLOSED fd
//! from a redirected one, so GNU's `stdout_is_closed` open-for-stderr
//! path is unreachable here (the C++ applet has the same limit). The
//! SIGHUP analog is the C++ applet's `CREATE_NEW_PROCESS_GROUP` — the
//! child leaves the parent's console-ctrl group.

use std::ffi::OsString;
use std::io::Read as _;
use std::os::windows::process::CommandExt as _;
use std::process::Stdio;

use crate::argopt::{self, OptSpec};
use crate::childproc::{build_child_command, resolve_child, ChildResolve};
use crate::fs_win::win32_reason;
use crate::gnuhelp;
use winux_core::{tr, win32_env_get, CmdResult, Ctx, Io, StdioMeta};

const OPTS: &[OptSpec] = &[
    // GNU nohup has no options at all besides --help/--version; a lone
    // `-h` is rejected below (GNU probed rc 125) before argopt's bare
    // `-h` -> help mapping can claim it.
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

/// Reason string for a std io::Error on the terminal-face opens — the
/// Win32 -> strerror map every other command uses (fs_win), so the
/// diagnostics keep the C-locale GNU suffixes ("Permission denied").
fn io_reason(e: &std::io::Error) -> &'static str {
    e.raw_os_error()
        .map(|c| win32_reason(c as u32))
        .unwrap_or("Invalid argument")
}

/// The registry/ABI entry: with no terminal facts injected, all three
/// faces stay off — the captured ABI streams are pipes, so GNU takes the
/// same no-redirect path (honest metadata, matching tty_cmd).
pub fn nohup_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    nohup_with_meta(args, io, ctx, &StdioMeta::pipe())
}

pub fn nohup_with_meta(args: &[OsString], io: &mut Io, ctx: &Ctx, stdio: &StdioMeta) -> CmdResult {
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
    // GNU nohup has no -h short option (probed: "invalid option -- 'h'",
    // rc = usage exit). argopt maps a lone `-h` to help as its own
    // extension, so nohup rejects that form before argopt sees it; the
    // cluster forms keep argopt's getopt-order diagnostics (`-hx` ->
    // "invalid option -- 'h'", `-xh` -> "invalid option -- 'x'").
    if leading.iter().any(|a| a.to_string_lossy() == "-h") {
        let _ = winux_core::usage_error(ctx, io, "nohup", &argopt::invalid_option(ctx, "h"));
        return Ok(usage_exit_status(ctx));
    }
    let p = match argopt::parse("nohup", &leading, OPTS, io, ctx, usage_exit_status(ctx)) {
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
        let _ = winux_core::usage_error(ctx, io, "nohup", &crate::messages::missing_operand(ctx));
        return Ok(usage_exit_status(ctx));
    }

    let command = command_args[0].to_string_lossy().into_owned();
    let cmd_args: Vec<String> = command_args[1..]
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    // ---- terminal faces (nohup.c:117-162) ------------------------------
    // Console facts come from the injected StdioMeta; each redirect is
    // materialized as a child-stdio override below. The notices precede
    // the launch, exactly where nohup.c issues them.
    let ignoring_input = stdio.is_tty(0);
    let redirecting_stdout = stdio.is_tty(1);
    let redirecting_stderr = stdio.is_tty(2);

    // stdin tty -> NUL (the C++ applet's GENERIC_WRITE NUL; reads on the
    // null device still return EOF, which is the unreadable-file face).
    let mut nul_in: Option<std::fs::File> = None;
    if ignoring_input {
        match std::fs::OpenOptions::new().write(true).open("NUL") {
            Ok(f) => nul_in = Some(f),
            Err(e) => {
                let _ = writeln!(
                    io.stderr,
                    "nohup: {}",
                    tr!(
                        ctx,
                        "command.nohup.error.stdin_unusable",
                        "failed to render standard input unusable: {reason}",
                        reason = io_reason(&e)
                    )
                );
                return Ok(usage_exit_status(ctx));
            }
        }
    }

    // stdout tty -> append nohup.out, else $HOME/nohup.out (GNU two-step:
    // each failed open gets its own diagnostic, in order).
    let mut out_file: Option<std::fs::File> = None;
    if redirecting_stdout {
        let local = ctx.cwd.join("nohup.out");
        let mut opened = std::fs::OpenOptions::new()
            .append(true)
            .create(true)
            .open(&local);
        let mut file_name = "nohup.out".to_string();
        if let Err(first) = opened {
            let home = win32_env_get(ctx.env, "HOME")
                .map(|v| v.to_string_lossy().into_owned())
                .filter(|h| !h.is_empty());
            if let Some(h) = home {
                file_name = format!("{h}/nohup.out");
                opened = std::fs::OpenOptions::new()
                    .append(true)
                    .create(true)
                    .open(&file_name);
                if let Err(second) = opened {
                    let _ = writeln!(
                        io.stderr,
                        "nohup: {}",
                        tr!(
                            ctx,
                            "command.nohup.error.open_failed",
                            "failed to open '{file}': {reason}",
                            file = "nohup.out",
                            reason = io_reason(&first)
                        )
                    );
                    let _ = writeln!(
                        io.stderr,
                        "nohup: {}",
                        tr!(
                            ctx,
                            "command.nohup.error.open_failed",
                            "failed to open '{file}': {reason}",
                            file = file_name,
                            reason = io_reason(&second)
                        )
                    );
                    return Ok(usage_exit_status(ctx));
                }
            } else {
                let _ = writeln!(
                    io.stderr,
                    "nohup: {}",
                    tr!(
                        ctx,
                        "command.nohup.error.open_failed",
                        "failed to open '{file}': {reason}",
                        file = "nohup.out",
                        reason = io_reason(&first)
                    )
                );
                return Ok(usage_exit_status(ctx));
            }
        }
        out_file = Some(opened.unwrap());
        let _ = writeln!(
            io.stderr,
            "nohup: {}",
            if ignoring_input {
                tr!(
                    ctx,
                    "command.nohup.warning.ignoring_appending",
                    "ignoring input and appending output to '{file}'",
                    file = file_name
                )
            } else {
                tr!(
                    ctx,
                    "command.nohup.warning.appending",
                    "appending output to '{file}'",
                    file = file_name
                )
            }
        );
    }

    // stderr tty -> stdout's destination. When stdout was redirected the
    // destination is the nohup.out file itself (a second append-mode
    // handle over it — byte order is preserved by O_APPEND writes, same
    // as GNU's dup2 sharing the open file description). Otherwise the
    // destination is the injected io.stdout: the child gets a pipe and
    // this task pumps its stderr into the abstract writer.
    let mut pump_stderr_to_stdout = false;
    if redirecting_stderr {
        if out_file.is_some() {
            // handled at spawn: a try_clone of the nohup.out handle.
        } else {
            pump_stderr_to_stdout = true;
            let _ = writeln!(
                io.stderr,
                "nohup: {}",
                if ignoring_input {
                    tr!(
                        ctx,
                        "command.nohup.warning.ignoring_redirecting",
                        "ignoring input and redirecting stderr to stdout"
                    )
                } else {
                    tr!(
                        ctx,
                        "command.nohup.warning.redirecting_stderr",
                        "redirecting stderr to stdout"
                    )
                }
            );
        }
    } else if ignoring_input && !redirecting_stdout {
        let _ = writeln!(
            io.stderr,
            "nohup: {}",
            tr!(
                ctx,
                "command.nohup.warning.ignoring_input",
                "ignoring input"
            )
        );
    }

    // Resolve and spawn through the shared childproc chain (PATH-only,
    // PATHEXT completion, COMSPEC batch routing, #1587 shebang launch).
    // Diagnostics follow GNU nohup.c's execvp failure path in the C
    // locale (probed WSL coreutils 9.4), the same shapes timeout reports.
    let mut child = match resolve_child(&command, ctx) {
        ChildResolve::Found(launch) => {
            let mut cmd = build_child_command(&launch, &cmd_args, ctx);
            if let Some(f) = &nul_in {
                cmd.stdin(f.try_clone().map_or_else(|_| Stdio::null(), Stdio::from));
            }
            if let Some(f) = &out_file {
                cmd.stdout(f.try_clone().map_or_else(|_| Stdio::null(), Stdio::from));
            }
            if redirecting_stderr {
                if let Some(f) = &out_file {
                    cmd.stderr(f.try_clone().map_or_else(|_| Stdio::null(), Stdio::from));
                } else {
                    cmd.stderr(Stdio::piped());
                }
            }
            // SIGHUP immunity's Windows analog (the C++ applet's
            // STARTUPINFO contract): leave the parent's console-ctrl
            // group so Ctrl+C/Ctrl+Break no longer reach the child.
            cmd.creation_flags(0x00000200 /* CREATE_NEW_PROCESS_GROUP */);
            match cmd.spawn() {
                Ok(c) => c,
                Err(_) => {
                    let _ = writeln!(
                        io.stderr,
                        "nohup: {}",
                        tr!(
                            ctx,
                            "command.nohup.error.failed_to_run",
                            "failed to run command '{command}': {reason}",
                            command = command,
                            reason = "Permission denied"
                        )
                    );
                    return Ok(126);
                }
            }
        }
        ChildResolve::NotFound => {
            let _ = writeln!(
                io.stderr,
                "nohup: {}",
                tr!(
                    ctx,
                    "command.nohup.error.failed_to_run",
                    "failed to run command '{command}': {reason}",
                    command = command,
                    reason = "No such file or directory"
                )
            );
            return Ok(127);
        }
        ChildResolve::BadFormat => {
            let _ = writeln!(
                io.stderr,
                "nohup: {}",
                tr!(
                    ctx,
                    "command.nohup.error.failed_to_run",
                    "failed to run command '{command}': {reason}",
                    command = command,
                    reason = "Exec format error"
                )
            );
            return Ok(126);
        }
        ChildResolve::NotExecutable | ChildResolve::NotADirectory => {
            let _ = writeln!(
                io.stderr,
                "nohup: {}",
                tr!(
                    ctx,
                    "command.nohup.error.failed_to_run",
                    "failed to run command '{command}': {reason}",
                    command = command,
                    reason = "Permission denied"
                )
            );
            return Ok(126);
        }
    };
    if pump_stderr_to_stdout {
        // "stderr to stdout" with an unredirected stdout: the child's
        // stderr bytes belong in the injected writer. Drain until the
        // last pipe holder closes (child exit), then collect the status.
        if let Some(mut src) = child.stderr.take() {
            let mut buf = [0u8; 8192];
            loop {
                match src.read(&mut buf) {
                    Ok(0) | Err(_) => break,
                    Ok(n) => {
                        // A dead stdout drops the bytes but keeps the
                        // pipe drained so the child never stalls.
                        let _ = io.stdout.write_all(&buf[..n]);
                    }
                }
            }
        }
    }
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
        let ctx = Ctx::new(sc.dir.clone(), env);
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::nohup_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn run(args: &[&str]) -> (u32, String, String) {
        // inherit the real process env so PATH-based child resolution works
        let mut env = MapEnv::new();
        for (k, v) in std::env::vars_os() {
            env.set(&k.to_string_lossy(), &v.to_string_lossy());
        }
        run_with(&env, args)
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
        // (GNU probed: nohup ./somedir -> 126 "Permission denied")
        let (r, _, err) = run(&["C:\\\\"]);
        assert_eq!(r, 126);
        assert!(err.contains("failed to run command"));
    }

    #[test]
    fn non_pe_children_report_gnu_wording() {
        // #1587: a found but unrunnable file used to exit 126 silently;
        // the chain now reports the GNU ENOEXEC strerror.
        let sc = ScratchCtx::new("nohup-badformat");
        let txt = sc.dir.join("plain.txt");
        std::fs::write(&txt, "just text\n").unwrap();
        let operand = txt.to_string_lossy().into_owned();
        let (r, _, err) = run(&[&operand]);
        assert_eq!(r, 126);
        assert!(err.contains(&format!(
            "failed to run command '{operand}': Exec format error"
        )));
        // a shebang script whose interpreter misses: the kernel's ENOENT
        let bad = sc.dir.join("bad.sh");
        std::fs::write(&bad, "#!/bin/no-such-interp-xyz\necho never\n").unwrap();
        let operand = bad.to_string_lossy().into_owned();
        let (r, _, err) = run(&[&operand]);
        assert_eq!(r, 127);
        assert!(err.contains(&format!(
            "failed to run command '{operand}': No such file or directory"
        )));
    }

    #[cfg(windows)]
    #[test]
    fn shebang_child_launches() {
        // #1587: `nohup ./hi.sh` runs the script through its interpreter
        // (rc 0) instead of dying with a silent 126.
        let sc = ScratchCtx::new("nohup-shebang");
        let hi = sc.dir.join("hi.sh");
        std::fs::write(&hi, "#!/bin/sh\nexit 3\n").unwrap();
        let operand = hi.to_string_lossy().into_owned();
        let (r, _, err) = run(&[&operand]);
        assert_eq!(r, 3, "err={err}");
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
        // errors (nohup.c usage() -> exit(usage_exit_status))
        let (r, _, err) = run(&["-h"]);
        assert_eq!(r, 125);
        assert!(err.contains("invalid option -- 'h'"));
        assert!(err.contains("Try 'nohup --help' for more information."));
    }

    fn run_meta(
        sc: &ScratchCtx,
        env: &MapEnv,
        args: &[&str],
        meta: &winux_core::StdioMeta,
    ) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = Ctx::new(sc.dir.clone(), env);
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::nohup_with_meta(&owned, &mut io.split(), &ctx, meta).unwrap();
        (r, io.out(), io.err())
    }

    fn real_env() -> MapEnv {
        let mut env = MapEnv::new();
        for (k, v) in std::env::vars_os() {
            env.set(&k.to_string_lossy(), &v.to_string_lossy());
        }
        env
    }

    #[test]
    fn pipe_meta_leaves_all_faces_off() {
        // nohup_cmd's own surface: injected pipe facts mean no redirect,
        // no notice, and no nohup.out in the cwd.
        let sc = ScratchCtx::new("nohup-pipemeta");
        let env = real_env();
        let (r, out, err) = run_meta(
            &sc,
            &env,
            &["cmd", "/c", "exit", "0"],
            &winux_core::StdioMeta::pipe(),
        );
        assert_eq!(r, 0);
        assert_eq!(out, "");
        assert_eq!(err, "");
        assert!(!sc.dir.join("nohup.out").exists());
    }

    #[test]
    fn tty_stdin_alone_ignores_input() {
        let sc = ScratchCtx::new("nohup-ttyin");
        let env = real_env();
        let (r, _out, err) = run_meta(
            &sc,
            &env,
            &["cmd", "/c", "exit", "0"],
            &winux_core::StdioMeta::with_tty([true, false, false]),
        );
        assert_eq!(r, 0);
        assert_eq!(err, "nohup: ignoring input\n");
        assert!(!sc.dir.join("nohup.out").exists());
    }

    #[test]
    fn tty_stdout_appends_nohup_out() {
        // stdout tty -> child output lands in ./nohup.out, the notice
        // spells GNU's inner wording.
        let sc = ScratchCtx::new("nohup-ttyout");
        let env = real_env();
        let (r, _out, err) = run_meta(
            &sc,
            &env,
            &["cmd", "/d", "/s", "/c", "echo hi"],
            &winux_core::StdioMeta::with_tty([false, true, false]),
        );
        assert_eq!(r, 0);
        assert_eq!(err, "nohup: appending output to 'nohup.out'\n");
        let bytes = std::fs::read(sc.dir.join("nohup.out")).unwrap();
        assert_eq!(String::from_utf8_lossy(&bytes), "hi\r\n");
    }

    #[test]
    fn tty_stdin_and_stdout_combine_notices() {
        let sc = ScratchCtx::new("nohup-ttyinout");
        let env = real_env();
        let (r, _out, err) = run_meta(
            &sc,
            &env,
            &["cmd", "/d", "/s", "/c", "echo hi"],
            &winux_core::StdioMeta::with_tty([true, true, false]),
        );
        assert_eq!(r, 0);
        assert_eq!(
            err,
            "nohup: ignoring input and appending output to 'nohup.out'\n"
        );
    }

    #[test]
    fn tty_all_three_sends_stderr_to_nohup_out() {
        // stderr tty + stdout tty: child's stderr appends to the same
        // file (a second append-mode handle, no "redirecting" notice).
        let sc = ScratchCtx::new("nohup-ttyall");
        let env = real_env();
        let (r, _out, err) = run_meta(
            &sc,
            &env,
            &["cmd", "/d", "/s", "/c", "echo O&echo E>&2"],
            &winux_core::StdioMeta::with_tty([true, true, true]),
        );
        assert_eq!(r, 0);
        assert_eq!(
            err,
            "nohup: ignoring input and appending output to 'nohup.out'\n"
        );
        let bytes = std::fs::read(sc.dir.join("nohup.out")).unwrap();
        let text = String::from_utf8_lossy(&bytes);
        assert!(text.contains("O\r\n"), "text={text:?}");
        assert!(text.contains("E\r\n"), "text={text:?}");
    }

    #[test]
    fn tty_stderr_alone_redirects_to_stdout() {
        // stderr tty, stdout not: GNU dup2s stderr onto stdout's
        // destination — in the Io model the child pipes stderr and the
        // task pumps it into the injected stdout writer.
        let sc = ScratchCtx::new("nohup-ttyerr");
        let env = real_env();
        let (r, out, err) = run_meta(
            &sc,
            &env,
            &["cmd", "/d", "/s", "/c", "echo E>&2"],
            &winux_core::StdioMeta::with_tty([false, false, true]),
        );
        assert_eq!(r, 0);
        assert_eq!(err, "nohup: redirecting stderr to stdout\n");
        assert_eq!(out, "E\r\n");
        assert!(!sc.dir.join("nohup.out").exists());
    }

    #[test]
    fn tty_stdin_and_stderr_combine_notices() {
        let sc = ScratchCtx::new("nohup-ttyinerr");
        let env = real_env();
        let (r, out, err) = run_meta(
            &sc,
            &env,
            &["cmd", "/d", "/s", "/c", "echo E>&2"],
            &winux_core::StdioMeta::with_tty([true, false, true]),
        );
        assert_eq!(r, 0);
        assert_eq!(
            err,
            "nohup: ignoring input and redirecting stderr to stdout\n"
        );
        assert_eq!(out, "E\r\n");
    }

    #[test]
    fn tty_stdout_falls_back_to_home() {
        // nohup.out blocked by a directory of the same name: GNU's second
        // open lands $HOME/nohup.out and the notice names it.
        let sc = ScratchCtx::new("nohup-homefb");
        let home = sc.dir.join("home");
        std::fs::create_dir_all(&home).unwrap();
        std::fs::create_dir(sc.dir.join("nohup.out")).unwrap();
        let mut env = real_env();
        env.vars
            .retain(|(k, _)| !k.to_string_lossy().eq_ignore_ascii_case("HOME"));
        env.set("HOME", &home.to_string_lossy());
        let (r, _out, err) = run_meta(
            &sc,
            &env,
            &["cmd", "/d", "/s", "/c", "echo hi"],
            &winux_core::StdioMeta::with_tty([false, true, false]),
        );
        assert_eq!(r, 0);
        let want = format!(
            "nohup: appending output to '{}/nohup.out'\n",
            home.to_string_lossy()
        );
        assert_eq!(err, want);
        let bytes = std::fs::read(home.join("nohup.out")).unwrap();
        assert_eq!(String::from_utf8_lossy(&bytes), "hi\r\n");
    }

    #[test]
    fn tty_stdout_open_failure_reports_both_and_exits_125() {
        // Both opens fail: GNU prints one diagnostic per step, in order.
        let sc = ScratchCtx::new("nohup-openfail");
        std::fs::create_dir(sc.dir.join("nohup.out")).unwrap();
        let mut env = real_env();
        // HOME under a missing root: the second open fails too.
        let gone = sc.dir.join("no\\such\\home");
        env.vars
            .retain(|(k, _)| !k.to_string_lossy().eq_ignore_ascii_case("HOME"));
        env.set("HOME", &gone.to_string_lossy());
        let (r, _out, err) = run_meta(
            &sc,
            &env,
            &["cmd", "/c", "exit", "0"],
            &winux_core::StdioMeta::with_tty([false, true, false]),
        );
        assert_eq!(r, 125);
        assert!(
            err.starts_with("nohup: failed to open 'nohup.out': "),
            "err={err:?}"
        );
        assert!(
            err.contains(&format!(
                "failed to open '{}/nohup.out': ",
                gone.to_string_lossy()
            )),
            "err={err:?}"
        );
    }
}
