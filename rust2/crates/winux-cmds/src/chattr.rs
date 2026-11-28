//! `chattr` and `lsattr` — Windows file attribute utilities, ported for WinuxCmd 2.0 (#1488).
//!
//! Uses `GetFileAttributesW` / `SetFileAttributesW` on Windows (with
//! read-only metadata fallback on non-Windows hosts), explicit `Io`/`Ctx`,
//! and zero globals.

use std::ffi::OsString;
use std::io::Write;
use std::path::{Path, PathBuf};

use crate::argopt;
use crate::fsutil;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const FILE_ATTRIBUTE_READONLY: u32 = 0x0000_0001;
const FILE_ATTRIBUTE_HIDDEN: u32 = 0x0000_0002;
const FILE_ATTRIBUTE_SYSTEM: u32 = 0x0000_0004;
const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x0000_0010;
const FILE_ATTRIBUTE_ARCHIVE: u32 = 0x0000_0020;
const FILE_ATTRIBUTE_TEMPORARY: u32 = 0x0000_0100;
const FILE_ATTRIBUTE_SPARSE_FILE: u32 = 0x0000_0200;
const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x0000_0400;
const FILE_ATTRIBUTE_COMPRESSED: u32 = 0x0000_0800;
const FILE_ATTRIBUTE_OFFLINE: u32 = 0x0000_1000;
const FILE_ATTRIBUTE_NOT_CONTENT_INDEXED: u32 = 0x0000_2000;
const FILE_ATTRIBUTE_ENCRYPTED: u32 = 0x0000_4000;

const MODIFIABLE_MASK: u32 = FILE_ATTRIBUTE_READONLY
    | FILE_ATTRIBUTE_HIDDEN
    | FILE_ATTRIBUTE_SYSTEM
    | FILE_ATTRIBUTE_ARCHIVE
    | FILE_ATTRIBUTE_NOT_CONTENT_INDEXED
    | FILE_ATTRIBUTE_TEMPORARY
    | FILE_ATTRIBUTE_OFFLINE;

fn bit_for_flag(flag: u8) -> Option<u32> {
    match flag {
        b'R' | b'r' => Some(FILE_ATTRIBUTE_READONLY),
        b'H' | b'h' => Some(FILE_ATTRIBUTE_HIDDEN),
        b'S' | b's' => Some(FILE_ATTRIBUTE_SYSTEM),
        b'A' | b'a' => Some(FILE_ATTRIBUTE_ARCHIVE),
        b'I' | b'i' => Some(FILE_ATTRIBUTE_NOT_CONTENT_INDEXED),
        b'T' | b't' => Some(FILE_ATTRIBUTE_TEMPORARY),
        b'O' | b'o' => Some(FILE_ATTRIBUTE_OFFLINE),
        _ => None,
    }
}

fn format_attrs(attrs: u32) -> String {
    let mut out = String::with_capacity(12);
    out.push(if attrs & FILE_ATTRIBUTE_READONLY != 0 { 'R' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_HIDDEN != 0 { 'H' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_SYSTEM != 0 { 'S' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_ARCHIVE != 0 { 'A' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_DIRECTORY != 0 { 'D' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_COMPRESSED != 0 { 'C' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_ENCRYPTED != 0 { 'E' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_NOT_CONTENT_INDEXED != 0 { 'I' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_TEMPORARY != 0 { 'T' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_OFFLINE != 0 { 'O' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_SPARSE_FILE != 0 { 'P' } else { '-' });
    out.push(if attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0 { 'L' } else { '-' });
    out
}

fn format_attrs_long(attrs: u32) -> String {
    let flags = [
        (FILE_ATTRIBUTE_READONLY, "readonly"),
        (FILE_ATTRIBUTE_HIDDEN, "hidden"),
        (FILE_ATTRIBUTE_SYSTEM, "system"),
        (FILE_ATTRIBUTE_ARCHIVE, "archive"),
        (FILE_ATTRIBUTE_DIRECTORY, "directory"),
        (FILE_ATTRIBUTE_COMPRESSED, "compressed"),
        (FILE_ATTRIBUTE_ENCRYPTED, "encrypted"),
        (FILE_ATTRIBUTE_NOT_CONTENT_INDEXED, "not-indexed"),
        (FILE_ATTRIBUTE_TEMPORARY, "temporary"),
        (FILE_ATTRIBUTE_OFFLINE, "offline"),
        (FILE_ATTRIBUTE_SPARSE_FILE, "sparse"),
        (FILE_ATTRIBUTE_REPARSE_POINT, "reparse-point"),
    ];
    let mut parts = Vec::new();
    for (bit, name) in flags {
        if attrs & bit != 0 {
            parts.push(name);
        }
    }
    if parts.is_empty() {
        "none".to_owned()
    } else {
        parts.join(", ")
    }
}

#[cfg(windows)]
mod sys {
    use std::io;
    use std::os::windows::ffi::OsStrExt;
    use std::path::Path;

    const INVALID_FILE_ATTRIBUTES: u32 = 0xFFFF_FFFF;

    #[link(name = "kernel32")]
    extern "system" {
        fn GetFileAttributesW(lp_file_name: *const u16) -> u32;
        fn SetFileAttributesW(lp_file_name: *const u16, dw_file_attributes: u32) -> i32;
    }

    fn to_wide(path: &Path) -> Vec<u16> {
        let mut w: Vec<u16> = path.as_os_str().encode_wide().collect();
        w.push(0);
        w
    }

    pub fn get_attrs(path: &Path) -> io::Result<u32> {
        let w = to_wide(path);
        let a = unsafe { GetFileAttributesW(w.as_ptr()) };
        if a == INVALID_FILE_ATTRIBUTES {
            Err(io::Error::last_os_error())
        } else {
            Ok(a)
        }
    }

    pub fn set_attrs(path: &Path, attrs: u32) -> io::Result<()> {
        let w = to_wide(path);
        if unsafe { SetFileAttributesW(w.as_ptr(), attrs) } == 0 {
            Err(io::Error::last_os_error())
        } else {
            Ok(())
        }
    }
}

#[cfg(not(windows))]
mod sys {
    use super::{FILE_ATTRIBUTE_DIRECTORY, FILE_ATTRIBUTE_READONLY, FILE_ATTRIBUTE_REPARSE_POINT};
    use std::io;
    use std::path::Path;

    pub fn get_attrs(path: &Path) -> io::Result<u32> {
        let meta = std::fs::symlink_metadata(path)?;
        let mut a = 0u32;
        if meta.permissions().readonly() {
            a |= FILE_ATTRIBUTE_READONLY;
        }
        if meta.is_dir() {
            a |= FILE_ATTRIBUTE_DIRECTORY;
        }
        if meta.file_type().is_symlink() {
            a |= FILE_ATTRIBUTE_REPARSE_POINT;
        }
        Ok(a)
    }

    pub fn set_attrs(path: &Path, attrs: u32) -> io::Result<()> {
        let meta = std::fs::metadata(path)?;
        let mut perms = meta.permissions();
        perms.set_readonly(attrs & FILE_ATTRIBUTE_READONLY != 0);
        std::fs::set_permissions(path, perms)
    }
}

struct ChattrCfg {
    recursive: bool,
    verbose: bool,
    force: bool,
    op: u8,
    set_bits: u32,
    clear_bits: u32,
}

fn apply_one(cfg: &ChattrCfg, path: &Path, display: &str, io: &mut Io) -> bool {
    let attrs = match sys::get_attrs(path) {
        Ok(a) => a,
        Err(e) => {
            if !cfg.force {
                let _ = writeln!(io.stderr, "chattr: cannot access '{display}': {}", fsutil::reason(&e));
            }
            return false;
        }
    };
    let new_attrs = if cfg.op == b'=' {
        (attrs & !MODIFIABLE_MASK) | cfg.set_bits
    } else {
        (attrs | cfg.set_bits) & !cfg.clear_bits
    };
    if new_attrs == attrs {
        return true;
    }
    if let Err(e) = sys::set_attrs(path, new_attrs) {
        if !cfg.force {
            let _ = writeln!(
                io.stderr,
                "chattr: failed to set attributes for '{display}': {}",
                fsutil::reason(&e)
            );
        }
        return false;
    }
    if cfg.verbose {
        let _ = writeln!(io.stdout, "chattr: changed attributes of '{display}'");
    }
    true
}

fn apply_recursive(cfg: &ChattrCfg, path: &Path, display: &str, io: &mut Io) -> bool {
    let mut ok = apply_one(cfg, path, display, io);
    let attrs = match sys::get_attrs(path) {
        Ok(a) => a,
        Err(_) => return ok,
    };
    if (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 || (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 {
        return ok;
    }
    if let Ok(entries) = std::fs::read_dir(path) {
        for entry in entries.flatten() {
            let child_path: PathBuf = entry.path();
            let child_disp = child_path.to_string_lossy().into_owned();
            ok = apply_recursive(cfg, &child_path, &child_disp, io) && ok;
        }
    }
    ok
}

pub fn chattr_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut cfg = ChattrCfg {
        recursive: false,
        verbose: false,
        force: false,
        op: 0,
        set_bits: 0,
        clear_bits: 0,
    };
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if no_more_opts || !positionals.is_empty() {
            positionals.push(s.into_owned());
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            continue;
        }
        if s == "--help" || s == "-h" {
            writeln!(io.stdout, "Usage: chattr [-RVf] MODE FILE...")?;
            return Ok(0);
        }
        if s == "--recursive" || s == "-R" {
            cfg.recursive = true;
            continue;
        }
        if s == "--verbose" || s == "-V" {
            cfg.verbose = true;
            continue;
        }
        if s == "--force" || s == "-f" {
            cfg.force = true;
            continue;
        }
        if s.starts_with("--") {
            let _ = usage_error(ctx, io, "chattr", &argopt::unrecognized_option(ctx, &s));
            return Ok(1);
        }
        // Mode strings like `+RH`, `-H`, `=A` are positional operands, not options.
        positionals.push(s.into_owned());
    }

    if positionals.len() < 2 {
        writeln!(io.stderr, "chattr: missing mode or file operand")?;
        writeln!(io.stderr, "Try 'chattr --help' for more information.")?;
        return Ok(1);
    }

    let mode = positionals[0].as_bytes();
    if mode.len() < 2 || !matches!(mode[0], b'+' | b'-' | b'=') {
        writeln!(io.stderr, "chattr: mode must start with '+', '-' or '='")?;
        return Ok(1);
    }
    cfg.op = mode[0];
    for &b in &mode[1..] {
        let Some(bit) = bit_for_flag(b) else {
            writeln!(io.stderr, "chattr: unsupported attribute '{}'", b as char)?;
            return Ok(1);
        };
        if cfg.op == b'-' {
            cfg.clear_bits |= bit;
        } else {
            cfg.set_bits |= bit;
        }
    }

    let mut ok = true;
    for raw in &positionals[1..] {
        let resolved = fsutil::resolve(ctx, raw);
        let res = if cfg.recursive {
            apply_recursive(&cfg, &resolved, raw, io)
        } else {
            apply_one(&cfg, &resolved, raw, io)
        };
        ok = res && ok;
    }
    Ok(if ok { 0 } else { 1 })
}

fn list_dir_children(
    path: &Path,
    recursive: bool,
    all: bool,
    long_names: bool,
    io: &mut Io,
    ok: &mut bool,
) {
    let entries = match std::fs::read_dir(path) {
        Ok(e) => e,
        Err(e) => {
            let _ = writeln!(
                io.stderr,
                "lsattr: cannot read directory '{}': {}",
                path.to_string_lossy(),
                fsutil::reason(&e)
            );
            *ok = false;
            return;
        }
    };
    if all {
        for special in [".", ".."] {
            let p = path.join(special);
            if let Ok(a) = sys::get_attrs(&p) {
                let fmt = if long_names {
                    format_attrs_long(a)
                } else {
                    format_attrs(a)
                };
                let _ = writeln!(io.stdout, "{fmt} {}", p.to_string_lossy());
            }
        }
    }
    for entry in entries.flatten() {
        let child = entry.path();
        let attrs = match sys::get_attrs(&child) {
            Ok(a) => a,
            Err(_) => continue,
        };
        let fmt = if long_names {
            format_attrs_long(attrs)
        } else {
            format_attrs(attrs)
        };
        let _ = writeln!(io.stdout, "{fmt} {}", child.to_string_lossy());
        let is_dir = attrs & FILE_ATTRIBUTE_DIRECTORY != 0;
        let is_link = attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0;
        if recursive && is_dir && !is_link {
            list_dir_children(&child, true, all, long_names, io, ok);
        }
    }
}

pub fn lsattr_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut recursive = false;
    let mut directory = false;
    let mut all = false;
    let mut long_names = false;
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
        if s == "--help" || s == "-h" {
            writeln!(io.stdout, "Usage: lsattr [-RVadl] [FILE]...")?;
            return Ok(0);
        }
        if s == "--version" || s == "-V" {
            writeln!(io.stdout, "lsattr (WinuxCmd) 0.1.0")?;
            return Ok(0);
        }
        if s.starts_with("--") {
            match s.as_ref() {
                "--recursive" => recursive = true,
                "--directory" => directory = true,
                "--all" => all = true,
                "--long" => long_names = true,
                _ => {
                    let _ = usage_error(ctx, io, "lsattr", &argopt::unrecognized_option(ctx, &s));
                    return Ok(1);
                }
            }
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            for ch in s[1..].chars() {
                match ch {
                    'R' => recursive = true,
                    'd' => directory = true,
                    'a' => all = true,
                    'l' => long_names = true,
                    'V' => {
                        writeln!(io.stdout, "lsattr (WinuxCmd) 0.1.0")?;
                        return Ok(0);
                    }
                    other => {
                        let _ = usage_error(
                            ctx,
                            io,
                            "lsattr",
                            &argopt::invalid_option(ctx, &other.to_string()),
                        );
                        return Ok(1);
                    }
                }
            }
            continue;
        }
        positionals.push(s.into_owned());
    }

    if positionals.is_empty() {
        positionals.push(".".to_owned());
    }

    let mut ok = true;
    for raw in &positionals {
        let resolved = fsutil::resolve(ctx, raw);
        let attrs = match sys::get_attrs(&resolved) {
            Ok(a) => a,
            Err(e) => {
                writeln!(io.stderr, "lsattr: cannot access '{raw}': {}", fsutil::reason(&e))?;
                ok = false;
                continue;
            }
        };
        if (attrs & FILE_ATTRIBUTE_DIRECTORY) == 0 || directory {
            let fmt = if long_names {
                format_attrs_long(attrs)
            } else {
                format_attrs(attrs)
            };
            writeln!(io.stdout, "{fmt} {raw}")?;
        } else {
            list_dir_children(&resolved, recursive, all, long_names, io, &mut ok);
        }
    }

    Ok(if ok { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn format_helpers_and_chattr_lsattr_roundtrip() {
        assert_eq!(format_attrs(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN), "RH----------");
        assert_eq!(
            format_attrs_long(FILE_ATTRIBUTE_READONLY | FILE_ATTRIBUTE_HIDDEN),
            "readonly, hidden"
        );
        assert_eq!(format_attrs_long(0), "none");

        let sc = ScratchCtx::new("chattr-test");
        std::fs::write(sc.path("f.txt"), b"hi").unwrap();
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let rc = chattr_cmd(
            &[OsString::from("+R"), OsString::from("f.txt")],
            &mut io.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc, 0);

        let mut io_ls = MemIo::new("");
        let rc_ls = lsattr_cmd(
            &[OsString::from("-l"), OsString::from("f.txt")],
            &mut io_ls.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc_ls, 0);
        assert!(io_ls.out().contains("readonly"));

        let mut io_clr = MemIo::new("");
        let rc_clr = chattr_cmd(
            &[OsString::from("-R"), OsString::from("f.txt")],
            &mut io_clr.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc_clr, 0);
    }
}
