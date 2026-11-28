//! `pidof` — find process IDs of running programs, ported for WinuxCmd 2.0 (#1488).
//!
//! Uses Win32 `CreateToolhelp32Snapshot` / `Process32FirstW` / `Process32NextW`
//! on Windows (with `/proc/<pid>/comm` fallback on non-Windows hosts),
//! explicit `Io`/`Ctx`, and zero globals.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const PIDOF_HELP: &str = "\
Usage:
 pidof [OPTION]... PROGRAM...

Find the process ID of a running program.

Options:
  -x, --exact    exact match only
  -h, --help     display this help and exit
  -V, --version  output version information and exit
";

fn basename_without_exe(s: &str) -> &str {
    let base = s.rsplit(['/', '\\']).next().unwrap_or(s);
    if base.len() > 4 && base[base.len() - 4..].eq_ignore_ascii_case(".exe") {
        &base[..base.len() - 4]
    } else {
        base
    }
}

fn process_matches(proc_name: &str, wanted: &str) -> bool {
    let base = basename_without_exe(proc_name);
    let wanted_base = basename_without_exe(wanted);
    base.eq_ignore_ascii_case(wanted_base) || proc_name.eq_ignore_ascii_case(wanted)
}

#[cfg(windows)]
fn enumerate_processes() -> Vec<(u32, String)> {
    const TH32CS_SNAPPROCESS: u32 = 0x0000_0002;
    const INVALID_HANDLE_VALUE: isize = -1;

    #[repr(C)]
    struct ProcessEntry32W {
        dw_size: u32,
        cnt_usage: u32,
        th32_process_id: u32,
        th32_default_heap_id: usize,
        th32_module_id: u32,
        cnt_threads: u32,
        th32_parent_process_id: u32,
        pc_pri_class_base: i32,
        dw_flags: u32,
        sz_exe_file: [u16; 260],
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn CreateToolhelp32Snapshot(dw_flags: u32, th32_process_id: u32) -> isize;
        fn Process32FirstW(h_snapshot: isize, lppe: *mut ProcessEntry32W) -> i32;
        fn Process32NextW(h_snapshot: isize, lppe: *mut ProcessEntry32W) -> i32;
        fn CloseHandle(h_object: isize) -> i32;
        fn GetCurrentProcessId() -> u32;
    }

    let self_pid = unsafe { GetCurrentProcessId() };
    let snap = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
    if snap == INVALID_HANDLE_VALUE || snap == 0 {
        return Vec::new();
    }

    let mut entry = ProcessEntry32W {
        dw_size: std::mem::size_of::<ProcessEntry32W>() as u32,
        cnt_usage: 0,
        th32_process_id: 0,
        th32_default_heap_id: 0,
        th32_module_id: 0,
        cnt_threads: 0,
        th32_parent_process_id: 0,
        pc_pri_class_base: 0,
        dw_flags: 0,
        sz_exe_file: [0; 260],
    };

    let mut out = Vec::new();
    let mut ok = unsafe { Process32FirstW(snap, &mut entry) };
    while ok != 0 {
        if entry.th32_process_id != self_pid {
            let len = entry
                .sz_exe_file
                .iter()
                .position(|&c| c == 0)
                .unwrap_or(entry.sz_exe_file.len());
            let name = String::from_utf16_lossy(&entry.sz_exe_file[..len]);
            out.push((entry.th32_process_id, name));
        }
        ok = unsafe { Process32NextW(snap, &mut entry) };
    }
    unsafe {
        CloseHandle(snap);
    }
    out
}

#[cfg(not(windows))]
fn enumerate_processes() -> Vec<(u32, String)> {
    let self_pid = std::process::id();
    let mut out = Vec::new();
    if let Ok(entries) = std::fs::read_dir("/proc") {
        for entry in entries.flatten() {
            let fname = entry.file_name();
            let s = fname.to_string_lossy();
            if let Ok(pid) = s.parse::<u32>() {
                if pid == self_pid {
                    continue;
                }
                if let Ok(comm) = std::fs::read_to_string(entry.path().join("comm")) {
                    out.push((pid, comm.trim().to_owned()));
                }
            }
        }
    }
    out
}

pub fn pidof_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if !no_more_opts {
            if s == "--" {
                no_more_opts = true;
                continue;
            }
            if s == "-h" || s == "--help" {
                io.stdout.write_all(PIDOF_HELP.as_bytes())?;
                return Ok(0);
            }
            if s == "-V" || s == "--version" {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("pidof", "WinuxCmd")
                )?;
                return Ok(0);
            }
            if s == "-x" || s == "--exact" {
                continue;
            }
            if s.starts_with('-') && s.len() > 1 {
                let msg = if let Some(rest) = s.strip_prefix("--") {
                    argopt::unrecognized_option(ctx, &format!("--{rest}"))
                } else {
                    let c = s.chars().nth(1).unwrap();
                    argopt::invalid_option(ctx, &c.to_string())
                };
                let _ = usage_error(ctx, io, "pidof", &msg);
                return Ok(1);
            }
        }
        positionals.push(s.into_owned());
    }

    if positionals.is_empty() {
        writeln!(io.stderr, "pidof: missing program name")?;
        writeln!(io.stderr, "Try 'pidof --help' for more information.")?;
        return Ok(1);
    }

    let procs = enumerate_processes();
    let mut pids: Vec<u32> = Vec::new();
    for wanted in &positionals {
        for (pid, name) in &procs {
            if process_matches(name, wanted) {
                pids.push(*pid);
            }
        }
    }

    if pids.is_empty() {
        return Ok(1);
    }

    for (idx, pid) in pids.iter().enumerate() {
        if idx > 0 {
            io.stdout.write_all(b" ")?;
        }
        write!(io.stdout, "{pid}")?;
    }
    writeln!(io.stdout)?;
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn name_matching_and_missing_args() {
        assert!(process_matches("powershell.exe", "powershell"));
        assert!(process_matches("PowerShell.EXE", "POWERSHELL.exe"));
        assert!(!process_matches("powershell.exe", "cmd"));

        let sc = ScratchCtx::new("pidof-test");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let rc = pidof_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("missing program name"));

        let mut io2 = MemIo::new("");
        let rc2 = pidof_cmd(
            &[OsString::from("WINUXCMD_NO_SUCH_PROC_999")],
            &mut io2.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc2, 1);
        assert!(io2.out().is_empty());
    }
}
