//! `mkpasswd` and `mkgroup` — Cygwin-style passwd/group entry generators,
//! ported for WinuxCmd 2.0 (#1488).
//!
//! Uses `winsys::imp::process_identity()` for `--current` and `netapi32`
//! (`NetUserEnum` / `NetLocalGroupEnum`) for `--local`, with explicit `Io`/`Ctx`.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::gnuhelp;
use crate::winsys;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const MKPASSWD_HELP: &str = "\
Usage: mkpasswd [-c] [-l] [-p PREFIX] [-s SHELL] [-P FD] [-S METHOD]
Generate passwd-like entries from Windows accounts.

  -c, --current      print only the current process user
  -l, --local        enumerate local machine users (default)
  -p, --path-prefix  home directory prefix (default /home)
  -s, --shell        login shell path (default /bin/sh)
  -h, --help         display this help and exit
  -V, --version      output version information and exit
";

const MKGROUP_HELP: &str = "\
Usage: mkgroup [OPTION]...
Generate group-like entries from Windows groups.

  -c, --current  print groups from the current process token
  -l, --local    enumerate local machine groups (default)
  -h, --help     display this help and exit
  -V, --version  output version information and exit
";

fn sanitize_field(value: &str) -> String {
    value.replace(':', "_")
}

fn format_passwd_line(name: &str, id: &str, prefix: &str, shell: &str) -> String {
    let uname = sanitize_field(if name.is_empty() { id } else { name });
    let uid = if id.is_empty() { "0" } else { id };
    format!("{uname}:*:{uid}:{uid}:Windows user:{prefix}/{uname}:{shell}")
}

fn format_group_line(name: &str, id: &str) -> String {
    let gname = sanitize_field(if name.is_empty() { id } else { name });
    let gid = if id.is_empty() { "0" } else { id };
    format!("{gname}:*:{gid}:")
}

#[cfg(windows)]
mod sys {
    use super::{format_group_line, format_passwd_line};
    use crate::winsys;

    const NERR_SUCCESS: u32 = 0;
    const ERROR_MORE_DATA: u32 = 234;
    const MAX_PREFERRED_LENGTH: u32 = 0xFFFF_FFFF;
    const FILTER_NORMAL_ACCOUNT: u32 = 0x0002;

    #[link(name = "netapi32")]
    extern "system" {
        fn NetUserEnum(
            servername: *const u16,
            level: u32,
            filter: u32,
            bufptr: *mut *mut *const u16,
            prefmaxlen: u32,
            entriesread: *mut u32,
            totalentries: *mut u32,
            resume_handle: *mut u32,
        ) -> u32;
        fn NetLocalGroupEnum(
            servername: *const u16,
            level: u32,
            bufptr: *mut *mut *const u16,
            prefmaxlen: u32,
            entriesread: *mut u32,
            totalentries: *mut u32,
            resume_handle: *mut usize,
        ) -> u32;
        fn NetApiBufferFree(buffer: *mut u8) -> u32;
    }

    unsafe fn utf16_ptr_to_string(p: *const u16) -> String {
        if p.is_null() {
            return String::new();
        }
        let mut len = 0usize;
        while *p.add(len) != 0 {
            len += 1;
        }
        String::from_utf16_lossy(std::slice::from_raw_parts(p, len))
    }

    pub fn local_passwd_lines(prefix: &str, shell: &str) -> Result<Vec<String>, u32> {
        let mut resume = 0u32;
        let mut lines = Vec::new();
        loop {
            let mut raw: *mut *const u16 = std::ptr::null_mut();
            let mut entries = 0u32;
            let mut total = 0u32;
            let status = unsafe {
                NetUserEnum(
                    std::ptr::null(),
                    0,
                    FILTER_NORMAL_ACCOUNT,
                    &mut raw,
                    MAX_PREFERRED_LENGTH,
                    &mut entries,
                    &mut total,
                    &mut resume,
                )
            };
            if status != NERR_SUCCESS && status != ERROR_MORE_DATA {
                if !raw.is_null() {
                    unsafe {
                        NetApiBufferFree(raw as *mut u8);
                    }
                }
                return Err(status);
            }
            for i in 0..(entries as usize) {
                let name = unsafe { utf16_ptr_to_string(*raw.add(i)) };
                let id = winsys::imp::account_id_from_name(&name).unwrap_or_else(|| "0".to_owned());
                lines.push(format_passwd_line(&name, &id, prefix, shell));
            }
            if !raw.is_null() {
                unsafe {
                    NetApiBufferFree(raw as *mut u8);
                }
            }
            if status != ERROR_MORE_DATA {
                break;
            }
        }
        Ok(lines)
    }

    pub fn local_group_lines() -> Result<Vec<String>, u32> {
        let mut resume = 0usize;
        let mut lines = Vec::new();
        loop {
            let mut raw: *mut *const u16 = std::ptr::null_mut();
            let mut entries = 0u32;
            let mut total = 0u32;
            let status = unsafe {
                NetLocalGroupEnum(
                    std::ptr::null(),
                    0,
                    &mut raw,
                    MAX_PREFERRED_LENGTH,
                    &mut entries,
                    &mut total,
                    &mut resume,
                )
            };
            if status != NERR_SUCCESS && status != ERROR_MORE_DATA {
                if !raw.is_null() {
                    unsafe {
                        NetApiBufferFree(raw as *mut u8);
                    }
                }
                return Err(status);
            }
            for i in 0..(entries as usize) {
                let name = unsafe { utf16_ptr_to_string(*raw.add(i)) };
                let id = winsys::imp::account_id_from_name(&name).unwrap_or_else(|| "0".to_owned());
                lines.push(format_group_line(&name, &id));
            }
            if !raw.is_null() {
                unsafe {
                    NetApiBufferFree(raw as *mut u8);
                }
            }
            if status != ERROR_MORE_DATA {
                break;
            }
        }
        Ok(lines)
    }
}

#[cfg(not(windows))]
mod sys {
    use super::{format_group_line, format_passwd_line};
    use crate::winsys;

    pub fn local_passwd_lines(prefix: &str, shell: &str) -> Result<Vec<String>, u32> {
        let name = winsys::imp::user_name().unwrap_or_else(|| "user".to_owned());
        Ok(vec![format_passwd_line(&name, "1000", prefix, shell)])
    }

    pub fn local_group_lines() -> Result<Vec<String>, u32> {
        Ok(vec![format_group_line("users", "100")])
    }
}

pub fn mkpasswd_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut current = false;
    let mut path_prefix = String::from("/home");
    let mut shell = String::from("/bin/sh");
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;
    let mut i = 0usize;

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
            io.stdout.write_all(MKPASSWD_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "-V" || s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("mkpasswd", "WinuxCmd")
            )?;
            return Ok(0);
        }
        if s == "-c" || s == "--current" {
            current = true;
            i += 1;
            continue;
        }
        if s == "-l" || s == "--local" {
            i += 1;
            continue;
        }
        if s == "-P" || s == "--password-fd" || s.starts_with("--password-fd=") {
            writeln!(
                io.stderr,
                "mkpasswd: --password-fd is not supported on Windows [DIFFERS from GNU mkpasswd]"
            )?;
            return Ok(1);
        }
        if s == "-S" || s == "--method" || s.starts_with("--method=") {
            writeln!(
                io.stderr,
                "mkpasswd: --method is not supported on Windows [DIFFERS from GNU mkpasswd]"
            )?;
            return Ok(1);
        }
        if s == "-p" || s == "--path-prefix" {
            i += 1;
            if i >= args.len() {
                let _ = usage_error(
                    ctx,
                    io,
                    "mkpasswd",
                    &argopt::option_requires_argument(ctx, &s),
                );
                return Ok(1);
            }
            path_prefix = args[i].to_string_lossy().into_owned();
            i += 1;
            continue;
        }
        if let Some(val) = s.strip_prefix("--path-prefix=") {
            path_prefix = val.to_owned();
            i += 1;
            continue;
        }
        if s == "-s" || s == "--shell" {
            i += 1;
            if i >= args.len() {
                let _ = usage_error(
                    ctx,
                    io,
                    "mkpasswd",
                    &argopt::option_requires_argument(ctx, &s),
                );
                return Ok(1);
            }
            shell = args[i].to_string_lossy().into_owned();
            i += 1;
            continue;
        }
        if let Some(val) = s.strip_prefix("--shell=") {
            shell = val.to_owned();
            i += 1;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "mkpasswd", &msg);
            return Ok(1);
        }
        positionals.push(s.into_owned());
        i += 1;
    }

    if let Some(first) = positionals.first() {
        writeln!(io.stderr, "mkpasswd: unexpected operand '{first}'")?;
        return Ok(1);
    }

    if current {
        let Some((user, _, _)) = winsys::imp::process_identity() else {
            writeln!(io.stderr, "mkpasswd: cannot query current user")?;
            return Ok(1);
        };
        writeln!(
            io.stdout,
            "{}",
            format_passwd_line(&user.name, &user.id, &path_prefix, &shell)
        )?;
        return Ok(0);
    }

    match sys::local_passwd_lines(&path_prefix, &shell) {
        Ok(lines) if !lines.is_empty() => {
            for line in lines {
                writeln!(io.stdout, "{line}")?;
            }
            Ok(0)
        }
        Ok(_) => Ok(1),
        Err(code) => {
            writeln!(io.stderr, "mkpasswd: NetUserEnum failed: error {code}")?;
            Ok(1)
        }
    }
}

pub fn mkgroup_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut current = false;
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
            io.stdout.write_all(MKGROUP_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "-V" || s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("mkgroup", "WinuxCmd")
            )?;
            return Ok(0);
        }
        if s == "-c" || s == "--current" {
            current = true;
            continue;
        }
        if s == "-l" || s == "--local" {
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "mkgroup", &msg);
            return Ok(1);
        }
        positionals.push(s.into_owned());
    }

    if let Some(first) = positionals.first() {
        writeln!(io.stderr, "mkgroup: unexpected operand '{first}'")?;
        return Ok(1);
    }

    if current {
        let Some((_, _, groups)) = winsys::imp::process_identity() else {
            writeln!(io.stderr, "mkgroup: cannot query current token")?;
            return Ok(1);
        };
        if groups.len() <= 1 {
            return Ok(1);
        }
        for g in &groups[1..] {
            writeln!(io.stdout, "{}", format_group_line(&g.name, &g.id))?;
        }
        return Ok(0);
    }

    match sys::local_group_lines() {
        Ok(lines) if !lines.is_empty() => {
            for line in lines {
                writeln!(io.stdout, "{line}")?;
            }
            Ok(0)
        }
        Ok(_) => Ok(1),
        Err(code) => {
            writeln!(io.stderr, "mkgroup: NetLocalGroupEnum failed: error {code}")?;
            Ok(1)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn formatting_and_unsupported_options() {
        assert_eq!(
            format_passwd_line("alice:bob", "1001", "/u", "/bin/false"),
            "alice_bob:*:1001:1001:Windows user:/u/alice_bob:/bin/false"
        );
        assert_eq!(format_group_line("devs:1", "544"), "devs_1:*:544:");

        let sc = ScratchCtx::new("mkpasswd-test");
        let ctx = sc.ctx();

        let mut io1 = MemIo::new("");
        let rc1 = mkpasswd_cmd(
            &[OsString::from("-P"), OsString::from("3")],
            &mut io1.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc1, 1);
        assert!(io1.err().contains("not supported on Windows"));

        let mut io2 = MemIo::new("");
        let rc2 = mkgroup_cmd(&[OsString::from("extra")], &mut io2.split(), &ctx).unwrap();
        assert_eq!(rc2, 1);
        assert!(io2.err().contains("unexpected operand 'extra'"));
    }
}
