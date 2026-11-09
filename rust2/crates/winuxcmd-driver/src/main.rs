//! winuxcmd-driver: the thin CLI adapter used by the differential gate.
//!
//! This is the ONLY place process state is read: argv comes from
//! `env::args_os`, env from `ProcessEnv`, cwd from `env::current_dir`,
//! stdio from the process handles. The command itself runs through the same
//! `winux_cmds::dispatch` the cdylib ABI uses, so the gate exercises the
//! library semantics, not a separate implementation.

use std::io::Write;

use winux_core::{Ctx, Io, ProcessEnv};

fn main() {
    let argv: Vec<std::ffi::OsString> = std::env::args_os().skip(1).collect();
    if argv.is_empty() {
        eprintln!("winuxcmd-driver: missing command name");
        std::process::exit(2);
    }
    let name = argv[0].to_string_lossy().into_owned();
    let args: Vec<std::ffi::OsString> = argv[1..].to_vec();

    let env = ProcessEnv;
    let cwd = std::env::current_dir().unwrap_or_default();
    let ctx = Ctx { cwd, env: &env };

    let stdin = std::io::stdin();
    let stdout = std::io::stdout();
    let stderr = std::io::stderr();
    let mut stdin_lock = stdin.lock();
    let mut stdout_lock = stdout.lock();
    let mut stderr_lock = stderr.lock();
    let mut io = Io {
        stdin: &mut stdin_lock,
        stdout: &mut stdout_lock,
        stderr: &mut stderr_lock,
    };

    let status = match winux_cmds::dispatch(&name, &args, &mut io, &ctx) {
        Some(Ok(s)) => s as i32,
        Some(Err(_)) => winux_core::ABI_FAILURE_STATUS as i32,
        None => {
            let _ = writeln!(stderr_lock, "winuxcmd-driver: unknown command: {}", name);
            127
        }
    };
    let _ = stdout_lock.flush();
    let _ = stderr_lock.flush();
    std::process::exit(status);
}
