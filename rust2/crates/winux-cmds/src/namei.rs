//! `namei` — follow a pathname until a terminal point is found (#1488).
//!
//! Port of `src/commands/namei.cpp` (util-linux 2.39.3 option set + #1237
//! POSIX drive-dialect folding via `fsutil::resolve`). Explicit `Io`/`Ctx`,
//! zero globals.

use std::ffi::OsString;
use std::io::Write;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::fsutil;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('l'),
        long: "long",
        takes_value: false,
    },
    OptSpec {
        short: Some('m'),
        long: "modes",
        takes_value: false,
    },
    OptSpec {
        short: Some('n'),
        long: "nosymlinks",
        takes_value: false,
    },
    OptSpec {
        short: Some('o'),
        long: "owners",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "vertical",
        takes_value: false,
    },
    OptSpec {
        short: Some('x'),
        long: "mountpoints",
        takes_value: false,
    },
    OptSpec {
        short: Some('Z'),
        long: "context",
        takes_value: false,
    },
];

struct Config {
    modes: bool,
    owners: bool,
    vertical: bool,
    _nosymlinks: bool,
    mountpoints: bool,
    context: bool,
    paths: Vec<String>,
}

struct WalkStep {
    prefix: String,
    name: String,
}

struct StepInfo {
    name: String,
    exists: bool,
    is_dir: bool,
    readonly: bool,
    is_symlink: bool,
    is_mount: bool,
    target: Option<String>,
    owner: String,
    group: String,
}

fn is_sep(b: u8) -> bool {
    b == b'/' || b == b'\\'
}

fn native_walk_steps(native: &str) -> Vec<WalkStep> {
    let mut steps = Vec::new();
    let bytes = native.as_bytes();
    if bytes.is_empty() {
        return steps;
    }

    let mut pos = 0usize;
    if bytes.len() >= 2 && bytes[1] == b':' && bytes[0].is_ascii_alphabetic() {
        pos = if bytes.len() > 2 && is_sep(bytes[2]) {
            3
        } else {
            2
        };
        let mut root = native[..pos].to_owned();
        if let Some(first) = root.get_mut(0..1) {
            first.make_ascii_uppercase();
        }
        steps.push(WalkStep {
            prefix: root.clone(),
            name: root,
        });
    } else if is_sep(bytes[0]) {
        if bytes.len() >= 2 && is_sep(bytes[1]) {
            let mut p = 2usize;
            while p < bytes.len() && !is_sep(bytes[p]) {
                p += 1;
            }
            if p < bytes.len() {
                p += 1;
                while p < bytes.len() && !is_sep(bytes[p]) {
                    p += 1;
                }
            }
            pos = p;
        } else {
            pos = 1;
        }
        let root = native[..pos].to_owned();
        steps.push(WalkStep {
            prefix: root.clone(),
            name: root,
        });
    }

    while pos < bytes.len() {
        while pos < bytes.len() && is_sep(bytes[pos]) {
            pos += 1;
        }
        let start = pos;
        while pos < bytes.len() && !is_sep(bytes[pos]) {
            pos += 1;
        }
        if start == pos {
            break;
        }
        steps.push(WalkStep {
            prefix: native[..pos].to_owned(),
            name: native[start..pos].to_owned(),
        });
    }
    steps
}

fn step_is_root(step: &WalkStep) -> bool {
    if step.name != step.prefix {
        return false;
    }
    let name = step.name.as_str();
    if name == "/" {
        return true;
    }
    let b = name.as_bytes();
    if b.len() == 2 && b[1] == b':' {
        return true;
    }
    if name.ends_with('\\') {
        return true;
    }
    if name.starts_with("\\\\") {
        return true;
    }
    false
}

fn mode_string(info: &StepInfo) -> String {
    let is_dir = info.is_dir && !info.is_symlink;
    let mut md = String::with_capacity(10);
    md.push(if info.is_symlink {
        'l'
    } else if is_dir {
        'd'
    } else {
        '-'
    });
    if info.is_symlink {
        md.push_str("rwxrwxrwx");
        return md;
    }
    let mut exec = is_dir;
    if !exec {
        if let Some((_, ext)) = info.name.rsplit_once('.') {
            let lower = ext.to_ascii_lowercase();
            exec = matches!(
                lower.as_str(),
                "exe" | "com" | "bat" | "cmd" | "ps1" | "msi"
            );
        }
    }
    md.push('r');
    md.push(if info.readonly { '-' } else { 'w' });
    md.push(if exec { 'x' } else { '-' });
    md.push_str("r-");
    md.push(if exec { 'x' } else { '-' });
    md.push_str("r-");
    md.push(if exec { 'x' } else { '-' });
    md
}

#[cfg(windows)]
mod sys {
    use super::StepInfo;
    use crate::fs_win;
    use std::ffi::c_void;
    use std::os::windows::ffi::OsStrExt;
    use std::path::Path;
    use winux_core::Ctx;

    const INVALID_FILE_ATTRIBUTES: u32 = 0xFFFF_FFFF;
    const FILE_ATTRIBUTE_READONLY: u32 = 0x0000_0001;
    const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x0000_0010;
    const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x0000_0400;
    const FILE_SHARE_READ: u32 = 0x0000_0001;
    const FILE_SHARE_WRITE: u32 = 0x0000_0002;
    const FILE_SHARE_DELETE: u32 = 0x0000_0004;
    const OPEN_EXISTING: u32 = 3;
    const FILE_FLAG_OPEN_REPARSE_POINT: u32 = 0x0020_0000;
    const FILE_FLAG_BACKUP_SEMANTICS: u32 = 0x0200_0000;
    const FSCTL_GET_REPARSE_POINT: u32 = 0x0009_00A8;
    const IO_REPARSE_TAG_MOUNT_POINT: u32 = 0xA000_0003;
    const VOLUME_NAME_DOS: u32 = 0;

    extern "system" {
        fn GetFileAttributesW(lp_file_name: *const u16) -> u32;
        fn CreateFileW(
            lp_file_name: *const u16,
            dw_desired_access: u32,
            dw_share_mode: u32,
            lp_security_attributes: *mut c_void,
            dw_creation_disposition: u32,
            dw_flags_and_attributes: u32,
            h_template_file: *mut c_void,
        ) -> *mut c_void;
        fn DeviceIoControl(
            h_device: *mut c_void,
            dw_io_control_code: u32,
            lp_in_buffer: *mut c_void,
            n_in_buffer_size: u32,
            lp_out_buffer: *mut c_void,
            n_out_buffer_size: u32,
            lp_bytes_returned: *mut u32,
            lp_overlapped: *mut c_void,
        ) -> i32;
        fn GetFinalPathNameByHandleW(
            h_file: *mut c_void,
            lpsz_file_path: *mut u16,
            cch_file_path: u32,
            dw_flags: u32,
        ) -> u32;
        fn CloseHandle(h_object: *mut c_void) -> i32;
    }

    pub fn probe_step(ctx: &Ctx, prefix: &str, name: &str, want_owners: bool) -> StepInfo {
        let mut info = StepInfo {
            name: name.to_owned(),
            exists: false,
            is_dir: false,
            readonly: false,
            is_symlink: false,
            is_mount: false,
            target: None,
            owner: String::new(),
            group: String::new(),
        };
        let mut w: Vec<u16> = Path::new(prefix).as_os_str().encode_wide().collect();
        w.push(0);
        let attrs = unsafe { GetFileAttributesW(w.as_ptr()) };
        if attrs == INVALID_FILE_ATTRIBUTES {
            return info;
        }
        info.exists = true;
        info.is_dir = (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
        info.readonly = (attrs & FILE_ATTRIBUTE_READONLY) != 0;

        if (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 {
            let h = unsafe {
                CreateFileW(
                    w.as_ptr(),
                    0,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    std::ptr::null_mut(),
                    OPEN_EXISTING,
                    FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                    std::ptr::null_mut(),
                )
            };
            if h as isize != -1 {
                let mut buf = [0u8; 16384];
                let mut returned = 0u32;
                let mut tag = 0u32;
                let ok = unsafe {
                    DeviceIoControl(
                        h,
                        FSCTL_GET_REPARSE_POINT,
                        std::ptr::null_mut(),
                        0,
                        buf.as_mut_ptr() as *mut c_void,
                        buf.len() as u32,
                        &mut returned,
                        std::ptr::null_mut(),
                    )
                };
                if ok != 0 && returned >= 4 {
                    tag = u32::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]);
                }
                if tag == IO_REPARSE_TAG_MOUNT_POINT {
                    info.is_mount = true;
                } else {
                    info.is_symlink = true;
                    let mut pbuf = vec![0u16; 4096];
                    let ret = unsafe {
                        GetFinalPathNameByHandleW(
                            h,
                            pbuf.as_mut_ptr(),
                            pbuf.len() as u32,
                            VOLUME_NAME_DOS,
                        )
                    };
                    if ret > 0 && (ret as usize) < pbuf.len() {
                        info.target = Some(String::from_utf16_lossy(&pbuf[..ret as usize]));
                    }
                }
                unsafe { CloseHandle(h) };
            } else {
                info.is_symlink = true;
            }
        }
        if want_owners {
            let ((oid, oname), (gid, gname)) = fs_win::win::file_accounts(ctx, prefix);
            info.owner = if oname.is_empty() { oid } else { oname };
            info.group = if gname.is_empty() { gid } else { gname };
        }
        info
    }
}

#[cfg(not(windows))]
mod sys {
    use super::StepInfo;
    use std::path::Path;
    use winux_core::Ctx;

    pub fn probe_step(_ctx: &Ctx, prefix: &str, name: &str, want_owners: bool) -> StepInfo {
        let mut info = StepInfo {
            name: name.to_owned(),
            exists: false,
            is_dir: false,
            readonly: false,
            is_symlink: false,
            is_mount: false,
            target: None,
            owner: String::new(),
            group: String::new(),
        };
        let p = Path::new(prefix);
        let meta = match std::fs::symlink_metadata(p) {
            Ok(m) => m,
            Err(_) => return info,
        };
        info.exists = true;
        info.is_dir = meta.is_dir();
        info.readonly = meta.permissions().readonly();
        info.is_symlink = meta.file_type().is_symlink();
        if info.is_symlink {
            if let Ok(t) = std::fs::read_link(p) {
                info.target = Some(t.to_string_lossy().into_owned());
            }
        }
        if want_owners {
            info.owner = "root".to_owned();
            info.group = "root".to_owned();
        }
        info
    }
}

fn run(cfg: &Config, io: &mut Io, ctx: &Ctx) -> u32 {
    let mut status = 0u32;
    for raw_path in &cfg.paths {
        let _ = writeln!(io.stdout);
        let _ = writeln!(io.stdout, "{raw_path}");

        let resolved = fsutil::resolve(ctx, Path::new(raw_path));
        let normalized = resolved.to_string_lossy();
        let steps = native_walk_steps(&normalized);

        let mut infos = Vec::with_capacity(steps.len());
        for step in &steps {
            let mut info = sys::probe_step(ctx, &step.prefix, &step.name, cfg.owners);
            if step_is_root(step) {
                info.is_mount = true;
            }
            let exists = info.exists;
            infos.push(info);
            if !exists {
                break;
            }
        }

        let mut owner_width = 0usize;
        let mut group_width = 0usize;
        if cfg.owners {
            for info in &infos {
                if !info.exists {
                    continue;
                }
                owner_width = owner_width.max(info.owner.len());
                group_width = group_width.max(info.group.len());
            }
        }

        for (i, info) in infos.iter().enumerate() {
            let indent = " ".repeat(3 + i.saturating_sub(1));
            if !info.exists {
                status = 1;
                if cfg.vertical {
                    let _ = write!(io.stdout, "f{indent} {}", info.name);
                } else {
                    let _ = write!(io.stdout, "{indent}f {}", info.name);
                }
                let _ = writeln!(io.stdout, " (No such file or directory)");
                break;
            }

            let mut glyph = if info.is_symlink {
                's'
            } else if info.is_dir {
                'd'
            } else {
                'f'
            };
            if cfg.mountpoints && info.is_mount {
                glyph = 'D';
            }

            let mut block = if cfg.modes {
                let mut md = mode_string(info);
                if cfg.mountpoints && info.is_mount && !md.is_empty() {
                    md.replace_range(0..1, "D");
                }
                md
            } else {
                glyph.to_string()
            };
            if cfg.owners {
                let o = format!("{:<width$}", info.owner, width = owner_width);
                let g = format!("{:<width$}", info.group, width = group_width);
                block.push(' ');
                block.push_str(&o);
                block.push(' ');
                block.push_str(&g);
            }
            if cfg.context {
                block.push_str(" ?");
            }

            if cfg.vertical {
                let _ = write!(io.stdout, "{block}{indent}");
            } else {
                let _ = write!(io.stdout, "{indent}{block}");
            }
            let _ = write!(io.stdout, " {}", info.name);
            if info.is_symlink {
                if let Some(target) = &info.target {
                    let _ = writeln!(io.stdout, " -> {target}");
                    continue;
                }
            }
            let _ = writeln!(io.stdout);
        }
    }
    status
}

pub fn namei_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("namei", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(rc) => return Ok(rc),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        if which == argopt::HELP {
            writeln!(
                io.stdout,
                "Usage: namei [OPTION]... PATH...\nFollow a pathname until a terminal point is found."
            )?;
        } else {
            writeln!(io.stdout, "namei (WinuxCmd) 0.2.0")?;
        }
        return Ok(0);
    }

    let long = p.has("long");
    let cfg = Config {
        modes: long || p.has("modes"),
        owners: long || p.has("owners"),
        vertical: long || p.has("vertical"),
        _nosymlinks: p.has("nosymlinks"),
        mountpoints: p.has("mountpoints"),
        context: p.has("context"),
        paths: p
            .positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect(),
    };

    if cfg.paths.is_empty() {
        writeln!(io.stderr, "namei: missing operand")?;
        writeln!(io.stderr, "Try 'namei --help' for more information.")?;
        return Ok(1);
    }

    Ok(run(&cfg, io, ctx))
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn missing_operand_fails() {
        let sc = ScratchCtx::new("namei-missing");
        let mut io = MemIo::new("");
        let rc = namei_cmd(&[], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("missing operand"));
    }

    #[test]
    fn walks_existing_and_missing_components() {
        let sc = ScratchCtx::new("namei-walk");
        std::fs::create_dir_all(sc.path("sub")).unwrap();
        std::fs::write(sc.path("sub/file.txt"), b"x").unwrap();

        let mut io = MemIo::new("");
        let rc = namei_cmd(&[OsString::from("sub/file.txt")], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 0);
        let out = io.out();
        assert!(out.contains("d sub"));
        assert!(out.contains("f file.txt"));

        let mut io2 = MemIo::new("");
        let rc2 = namei_cmd(
            &[OsString::from("sub/nope.txt")],
            &mut io2.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc2, 1);
        assert!(io2.out().contains("(No such file or directory)"));
    }

    #[test]
    fn native_walk_steps_handles_drive_and_relative() {
        let steps = native_walk_steps(r"e:\a\b");
        assert_eq!(steps.len(), 3);
        assert_eq!(steps[0].name, r"E:\");
        assert_eq!(steps[1].name, "a");
        assert_eq!(steps[2].name, "b");
    }
}
