//! `pldd` — list dynamic shared objects / DLLs loaded into a process,
//! ported for WinuxCmd 2.0 (#1488).
//!
//! Uses `CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid)`,
//! `Module32FirstW`, and `Module32NextW` on Windows, with explicit `Io`/`Ctx`.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const PLDD_HELP: &str = "\
Usage: pldd [OPTION]... PID
List DLLs and executable modules loaded by a process.

  -n, --name     print module basenames instead of paths
  -h, --help     display this help and exit
  -V, --version  output version information and exit
";

#[cfg(windows)]
fn enumerate_modules(pid: u32) -> Result<Vec<(String, String)>, String> {
    const TH32CS_SNAPMODULE: u32 = 0x0000_0008;
    const TH32CS_SNAPMODULE32: u32 = 0x0000_0010;
    const INVALID_HANDLE_VALUE: isize = -1;

    #[repr(C)]
    struct ModuleEntry32W {
        dw_size: u32,
        th32_module_id: u32,
        th32_process_id: u32,
        glblcnt_usage: u32,
        proccnt_usage: u32,
        mod_base_addr: *mut u8,
        mod_base_size: u32,
        h_module: isize,
        sz_module: [u16; 256],
        sz_exe_path: [u16; 260],
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn CreateToolhelp32Snapshot(dw_flags: u32, th32_process_id: u32) -> isize;
        fn Module32FirstW(h_snapshot: isize, lpme: *mut ModuleEntry32W) -> i32;
        fn Module32NextW(h_snapshot: isize, lpme: *mut ModuleEntry32W) -> i32;
        fn CloseHandle(h_object: isize) -> i32;
    }

    fn utf16_z(buf: &[u16]) -> String {
        let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
        String::from_utf16_lossy(&buf[..len])
    }

    let snap = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid) };
    if snap == INVALID_HANDLE_VALUE || snap == 0 {
        return Err(std::io::Error::last_os_error().to_string());
    }

    let mut entry = ModuleEntry32W {
        dw_size: std::mem::size_of::<ModuleEntry32W>() as u32,
        th32_module_id: 0,
        th32_process_id: 0,
        glblcnt_usage: 0,
        proccnt_usage: 0,
        mod_base_addr: std::ptr::null_mut(),
        mod_base_size: 0,
        h_module: 0,
        sz_module: [0; 256],
        sz_exe_path: [0; 260],
    };

    let mut out = Vec::new();
    let mut ok = unsafe { Module32FirstW(snap, &mut entry) };
    while ok != 0 {
        let name = utf16_z(&entry.sz_module);
        let path = utf16_z(&entry.sz_exe_path);
        out.push((name, path));
        ok = unsafe { Module32NextW(snap, &mut entry) };
    }
    unsafe {
        CloseHandle(snap);
    }
    Ok(out)
}

#[cfg(not(windows))]
fn enumerate_modules(pid: u32) -> Result<Vec<(String, String)>, String> {
    let text = std::fs::read_to_string(format!("/proc/{pid}/maps")).map_err(|e| e.to_string())?;
    let mut seen = std::collections::BTreeSet::new();
    let mut out = Vec::new();
    for line in text.lines() {
        if let Some(path) = line.split_whitespace().nth(5) {
            if path.starts_with('/') && seen.insert(path.to_owned()) {
                let name = path.rsplit('/').next().unwrap_or(path).to_owned();
                out.push((name, path.to_owned()));
            }
        }
    }
    Ok(out)
}

pub fn pldd_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut names_only = false;
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if no_more_opts {
            positionals.push(s.into_owned());
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            continue;
        }
        if s == "-h" || s == "--help" {
            io.stdout.write_all(PLDD_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "-V" || s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("pldd", "WinuxCmd")
            )?;
            return Ok(0);
        }
        if s == "-n" || s == "--name" {
            names_only = true;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "pldd", &msg);
            return Ok(1);
        }
        positionals.push(s.into_owned());
    }

    if positionals.len() != 1 {
        writeln!(io.stderr, "pldd: exactly one PID is required")?;
        writeln!(io.stderr, "Try 'pldd --help' for more information.")?;
        return Ok(1);
    }

    let pid_str = &positionals[0];
    let Ok(pid) = pid_str.parse::<u32>() else {
        writeln!(io.stderr, "pldd: invalid process ID '{pid_str}'")?;
        return Ok(1);
    };
    if pid == 0 {
        writeln!(io.stderr, "pldd: invalid process ID '{pid_str}'")?;
        return Ok(1);
    }

    match enumerate_modules(pid) {
        Ok(mods) => {
            for (name, path) in mods {
                writeln!(io.stdout, "{}", if names_only { name } else { path })?;
            }
            Ok(0)
        }
        Err(e) => {
            writeln!(io.stderr, "pldd: {pid}: {e}")?;
            Ok(1)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn validation_and_self_pid() {
        let sc = ScratchCtx::new("pldd-test");
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let rc = pldd_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("exactly one PID is required"));

        let mut io_bad = MemIo::new("");
        let rc_bad = pldd_cmd(&[OsString::from("abc")], &mut io_bad.split(), &ctx).unwrap();
        assert_eq!(rc_bad, 1);
        assert!(io_bad.err().contains("invalid process ID 'abc'"));

        let self_pid = std::process::id().to_string();
        let mut io_self = MemIo::new("");
        let rc_self = pldd_cmd(&[OsString::from(self_pid)], &mut io_self.split(), &ctx).unwrap();
        assert_eq!(rc_self, 0);
        assert!(!io_self.out().is_empty());
    }
}
