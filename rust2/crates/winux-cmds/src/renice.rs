//! `renice` — util-linux renice.c semantics on Windows, ported for WinuxCmd 2.0 (#1488).
//!
//! Maps Unix niceness (`-20..=19`) to Win32 priority classes via `OpenProcess`,
//! `GetPriorityClass`, and `SetPriorityClass` with explicit `Io`/`Ctx`.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const RENICE_HELP: &str = "\
Usage:
 renice [-n] <priority> [-p|--pid] <pid>...
 renice [-n] <priority>  -g|--pgrp <pgid>...
 renice [-n] <priority>  -u|--user <user>...

Alter the scheduling priority of running processes.

Options:
 -n, --priority <num>  specify the nice value
 -p, --pid             interpret arguments as process ID (default)
 -g, --pgrp            interpret arguments as process group ID (unsupported on Windows)
 -u, --user            interpret arguments as user name (unsupported on Windows)
 -h, --help            display this help
 -V, --version         display version
";

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

fn priority_class_for_niceness(n: i32) -> u32 {
    if n <= -15 {
        REALTIME_PRIORITY_CLASS
    } else if n <= -8 {
        HIGH_PRIORITY_CLASS
    } else if n < 0 {
        ABOVE_NORMAL_PRIORITY_CLASS
    } else if n >= 15 {
        IDLE_PRIORITY_CLASS
    } else if n >= 5 {
        BELOW_NORMAL_PRIORITY_CLASS
    } else {
        NORMAL_PRIORITY_CLASS
    }
}

#[cfg(windows)]
mod sys {
    use std::io;

    const PROCESS_SET_INFORMATION: u32 = 0x0200;
    const PROCESS_QUERY_LIMITED_INFORMATION: u32 = 0x1000;

    #[link(name = "kernel32")]
    extern "system" {
        fn OpenProcess(access: u32, inherit: i32, pid: u32) -> isize;
        fn GetPriorityClass(h_process: isize) -> u32;
        fn SetPriorityClass(h_process: isize, priority_class: u32) -> i32;
        fn CloseHandle(handle: isize) -> i32;
    }

    pub fn set_process_priority(pid: u32, new_class: u32) -> io::Result<u32> {
        let handle = unsafe {
            OpenProcess(
                PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION,
                0,
                pid,
            )
        };
        if handle == 0 {
            return Err(io::Error::last_os_error());
        }
        let old_class = unsafe { GetPriorityClass(handle) };
        let ok = unsafe { SetPriorityClass(handle, new_class) };
        let err = io::Error::last_os_error();
        unsafe {
            CloseHandle(handle);
        }
        if ok == 0 {
            return Err(err);
        }
        Ok(old_class)
    }
}

#[cfg(not(windows))]
mod sys {
    use std::io;

    pub fn set_process_priority(_pid: u32, _new_class: u32) -> io::Result<u32> {
        Err(io::Error::from_raw_os_error(libc_enosys()))
    }

    const fn libc_enosys() -> i32 {
        38
    }
}

pub fn renice_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut priority_text: Option<String> = None;
    let mut operands: Vec<String> = Vec::new();
    let mut no_more_opts = false;
    let mut i = 0usize;

    while i < args.len() {
        let s = args[i].to_string_lossy();
        if no_more_opts {
            operands.push(s.into_owned());
            i += 1;
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            i += 1;
            continue;
        }
        if s == "-h" || s == "--help" {
            io.stdout.write_all(RENICE_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "-V" || s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("renice", "WinuxCmd")
            )?;
            return Ok(0);
        }
        if s == "-g" || s == "--pgrp" {
            writeln!(
                io.stderr,
                "renice: process group IDs (-g/--pgrp) are not supported on Windows [DIFFERS from GNU renice]"
            )?;
            return Ok(1);
        }
        if s == "-u" || s == "--user" {
            writeln!(
                io.stderr,
                "renice: user-based renice (-u/--user) is not supported on Windows [DIFFERS from GNU renice]"
            )?;
            return Ok(1);
        }
        if s == "-p" || s == "--pid" {
            i += 1;
            continue;
        }
        if s == "-n" || s == "--priority" {
            i += 1;
            if i >= args.len() {
                let _ = usage_error(
                    ctx,
                    io,
                    "renice",
                    &argopt::option_requires_argument(ctx, &s),
                );
                return Ok(1);
            }
            priority_text = Some(args[i].to_string_lossy().into_owned());
            i += 1;
            continue;
        }
        if let Some(val) = s.strip_prefix("--priority=") {
            priority_text = Some(val.to_owned());
            i += 1;
            continue;
        }
        if s.starts_with("-n") && s.len() > 2 {
            priority_text = Some(s[2..].to_owned());
            i += 1;
            continue;
        }
        if s.starts_with("--") {
            let _ = usage_error(ctx, io, "renice", &argopt::unrecognized_option(ctx, &s));
            return Ok(1);
        }
        // Negative numbers (e.g. `-5`) are valid priorities when no -n was given.
        if s.starts_with('-') && s.len() > 1 && s[1..].bytes().all(|b| b.is_ascii_digit()) {
            if priority_text.is_none() {
                priority_text = Some(s.into_owned());
            } else {
                operands.push(s.into_owned());
            }
            i += 1;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let c = s.chars().nth(1).unwrap();
            let _ = usage_error(
                ctx,
                io,
                "renice",
                &argopt::invalid_option(ctx, &c.to_string()),
            );
            return Ok(1);
        }
        if priority_text.is_none() {
            priority_text = Some(s.into_owned());
        } else {
            operands.push(s.into_owned());
        }
        i += 1;
    }

    let Some(prio_str) = priority_text.filter(|p| !p.is_empty()) else {
        let _ = usage_error(ctx, io, "renice", "not enough arguments");
        return Ok(1);
    };
    if operands.is_empty() {
        let _ = usage_error(ctx, io, "renice", "not enough arguments");
        return Ok(1);
    }

    let Ok(parsed_nice) = prio_str.parse::<i32>() else {
        let _ = usage_error(ctx, io, "renice", &format!("invalid priority '{prio_str}'"));
        return Ok(1);
    };
    let niceness = parsed_nice.clamp(-20, 19);
    let priority_class = priority_class_for_niceness(niceness);

    let mut status = 0u32;
    for op in &operands {
        let Ok(pid) = op.parse::<u32>() else {
            writeln!(io.stderr, "renice: invalid process ID '{op}'")?;
            status = 1;
            continue;
        };
        if pid == 0 {
            writeln!(io.stderr, "renice: invalid process ID '{op}'")?;
            status = 1;
            continue;
        }
        match sys::set_process_priority(pid, priority_class) {
            Ok(old_class) => {
                let old_nice = niceness_from_priority_class(old_class);
                writeln!(
                    io.stdout,
                    "{pid} (process ID) old priority {old_nice}, new priority {niceness}"
                )?;
            }
            Err(e) => {
                writeln!(
                    io.stderr,
                    "renice: failed to set priority for {pid}: {e}"
                )?;
                status = 1;
            }
        }
    }

    Ok(status)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_renice(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("renice-test");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = renice_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn priority_mapping_and_validation() {
        assert_eq!(priority_class_for_niceness(-20), REALTIME_PRIORITY_CLASS);
        assert_eq!(priority_class_for_niceness(-10), HIGH_PRIORITY_CLASS);
        assert_eq!(priority_class_for_niceness(0), NORMAL_PRIORITY_CLASS);
        assert_eq!(priority_class_for_niceness(10), BELOW_NORMAL_PRIORITY_CLASS);
        assert_eq!(priority_class_for_niceness(19), IDLE_PRIORITY_CLASS);
        assert_eq!(niceness_from_priority_class(IDLE_PRIORITY_CLASS), 19);

        let (rc, _, err) = run_renice(&[]);
        assert_eq!(rc, 1);
        assert!(err.contains("not enough arguments"));

        let (rc, _, err) = run_renice(&["10"]);
        assert_eq!(rc, 1);
        assert!(err.contains("not enough arguments"));

        let (rc, _, err) = run_renice(&["bogus", "123"]);
        assert_eq!(rc, 1);
        assert!(err.contains("invalid priority 'bogus'"));

        let (rc, _, err) = run_renice(&["10", "not-a-pid"]);
        assert_eq!(rc, 1);
        assert!(err.contains("invalid process ID 'not-a-pid'"));
    }

    #[test]
    fn unsupported_pgrp_and_user_rejected() {
        let (rc, _, err) = run_renice(&["-g", "10", "123"]);
        assert_eq!(rc, 1);
        assert!(err.contains("not supported on Windows"));

        let (rc, _, err) = run_renice(&["-u", "10", "root"]);
        assert_eq!(rc, 1);
        assert!(err.contains("not supported on Windows"));
    }
}
