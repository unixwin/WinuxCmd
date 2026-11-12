//! `nice` — GNU coreutils nice.c (9.4) semantics with the Windows
//! priority mapping of the audited 1.x body (src/commands/nice.cpp,
//! #1359), probed against WSL coreutils 9.4 (rust2/target/probe/p-mk.sh).
//!
//! Option surface: `-n/--adjustment=N` (default 10), parsed like GNU's
//! xstrtol: leading whitespace and a leading `+` are accepted, an empty
//! or non-numeric value dies with `nice: invalid adjustment 'V'` and rc
//! 125 with NO try-tail (probed; out-of-range values clamp at ±50 like
//! the C++ body). With no COMMAND: prints the current niceness
//! (`win32_current_niceness`: NORMAL 0, BELOW_NORMAL 10, IDLE 19,
//! ABOVE_NORMAL -5, HIGH -10; the gate's processes all report 0 like
//! GNU). With an adjustment but no COMMAND:
//! `a command must be given with an adjustment` + try-tail, rc 125.
//!
//! The child resolves through the shared execvp-shaped PATH-only search
//! (reused from timeout.rs): not-found is rc 127 with
//! `nice: 'CMD': No such file or directory`. The child is created
//! CREATE_SUSPENDED, the niceness-mapped priority class is applied with
//! SetPriorityClass (a failure or a silent clamp reports
//! `nice: cannot set niceness: <reason>` on stderr but the command still
//! runs — GNU's behavior), then the main thread is resumed and its exit
//! code is the command's status.
//!
//! Documented deviation: the child inherits the PROCESS stdio handles,
//! not the injected `Io` buffers (same shape as timeout.rs); the
//! differential gate redirects process stdio, so the observable bytes
//! match. Negative adjustments on unprivileged Windows report
//! "cannot set niceness" where GNU would, but the class mapping is
//! Windows-inherent.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use crate::timeout::{quote_windows_arg, resolve_child, ChildResolve};
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[OptSpec {
    short: Some('n'),
    long: "adjustment",
    takes_value: true,
}];

// kernel32 surface for the priority mapping (same family as fs_win.rs;
// no CRT fds are touched)
#[link(name = "kernel32")]
unsafe extern "system" {
    fn GetCurrentProcess() -> *mut core::ffi::c_void;
    fn GetPriorityClass(h_process: *mut core::ffi::c_void) -> u32;
    fn SetPriorityClass(h_process: *mut core::ffi::c_void, priority_class: u32) -> i32;
    fn ResumeThread(h_thread: *mut core::ffi::c_void) -> u32;
    fn WaitForSingleObject(h_handle: *mut core::ffi::c_void, ms: u32) -> u32;
    fn GetExitCodeProcess(h_process: *mut core::ffi::c_void, exit_code: *mut u32) -> i32;
    fn CloseHandle(h_object: *mut core::ffi::c_void) -> i32;
}

// priority classes
const NORMAL_PRIORITY_CLASS: u32 = 0x20;
const IDLE_PRIORITY_CLASS: u32 = 0x40;
const HIGH_PRIORITY_CLASS: u32 = 0x80;
const REALTIME_PRIORITY_CLASS: u32 = 0x100;
const BELOW_NORMAL_PRIORITY_CLASS: u32 = 0x4000;
const ABOVE_NORMAL_PRIORITY_CLASS: u32 = 0x8000;

fn niceness_from_priority_class(class: u32) -> i32 {
    match class {
        HIGH_PRIORITY_CLASS => -10,
        ABOVE_NORMAL_PRIORITY_CLASS => -5,
        BELOW_NORMAL_PRIORITY_CLASS => 10,
        IDLE_PRIORITY_CLASS => 19,
        REALTIME_PRIORITY_CLASS => -20,
        _ => 0,
    }
}

fn priority_class_for_niceness(niceness: i32) -> u32 {
    if niceness <= -15 {
        REALTIME_PRIORITY_CLASS
    } else if niceness <= -10 {
        HIGH_PRIORITY_CLASS
    } else if niceness < 0 {
        ABOVE_NORMAL_PRIORITY_CLASS
    } else if niceness >= 19 {
        IDLE_PRIORITY_CLASS
    } else if niceness >= 10 {
        BELOW_NORMAL_PRIORITY_CLASS
    } else {
        NORMAL_PRIORITY_CLASS
    }
}

/// current niceness of THIS process (the gate's dispatcher runs at
/// NORMAL, so this prints 0 like GNU's getpriority(2) default)
fn current_niceness() -> i32 {
    unsafe { niceness_from_priority_class(GetPriorityClass(GetCurrentProcess())) }
}

/// xstrtol-shaped adjustment parse (nice.c:164-174): leading whitespace
/// and '+' accepted; empty/no-digits invalid; overflow clamps to ±50.
fn parse_adjustment(raw: &str) -> Option<i32> {
    let t = raw.trim_matches(|c: char| c.is_ascii_whitespace());
    if t.is_empty() {
        return None;
    }
    let (neg, digits) = match t.strip_prefix('-') {
        Some(r) => (true, r.strip_prefix('+').unwrap_or(r)),
        None => (false, t.strip_prefix('+').unwrap_or(t)),
    };
    if digits.is_empty() || !digits.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    let value: i64 = digits
        .parse::<i64>()
        .unwrap_or(if neg { i64::MIN } else { i64::MAX });
    let clamped = value.clamp(-50, 50);
    Some(if neg {
        -(clamped as i32)
    } else {
        clamped as i32
    })
}

/// Resume a suspended child's main thread via the toolhelp thread
/// snapshot (std exposes no thread handle for the child).
fn resume_child_main_thread(pid: u32) -> bool {
    #[repr(C)]
    struct THREADENTRY32 {
        dw_size: u32,
        cnt_usage: u32,
        th32_thread_id: u32,
        th32_owner_process_id: u32,
        tp_base_pri: i32,
        tp_delta_pri: i32,
        dw_flags: u32,
    }
    unsafe extern "system" {
        fn CreateToolhelp32Snapshot(flags: u32, pid: u32) -> *mut core::ffi::c_void;
        fn Thread32First(h: *mut core::ffi::c_void, entry: *mut THREADENTRY32) -> i32;
        fn Thread32Next(h: *mut core::ffi::c_void, entry: *mut THREADENTRY32) -> i32;
        fn OpenThread(access: u32, inherit: i32, tid: u32) -> *mut core::ffi::c_void;
    }
    const TH32CS_SNAPTHREAD: u32 = 0x4;
    const THREAD_SUSPEND_RESUME: u32 = 0x0002;
    const INVALID_HANDLE_VALUE: *mut core::ffi::c_void = -1isize as *mut _;
    unsafe {
        let snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
        if snap == INVALID_HANDLE_VALUE || snap.is_null() {
            return false;
        }
        let mut entry = THREADENTRY32 {
            dw_size: std::mem::size_of::<THREADENTRY32>() as u32,
            cnt_usage: 0,
            th32_thread_id: 0,
            th32_owner_process_id: 0,
            tp_base_pri: 0,
            tp_delta_pri: 0,
            dw_flags: 0,
        };
        let mut ok = false;
        if Thread32First(snap, &mut entry) != 0 {
            loop {
                if entry.th32_owner_process_id == pid {
                    let thread = OpenThread(THREAD_SUSPEND_RESUME, 0, entry.th32_thread_id);
                    if !thread.is_null() && thread != INVALID_HANDLE_VALUE {
                        ok = ResumeThread(thread) != u32::MAX;
                        CloseHandle(thread);
                        if ok {
                            break;
                        }
                    }
                }
                if Thread32Next(snap, &mut entry) == 0 {
                    break;
                }
            }
        }
        CloseHandle(snap);
        ok
    }
}

// `--help` body verbatim from `env nice --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const NICE_HELP_BODY: &str = r#"Usage: nice [OPTION] [COMMAND [ARG]...]
Run COMMAND with an adjusted niceness, which affects process scheduling.
With no COMMAND, print the current niceness.  Niceness values range from
-20 (most favorable to the process) to 19 (least favorable to the process).

Mandatory arguments to long options are mandatory for short options too.
  -n, --adjustment=N   add integer N to the niceness (default 10)
      --help        display this help and exit
      --version     output version information and exit

NOTE: your shell may have its own version of nice, which usually supersedes
the version described here.  Please refer to your shell's documentation
for details about the options it supports.

Exit status:
  125  if the nice command itself fails
  126  if COMMAND is found but cannot be invoked
  127  if COMMAND cannot be found
  -    the exit status of COMMAND otherwise

"#;

pub fn nice_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("nice", args, OPTS, io, ctx, 125) {
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
                    NICE_HELP_BODY,
                    gnuhelp::footer("nice", "nice invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("nice", "David MacKenzie")
                )?;
                0
            }
        });
    }

    // adjustment: presence decided by occurrence; '-n ""' is invalid
    let mut adjustment = 10i32;
    let mut has_adjustment = false;
    if let Some(raw) = p.value_of("adjustment") {
        match parse_adjustment(raw) {
            Some(v) => adjustment = v,
            None => {
                // probed: one stderr line, no try tail, rc 125
                let _ = writeln!(io.stderr, "nice: invalid adjustment '{raw}'");
                return Ok(125);
            }
        }
        has_adjustment = true;
    }

    let command = p
        .positionals
        .first()
        .map(|a| a.to_string_lossy().into_owned());
    let Some(command) = command else {
        if has_adjustment {
            let _ = writeln!(
                io.stderr,
                "nice: a command must be given with an adjustment"
            );
            let _ = writeln!(io.stderr, "Try 'nice --help' for more information.");
            return Ok(125);
        }
        writeln!(io.stdout, "{}", current_niceness())?;
        return Ok(0);
    };
    let cmd_args: Vec<String> = p.positionals[1..]
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    // resolve and spawn the child (process stdio inherited; documented)
    let resolved = resolve_child(&command, ctx);
    let mut cmd = match &resolved {
        ChildResolve::Found { path, batch } => {
            if *batch {
                let comspec = winux_core::win32_env_get(ctx.env, "COMSPEC")
                    .map(|v| v.to_string_lossy().into_owned())
                    .filter(|v| !v.is_empty())
                    .unwrap_or_else(|| "cmd.exe".to_string());
                let mut inner = quote_windows_arg(path);
                for a in &cmd_args {
                    inner.push(' ');
                    inner.push_str(&quote_windows_arg(a));
                }
                let mut c = std::process::Command::new(&comspec);
                c.raw_arg("/d").raw_arg("/s").raw_arg("/c");
                c.raw_arg(format!("\"{inner}\""));
                c
            } else {
                let mut c = std::process::Command::new(path);
                c.args(&cmd_args);
                c
            }
        }
        ChildResolve::NotFound => {
            let _ = writeln!(io.stderr, "nice: '{command}': No such file or directory");
            return Ok(127);
        }
        ChildResolve::NotExecutable | ChildResolve::NotADirectory => {
            let _ = writeln!(io.stderr, "nice: '{command}': Permission denied");
            return Ok(126);
        }
    };
    cmd.envs(
        ctx.env
            .entries()
            .iter()
            .filter(|(k, _)| !k.is_empty() && !k.to_string_lossy().starts_with('='))
            .map(|(k, v)| (k.clone(), v.clone())),
    );
    cmd.current_dir(&ctx.cwd);
    // CREATE_SUSPENDED so the priority class is observable before the
    // child runs any code (#1359)
    const CREATE_SUSPENDED: u32 = 0x4;
    use std::os::windows::io::AsRawHandle;
    use std::os::windows::process::CommandExt;
    cmd.creation_flags(CREATE_SUSPENDED);
    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(_) => {
            let _ = writeln!(io.stderr, "nice: '{command}': Permission denied");
            return Ok(126);
        }
    };

    // apply the priority class; failure or a silent clamp reports and
    // continues, exactly like GNU's setpriority fallback
    let target = current_niceness() + adjustment;
    let class = priority_class_for_niceness(target.clamp(-20, 19));
    unsafe {
        let handle = child.as_raw_handle() as *mut core::ffi::c_void;
        let mut applied = SetPriorityClass(handle, class) != 0;
        if applied && GetPriorityClass(handle) != class {
            applied = false;
        }
        if !applied {
            let _ = writeln!(io.stderr, "nice: cannot set niceness: Permission denied");
        }
        // resume the suspended main thread
        let resumed = resume_child_main_thread(child.id());
        if !resumed {
            // the C++ body terminates the stuck child and reports 125
            let _ = child.kill();
            let _ = child.wait();
            let _ = writeln!(io.stderr, "nice: '{command}': Permission denied");
            return Ok(125);
        }
        let mut code: u32 = 259; // STILL_ACTIVE
        WaitForSingleObject(handle, 0xFFFFFFFF);
        GetExitCodeProcess(handle, &mut code);
        if code == 259 {
            // still active (wait race): fall back to std's status
            let status = child.wait();
            code = status
                .ok()
                .and_then(|s| s.code())
                .map(|c| c as u32)
                .unwrap_or(125);
        }
        Ok(code)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("nice");
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = nice_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: nice [OPTION] [COMMAND [ARG]...]\n"));
        assert!(out.contains("  125  if the nice command itself fails"));
        assert!(out.ends_with("info '(coreutils) nice invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("nice (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie.\n"));
    }

    #[test]
    fn adjustment_parse() {
        assert_eq!(parse_adjustment("5"), Some(5));
        assert_eq!(parse_adjustment("-3"), Some(-3));
        assert_eq!(parse_adjustment("+7"), Some(7));
        assert_eq!(parse_adjustment(" 4 "), Some(4));
        assert_eq!(parse_adjustment(""), None);
        assert_eq!(parse_adjustment("abc"), None);
        assert_eq!(parse_adjustment("-"), None);
        // overflow clamps like the C++ body
        assert_eq!(parse_adjustment("99999999999999999999"), Some(50));
        assert_eq!(parse_adjustment("-99999"), Some(-50));
    }

    #[test]
    fn niceness_mapping() {
        assert_eq!(niceness_from_priority_class(NORMAL_PRIORITY_CLASS), 0);
        assert_eq!(niceness_from_priority_class(IDLE_PRIORITY_CLASS), 19);
        assert_eq!(
            niceness_from_priority_class(BELOW_NORMAL_PRIORITY_CLASS),
            10
        );
        assert_eq!(
            niceness_from_priority_class(ABOVE_NORMAL_PRIORITY_CLASS),
            -5
        );
        assert_eq!(niceness_from_priority_class(HIGH_PRIORITY_CLASS), -10);
        assert_eq!(priority_class_for_niceness(0), NORMAL_PRIORITY_CLASS);
        assert_eq!(priority_class_for_niceness(10), BELOW_NORMAL_PRIORITY_CLASS);
        assert_eq!(priority_class_for_niceness(19), IDLE_PRIORITY_CLASS);
        assert_eq!(priority_class_for_niceness(-3), ABOVE_NORMAL_PRIORITY_CLASS);
        assert_eq!(priority_class_for_niceness(-10), HIGH_PRIORITY_CLASS);
    }

    #[test]
    fn no_command_prints_niceness() {
        // the test process runs at NORMAL: 0, same as GNU's getpriority
        let (r, out, err) = run(&[]);
        assert_eq!(r, 0);
        assert_eq!(out, "0\n");
        assert!(err.is_empty());
    }

    #[test]
    fn adjustment_without_command() {
        let (r, out, e) = run(&["-n", "2"]);
        assert_eq!(r, 125);
        assert_eq!(out, "");
        assert!(e.contains("a command must be given with an adjustment"));
        assert!(e.contains("Try 'nice --help'"));
    }

    #[test]
    fn invalid_adjustment_no_try_tail() {
        let (r, out, e) = run(&["-n", "", "true"]);
        assert_eq!(r, 125);
        assert_eq!(out, "");
        assert_eq!(e, "nice: invalid adjustment ''\n");
        let (r, _, e) = run(&["-n", "abc", "true"]);
        assert_eq!(r, 125);
        assert_eq!(e, "nice: invalid adjustment 'abc'\n");
    }

    #[test]
    fn command_not_found() {
        let (r, out, e) = run(&["nosuchcmd123"]);
        assert_eq!(r, 127);
        assert_eq!(out, "");
        assert!(e.contains("nice: 'nosuchcmd123': No such file or directory"));
    }

    #[test]
    fn runs_child() {
        // no `true` exists on Windows: run the system command interpreter
        // (System32 is the resolver's fallback PATH when ctx has none)
        let (r, _, _) = run(&["cmd", "/c", "exit", "3"]);
        assert_eq!(r, 3);
        let (r, _, _) = run(&["-n", "5", "cmd", "/c", "exit", "0"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&["-n", "-3", "cmd", "/c", "exit", "0"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&["--adjustment=5", "cmd", "/c", "exit", "0"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&["--", "cmd", "/c", "exit", "0"]);
        assert_eq!(r, 0);
        // negative adjustments still run the child (GNU behavior)
        let (r, _, _) = run(&["-n", "999", "cmd", "/c", "exit", "0"]);
        assert_eq!(r, 0);
    }
}
