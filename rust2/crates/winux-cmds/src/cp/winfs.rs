//! Windows filesystem layer for `cp` — hand-written kernel32/advapi32 FFI in
//! the `fs_win.rs` / `find/winfs.rs` family (zero crates).
//!
//! The value model follows the find review's winfs precedent:
//! * identity is `(volume serial, file index)` from one handle
//!   (`GetFileInformationByHandle`) — GNU's `(st_dev, st_ino)`, used for
//!   same-file detection, cyclic-link detection and `--preserve=links`;
//! * a POSIX symbolic link is any name-surrogate reparse point
//!   (`IO_REPARSE_TAG_SYMLINK`, `IO_REPARSE_TAG_MOUNT_POINT` = junction,
//!   and the WSL/LX tag shape): junctions count as links exactly like the
//!   C++ `cp.cpp:link_reparse_tag` (#1064), so `-d`/`-a`/plain `-r` recreate
//!   them instead of descending;
//! * reparse targets are read from the raw buffer (`FSCTL_GET_REPARSE_POINT`)
//!   because `std::fs::read_link` does not decode a junction; a junction is
//!   the unprivileged directory-link analog of `symlink(2)` on this
//!   platform, so `cp -d` recreates one with `FSCTL_SET_REPARSE_POINT`
//!   instead of `CreateSymbolicLinkW` (which needs Developer Mode and is
//!   reported as `Operation not permitted`, the documented platform bound);
//! * the mode value model (`0644` files, `0755` directories, `0777` links,
//!   `.exe/.bat/.cmd/.com` executable, read-only attribute clears write
//!   bits) is the shared `modemap.rs` one `stat`/`ls` use;
//! * times are raw FILETIME ticks (100 ns since 1601) end to end, so
//!   `--preserve=timestamps` copies exactly what the source handle reports;
//! * device names are the shared `winux_core::path` DOS table; `NUL` (and
//!   `/dev/null`) are opened as streams, `cp a NUL` is a silent success
//!   like GNU;
//! * enumeration is the volume's native order (NTFS: case-insensitive
//!   B-tree order), not GNU readdir order — verbose recursive gate rows
//!   keep one child per directory so byte comparisons stay stable, the
//!   same ruling `find` recorded.

use std::fs::File;

pub type DWORD = u32;

pub const FILE_ATTRIBUTE_READONLY: DWORD = 0x1;
pub const FILE_ATTRIBUTE_HIDDEN: DWORD = 0x2;
pub const FILE_ATTRIBUTE_SYSTEM: DWORD = 0x4;
pub const FILE_ATTRIBUTE_DIRECTORY: DWORD = 0x10;
pub const FILE_ATTRIBUTE_ARCHIVE: DWORD = 0x20;
pub const FILE_ATTRIBUTE_NORMAL: DWORD = 0x80;
pub const FILE_ATTRIBUTE_REPARSE_POINT: DWORD = 0x400;
pub const FILE_ATTRIBUTE_SPARSE_FILE: DWORD = 0x200;
pub const FILE_ATTRIBUTE_DEVICE: DWORD = 0x40;

pub const IO_REPARSE_TAG_MOUNT_POINT: DWORD = 0xA000_0003;
pub const IO_REPARSE_TAG_SYMLINK: DWORD = 0xA000_000C;
/// WSL DrvFs POSIX symlink (ntifs.h `IO_REPARSE_TAG_LX_SYMLINK`). Windows
/// has no reader for the tag, so every dereference resolves the stored POSIX
/// target in user space (#1647/#1656, the C++ #1732 rule).
pub const IO_REPARSE_TAG_LX_SYMLINK: DWORD = 0xA000_001D;

/// One metadata snapshot of a path.
#[derive(Clone, Debug, Default)]
pub struct Meta {
    pub attrs: DWORD,
    pub tag: DWORD,
    pub size: u64,
    pub links: u32,
    pub dev: u32,
    pub ino: u64,
    /// FILETIME ticks (100 ns since 1601-01-01).
    pub btime: i64,
    pub atime: i64,
    pub mtime: i64,
    pub ctime: i64,
}

impl Meta {
    pub fn is_dir(&self) -> bool {
        self.attrs & FILE_ATTRIBUTE_DIRECTORY != 0
    }
    pub fn is_link(&self) -> bool {
        self.attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0 && is_link_tag(self.tag)
    }
    pub fn is_read_only(&self) -> bool {
        self.attrs & FILE_ATTRIBUTE_READONLY != 0
    }
    pub fn id(&self) -> (u32, u64) {
        (self.dev, self.ino)
    }
}

/// A name-surrogate reparse point is the POSIX symlink analog (junction and
/// WSL/LX symlinks included). App-exec aliases / cloud placeholders are not.
pub fn is_link_tag(tag: DWORD) -> bool {
    tag == IO_REPARSE_TAG_SYMLINK
        || tag == IO_REPARSE_TAG_MOUNT_POINT
        || (tag & 0x2000_0000 != 0 && tag & 0x8000_0000 != 0)
}

#[cfg(windows)]
mod imp {
    use super::*;
    use std::ffi::{c_void, OsStr};
    use std::os::windows::ffi::OsStrExt;
    use std::os::windows::io::FromRawHandle;

    pub type Handle = *mut c_void;
    const INVALID_HANDLE_VALUE: Handle = -1isize as Handle;

    const FILE_READ_ATTRIBUTES: DWORD = 0x80;
    const FILE_WRITE_ATTRIBUTES: DWORD = 0x100;
    const GENERIC_READ: DWORD = 0x8000_0000;
    const GENERIC_WRITE: DWORD = 0x4000_0000;
    const FILE_SHARE_ALL: DWORD = 0x7;
    const OPEN_EXISTING: DWORD = 3;
    const OPEN_ALWAYS: DWORD = 4;
    const FILE_FLAG_BACKUP_SEMANTICS: DWORD = 0x0200_0000;
    const FILE_FLAG_OPEN_REPARSE_POINT: DWORD = 0x0020_0000;
    const INVALID_FILE_ATTRIBUTES: DWORD = 0xFFFF_FFFF;
    const ERROR_FILE_NOT_FOUND: DWORD = 2;
    const ERROR_PATH_NOT_FOUND: DWORD = 3;
    const ERROR_ACCESS_DENIED: DWORD = 5;
    const ERROR_ALREADY_EXISTS: DWORD = 183;
    const ERROR_NO_MORE_FILES: DWORD = 18;
    const ERROR_DIRECTORY: DWORD = 267;
    const ERROR_PRIVILEGE_NOT_HELD: DWORD = 1314;
    const FILE_BASIC_INFO_CLASS: i32 = 0;
    const FILE_ATTRIBUTE_TAG_INFO_CLASS: i32 = 9;
    const FSCTL_GET_REPARSE_POINT: DWORD = 0x0009_00A8;
    const FSCTL_SET_REPARSE_POINT: DWORD = 0x0009_00A4;
    const MAXIMUM_REPARSE_DATA_BUFFER_SIZE: usize = 16 * 1024;
    const FIND_EX_INFO_BASIC: i32 = 1;
    const FIND_EX_SEARCH_NAME_MATCH: i32 = 0;
    const FIND_FIRST_EX_LARGE_FETCH: DWORD = 2;

    const SYMBOLIC_LINK_FLAG_DIRECTORY: DWORD = 0x1;
    const SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE: DWORD = 0x2;
    const MOVEFILE_REPLACE_EXISTING: DWORD = 0x1;

    #[repr(C)]
    #[derive(Clone, Copy, Default)]
    struct FileTime {
        low: DWORD,
        high: DWORD,
    }

    impl FileTime {
        fn ticks(self) -> i64 {
            ((self.high as u64) << 32 | self.low as u64) as i64
        }
        fn from_ticks(t: i64) -> FileTime {
            FileTime {
                low: t as u32,
                high: (t as u64 >> 32) as u32,
            }
        }
    }

    #[repr(C)]
    struct Win32FindDataW {
        attrs: DWORD,
        creation: FileTime,
        access: FileTime,
        write: FileTime,
        size_high: DWORD,
        size_low: DWORD,
        reserved0: DWORD,
        reserved1: DWORD,
        name: [u16; 260],
        alt: [u16; 14],
    }

    fn empty_find_data() -> Win32FindDataW {
        Win32FindDataW {
            attrs: 0,
            creation: FileTime::default(),
            access: FileTime::default(),
            write: FileTime::default(),
            size_high: 0,
            size_low: 0,
            reserved0: 0,
            reserved1: 0,
            name: [0; 260],
            alt: [0; 14],
        }
    }

    #[repr(C)]
    #[derive(Default)]
    struct ByHandleFileInformation {
        attrs: DWORD,
        creation: FileTime,
        access: FileTime,
        write: FileTime,
        volume_serial: DWORD,
        size_high: DWORD,
        size_low: DWORD,
        links: DWORD,
        index_high: DWORD,
        index_low: DWORD,
    }

    #[repr(C)]
    #[derive(Default)]
    struct FileBasicInfo {
        creation: i64,
        access: i64,
        write: i64,
        change: i64,
        attrs: DWORD,
    }

    #[repr(C)]
    #[derive(Default)]
    struct FileAttributeTagInfo {
        attrs: DWORD,
        tag: DWORD,
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn CreateFileW(
            name: *const u16,
            access: DWORD,
            share: DWORD,
            security: *mut c_void,
            disposition: DWORD,
            flags: DWORD,
            template: Handle,
        ) -> Handle;
        fn CloseHandle(h: Handle) -> i32;
        fn ReadFile(
            h: Handle,
            buf: *mut c_void,
            to_read: DWORD,
            read: *mut DWORD,
            overlapped: Handle,
        ) -> i32;
        fn WriteFile(
            h: Handle,
            buf: *const c_void,
            to_write: DWORD,
            written: *mut DWORD,
            overlapped: Handle,
        ) -> i32;
        fn SetEndOfFile(h: Handle) -> i32;
        fn SetFilePointerEx(h: Handle, distance: i64, new_pos: *mut i64, method: DWORD) -> i32;
        fn GetFileInformationByHandle(h: Handle, info: *mut ByHandleFileInformation) -> i32;
        fn GetFileInformationByHandleEx(
            h: Handle,
            class: i32,
            info: *mut c_void,
            size: DWORD,
        ) -> i32;
        fn GetFileAttributesW(name: *const u16) -> DWORD;
        fn SetFileAttributesW(name: *const u16, attrs: DWORD) -> i32;
        fn SetFileTime(
            h: Handle,
            creation: *const FileTime,
            access: *const FileTime,
            write: *const FileTime,
        ) -> i32;
        fn FindFirstFileExW(
            name: *const u16,
            level: i32,
            data: *mut Win32FindDataW,
            search: i32,
            filter: *mut c_void,
            flags: DWORD,
        ) -> Handle;
        fn FindNextFileW(h: Handle, data: *mut Win32FindDataW) -> i32;
        fn FindClose(h: Handle) -> i32;
        fn CreateDirectoryW(name: *const u16, security: *mut c_void) -> i32;
        fn RemoveDirectoryW(name: *const u16) -> i32;
        fn DeleteFileW(name: *const u16) -> i32;
        fn MoveFileExW(from: *const u16, to: *const u16, flags: DWORD) -> i32;
        fn CreateHardLinkW(link: *const u16, existing: *const u16, security: *mut c_void) -> i32;
        fn CreateSymbolicLinkW(link: *const u16, target: *const u16, flags: DWORD) -> u8;
        fn DeviceIoControl(
            h: Handle,
            code: DWORD,
            in_buf: *const c_void,
            in_size: DWORD,
            out_buf: *mut c_void,
            out_size: DWORD,
            returned: *mut DWORD,
            overlapped: Handle,
        ) -> i32;
        fn GetLastError() -> DWORD;
    }

    pub fn wide(s: &str) -> Vec<u16> {
        let mut v: Vec<u16> = OsStr::new(s).encode_wide().collect();
        v.push(0);
        v
    }

    fn last_error() -> DWORD {
        unsafe { GetLastError() }
    }

    fn open_handle(path: &str, access: DWORD, flags: DWORD) -> Result<Handle, DWORD> {
        let w = wide(path);
        let h = unsafe {
            CreateFileW(
                w.as_ptr(),
                access,
                FILE_SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                flags,
                std::ptr::null_mut(),
            )
        };
        if h == INVALID_HANDLE_VALUE || h.is_null() {
            Err(last_error())
        } else {
            Ok(h)
        }
    }

    /// `GetFileAttributesW`: the link's own attributes (no follow), and the
    /// existence probe for dangling links.
    pub fn attributes(path: &str) -> Option<DWORD> {
        if winux_core::path::resolve_pseudo_device(path).is_some() {
            return Some(FILE_ATTRIBUTE_DEVICE);
        }
        let w = wide(path);
        let a = unsafe { GetFileAttributesW(w.as_ptr()) };
        if a == INVALID_FILE_ATTRIBUTES {
            None
        } else {
            Some(a)
        }
    }

    /// One path's own record (no follow): tag/size work for dangling links.
    pub fn find_record(path: &str) -> Result<(DWORD, DWORD, u64), DWORD> {
        let w = wide(path);
        let mut data = empty_find_data();
        let h = unsafe {
            FindFirstFileExW(
                w.as_ptr(),
                FIND_EX_INFO_BASIC,
                &mut data,
                FIND_EX_SEARCH_NAME_MATCH,
                std::ptr::null_mut(),
                0,
            )
        };
        if h == INVALID_HANDLE_VALUE {
            return Err(last_error());
        }
        unsafe { FindClose(h) };
        let size = (data.size_high as u64) << 32 | data.size_low as u64;
        let tag = if data.attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
            data.reserved0
        } else {
            0
        };
        Ok((data.attrs, tag, size))
    }

    /// Snapshot `path`; `follow` resolves links (stat), else the link itself
    /// (lstat). The handle open is the authority for identity/times; the
    /// enumeration record supplies the reparse tag.
    pub fn meta(path: &str, follow: bool) -> Result<Meta, DWORD> {
        // Pseudo-devices (/dev/null -> NUL, /dev/std*) are character
        // devices with no filesystem record; GNU stat(2) reports a
        // character special file and cp reads/writes through the device.
        if winux_core::path::resolve_pseudo_device(path).is_some() {
            let mut m = Meta::default();
            m.attrs = FILE_ATTRIBUTE_DEVICE;
            m.ino = 1;
            return Ok(m);
        }
        let mut flags = FILE_FLAG_BACKUP_SEMANTICS;
        if !follow {
            flags |= FILE_FLAG_OPEN_REPARSE_POINT;
        }
        let h = open_handle(path, FILE_READ_ATTRIBUTES, flags)?;
        let mut out = Meta::default();
        unsafe {
            let mut info = ByHandleFileInformation::default();
            if GetFileInformationByHandle(h, &mut info) == 0 {
                let e = last_error();
                CloseHandle(h);
                return Err(e);
            }
            out.attrs = info.attrs;
            out.size = (info.size_high as u64) << 32 | info.size_low as u64;
            out.links = info.links.max(1);
            out.dev = info.volume_serial;
            out.ino = (info.index_high as u64) << 32 | info.index_low as u64;
            out.btime = info.creation.ticks();
            out.atime = info.access.ticks();
            out.mtime = info.write.ticks();
            out.ctime = out.mtime;
            let mut basic = FileBasicInfo::default();
            if GetFileInformationByHandleEx(
                h,
                FILE_BASIC_INFO_CLASS,
                &mut basic as *mut _ as *mut c_void,
                std::mem::size_of::<FileBasicInfo>() as DWORD,
            ) != 0
            {
                out.ctime = basic.change;
            }
            if out.attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
                let mut tag = FileAttributeTagInfo::default();
                if GetFileInformationByHandleEx(
                    h,
                    FILE_ATTRIBUTE_TAG_INFO_CLASS,
                    &mut tag as *mut _ as *mut c_void,
                    std::mem::size_of::<FileAttributeTagInfo>() as DWORD,
                ) != 0
                {
                    out.tag = tag.tag;
                }
                if out.tag == 0 {
                    out.tag = find_record(path).map(|(_, t, _)| t).unwrap_or(0);
                }
            }
            CloseHandle(h);
        }
        Ok(out)
    }

    /// The link's stored target text and whether it is a directory link
    /// (junction, or a symlink with the directory flag).
    pub fn link_target(path: &str) -> Result<(String, bool), DWORD> {
        let h = open_handle(
            path,
            FILE_READ_ATTRIBUTES,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        )?;
        let mut buf = vec![0u8; MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
        let mut returned: DWORD = 0;
        let ok = unsafe {
            DeviceIoControl(
                h,
                FSCTL_GET_REPARSE_POINT,
                std::ptr::null(),
                0,
                buf.as_mut_ptr() as *mut c_void,
                buf.len() as DWORD,
                &mut returned,
                std::ptr::null_mut(),
            )
        };
        unsafe { CloseHandle(h) };
        if ok == 0 {
            return Err(last_error());
        }
        if buf.len() < 8 {
            return Err(ERROR_DIRECTORY);
        }
        let tag = DWORD::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]);
        // Both unions start at offset 8 with the two offset/length pairs.
        let u16at = |off: usize| -> u16 {
            if off + 2 > buf.len() {
                0
            } else {
                u16::from_le_bytes([buf[off], buf[off + 1]])
            }
        };
        // IO_REPARSE_TAG_LX_SYMLINK: u32 version then the UTF-8 POSIX
        // target (no terminator, no UTF-16 substitution/print pair).  The
        // directory bit of the link's own attributes is the only type
        // information the tag carries (#1732).
        if tag == IO_REPARSE_TAG_LX_SYMLINK {
            let data_len = u16at(4) as usize;
            if data_len < 4 || 8 + data_len > buf.len() {
                return Err(ERROR_DIRECTORY);
            }
            let raw = &buf[12..8 + data_len];
            let target = String::from_utf8_lossy(raw);
            let target = target.trim_end_matches('\0');
            if target.is_empty() {
                return Err(ERROR_DIRECTORY);
            }
            let is_dir = attributes(path)
                .map(|a| a & FILE_ATTRIBUTE_DIRECTORY != 0)
                .unwrap_or(false);
            return Ok((target.to_string(), is_dir));
        }
        let substitute_off = u16at(8) as usize;
        let substitute_len = u16at(10) as usize;
        let print_off = u16at(12) as usize;
        let print_len = u16at(14) as usize;
        // PathBuffer offset in the reparse buffer.  MountPointReparseBuffer
        // has the four offset/length u16s then PathBuffer at 16, but
        // SymbolicLinkReparseBuffer inserts a 4-byte Flags field first, so
        // its PathBuffer starts at 20 (#1761: reading at 16 for a SYMLINK tag
        // consumed Flags as two UTF-16 units and shifted the whole target).
        let path_start = if tag == IO_REPARSE_TAG_SYMLINK {
            20usize
        } else {
            16usize
        };
        let read_name = |off: usize, len: usize| -> String {
            let start = path_start + off;
            let end = start + len;
            if len == 0 || end > buf.len() {
                return String::new();
            }
            let units: Vec<u16> = buf[start..end]
                .chunks_exact(2)
                .map(|c| u16::from_le_bytes([c[0], c[1]]))
                .collect();
            String::from_utf16_lossy(&units)
        };
        let mut target = read_name(print_off, print_len);
        if target.is_empty() {
            target = read_name(substitute_off, substitute_len);
        }
        for p in [r"\??\", r"\\?\"] {
            if let Some(rest) = target.strip_prefix(p) {
                target = rest.to_string();
                break;
            }
        }
        let is_dir = tag == IO_REPARSE_TAG_MOUNT_POINT
            || (tag == IO_REPARSE_TAG_SYMLINK
                && buf.len() >= 20
                && DWORD::from_le_bytes([buf[16], buf[17], buf[18], buf[19]]) & 1 != 0);
        Ok((target, is_dir))
    }

    /// The stored POSIX target of a WSL DrvFs LX symlink, or `None` for any
    /// other reparse point / ordinary path.  This is the reader every
    /// dereference needs before it can resolve the chain itself (#1647).
    pub fn lx_target(path: &str) -> Option<String> {
        let attrs = attributes(path)?;
        if attrs & FILE_ATTRIBUTE_REPARSE_POINT == 0 {
            return None;
        }
        let h = open_handle(
            path,
            0,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        )
        .ok()?;
        let mut buf = vec![0u8; MAXIMUM_REPARSE_DATA_BUFFER_SIZE];
        let mut returned: DWORD = 0;
        let ok = unsafe {
            DeviceIoControl(
                h,
                FSCTL_GET_REPARSE_POINT,
                std::ptr::null(),
                0,
                buf.as_mut_ptr() as *mut c_void,
                buf.len() as DWORD,
                &mut returned,
                std::ptr::null_mut(),
            )
        };
        unsafe { CloseHandle(h) };
        if ok == 0 {
            return None;
        }
        let len = (returned as usize).min(buf.len());
        if len < 12 {
            return None;
        }
        let tag = DWORD::from_le_bytes([buf[0], buf[1], buf[2], buf[3]]);
        if tag != IO_REPARSE_TAG_LX_SYMLINK {
            return None;
        }
        let data_len = u16::from_le_bytes([buf[4], buf[5]]) as usize;
        if data_len < 4 || 8 + data_len > len {
            return None;
        }
        let raw = &buf[12..8 + data_len];
        let target = String::from_utf8_lossy(raw);
        let target = target.trim_end_matches('\0');
        if target.is_empty() {
            None
        } else {
            Some(target.to_string())
        }
    }

    pub fn create_symlink(link: &str, target: &str, dir: bool) -> Result<(), DWORD> {
        let t = wide(&target.replace('/', "\\"));
        let l = wide(link);
        let mut flags = if dir { SYMBOLIC_LINK_FLAG_DIRECTORY } else { 0 };
        let mut rc = unsafe { CreateSymbolicLinkW(l.as_ptr(), t.as_ptr(), flags) };
        if rc == 0 {
            // Older systems reject the flag bit on the first call
            // (fs_win::create_symlink retry shape).
            flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
            rc = unsafe { CreateSymbolicLinkW(l.as_ptr(), t.as_ptr(), flags) };
        }
        if rc == 0 {
            Err(last_error())
        } else {
            Ok(())
        }
    }

    /// Create a directory junction — the unprivileged directory-link form.
    /// The target is stored absolute in both the substitute and print names
    /// (mklink /J shape), so a copied junction keeps resolving.
    pub fn create_junction(link: &str, target: &str) -> Result<(), DWORD> {
        let lw = wide(link);
        if unsafe { CreateDirectoryW(lw.as_ptr(), std::ptr::null_mut()) } == 0 {
            let e = last_error();
            if e != ERROR_ALREADY_EXISTS {
                return Err(e);
            }
        }
        let target_norm = target.replace('/', "\\");
        let abs = if target_norm.len() >= 2 && target_norm.as_bytes()[1] == b':' {
            target_norm
        } else {
            // relative target: mklink resolves against the link's parent;
            // joina lexically to keep the stored form absolute
            let parent = match link.rfind(['\\', '/']) {
                Some(i) if i > 0 => &link[..i],
                _ => ".",
            };
            format!("{parent}\\{target_norm}")
        };
        let substitute: Vec<u16> = format!(r"\??\{abs}").encode_utf16().collect();
        let print: Vec<u16> = abs.encode_utf16().collect();
        // The on-disk shape mklink/fsutil produce: substitute, NUL, print,
        // NUL. PrintNameOffset is subLen + 2 and ReparseDataLength counts
        // the 8-byte field block plus both NUL-terminated names.
        let sub_bytes = substitute.len() * 2;
        let print_bytes = print.len() * 2;
        let data_len = 8 + sub_bytes + 2 + print_bytes + 2;
        let total = 8 + data_len;
        let mut buf = vec![0u8; total];
        buf[0..4].copy_from_slice(&IO_REPARSE_TAG_MOUNT_POINT.to_le_bytes());
        buf[4..6].copy_from_slice(&(data_len as u16).to_le_bytes());
        // reserved: 0
        buf[8..10].copy_from_slice(&0u16.to_le_bytes());
        buf[10..12].copy_from_slice(&(sub_bytes as u16).to_le_bytes());
        buf[12..14].copy_from_slice(&((sub_bytes + 2) as u16).to_le_bytes());
        buf[14..16].copy_from_slice(&(print_bytes as u16).to_le_bytes());
        let mut off = 16;
        for u in substitute
            .iter()
            .chain([&0u16].into_iter())
            .chain(print.iter())
            .chain([&0u16].into_iter())
        {
            buf[off..off + 2].copy_from_slice(&u.to_le_bytes());
            off += 2;
        }
        let h = match open_handle(
            link,
            GENERIC_WRITE,
            FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
        ) {
            Ok(h) => h,
            Err(e) => {
                unsafe { RemoveDirectoryW(lw.as_ptr()) };
                return Err(e);
            }
        };
        let mut returned: DWORD = 0;
        let ok = unsafe {
            DeviceIoControl(
                h,
                FSCTL_SET_REPARSE_POINT,
                buf.as_ptr() as *const c_void,
                total as DWORD,
                std::ptr::null_mut(),
                0,
                &mut returned,
                std::ptr::null_mut(),
            )
        };
        unsafe { CloseHandle(h) };
        if ok == 0 {
            let e = last_error();
            unsafe { RemoveDirectoryW(lw.as_ptr()) };
            return Err(e);
        }
        Ok(())
    }

    /// Delete a file or an (empty) directory; the read-only attribute is
    /// cleared and the call retried, because POSIX unlink ignores it.
    pub fn delete(path: &str, is_dir: bool) -> Result<(), DWORD> {
        let w = wide(path);
        // A directory reparse point (junction / dir symlink) is removed with
        // RemoveDirectoryW, which unlinks the reparse point itself
        // (unlink.rs rule); DeleteFileW refuses it.
        let is_dir = is_dir
            || attributes(path)
                .map(|a| a & FILE_ATTRIBUTE_DIRECTORY != 0)
                .unwrap_or(false);
        let attempt = || unsafe {
            if is_dir {
                RemoveDirectoryW(w.as_ptr()) != 0
            } else {
                DeleteFileW(w.as_ptr()) != 0
            }
        };
        if attempt() {
            return Ok(());
        }
        let e = last_error();
        if e == ERROR_ACCESS_DENIED {
            if let Some(a) = attributes(path) {
                if a & FILE_ATTRIBUTE_READONLY != 0 {
                    unsafe { SetFileAttributesW(w.as_ptr(), a & !FILE_ATTRIBUTE_READONLY) };
                    if attempt() {
                        return Ok(());
                    }
                    let e2 = last_error();
                    unsafe { SetFileAttributesW(w.as_ptr(), a) };
                    return Err(e2);
                }
            }
        }
        Err(e)
    }

    pub fn ensure_dir(path: &str) -> Result<(), DWORD> {
        let w = wide(path);
        if unsafe { CreateDirectoryW(w.as_ptr(), std::ptr::null_mut()) } != 0 {
            return Ok(());
        }
        let e = last_error();
        if e == ERROR_ALREADY_EXISTS {
            return Ok(());
        }
        Err(e)
    }

    pub fn rename(from: &str, to: &str) -> Result<(), DWORD> {
        let f = wide(from);
        let t = wide(to);
        if unsafe { MoveFileExW(f.as_ptr(), t.as_ptr(), MOVEFILE_REPLACE_EXISTING) } == 0 {
            Err(last_error())
        } else {
            Ok(())
        }
    }

    /// Set/clear the read-only bit, preserving the other attributes.
    pub fn set_read_only(path: &str, read_only: bool) -> Result<(), DWORD> {
        let a = attributes(path).ok_or_else(last_error)?;
        let target = if read_only {
            a | FILE_ATTRIBUTE_READONLY
        } else {
            a & !FILE_ATTRIBUTE_READONLY
        };
        let w = wide(path);
        if unsafe { SetFileAttributesW(w.as_ptr(), target) } == 0 {
            Err(last_error())
        } else {
            Ok(())
        }
    }

    pub fn set_times(path: &str, btime: i64, atime: i64, mtime: i64) -> Result<(), DWORD> {
        let dir = attributes(path).map(|a| a & FILE_ATTRIBUTE_DIRECTORY != 0);
        let flags = if dir == Some(true) {
            FILE_FLAG_BACKUP_SEMANTICS
        } else {
            0
        };
        let h = open_handle(path, FILE_WRITE_ATTRIBUTES, flags)?;
        let c = FileTime::from_ticks(btime);
        let a = FileTime::from_ticks(atime);
        let m = FileTime::from_ticks(mtime);
        let ok = unsafe { SetFileTime(h, &c, &a, &m) };
        unsafe { CloseHandle(h) };
        if ok == 0 {
            Err(last_error())
        } else {
            Ok(())
        }
    }

    pub fn hard_link(existing: &str, link: &str) -> Result<(), DWORD> {
        let e = wide(existing);
        let l = wide(link);
        if unsafe { CreateHardLinkW(l.as_ptr(), e.as_ptr(), std::ptr::null_mut()) } == 0 {
            Err(last_error())
        } else {
            Ok(())
        }
    }

    /// Enumerate one directory in the volume's native order.
    pub fn read_dir(dir: &str) -> Result<Vec<(String, DWORD, DWORD, u64)>, DWORD> {
        let mut pat = dir.to_string();
        if !pat.ends_with('\\') && !pat.ends_with('/') {
            pat.push('\\');
        }
        pat.push('*');
        let pw = wide(&pat);
        let mut data = empty_find_data();
        let h = unsafe {
            FindFirstFileExW(
                pw.as_ptr(),
                FIND_EX_INFO_BASIC,
                &mut data,
                FIND_EX_SEARCH_NAME_MATCH,
                std::ptr::null_mut(),
                FIND_FIRST_EX_LARGE_FETCH,
            )
        };
        if h == INVALID_HANDLE_VALUE {
            let e = last_error();
            if e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND {
                return Ok(Vec::new());
            }
            return Err(e);
        }
        let mut out = Vec::new();
        loop {
            let len = data.name.iter().position(|&c| c == 0).unwrap_or(0);
            let name = String::from_utf16_lossy(&data.name[..len]);
            let size = (data.size_high as u64) << 32 | data.size_low as u64;
            let tag = if data.attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
                data.reserved0
            } else {
                0
            };
            if name != "." && name != ".." {
                out.push((name, data.attrs, tag, size));
            }
            data = empty_find_data();
            if unsafe { FindNextFileW(h, &mut data) } == 0 {
                let e = last_error();
                unsafe { FindClose(h) };
                if e == ERROR_NO_MORE_FILES {
                    return Ok(out);
                }
                return Err(e);
            }
        }
    }

    pub fn open_read(path: &str) -> Result<File, DWORD> {
        let h = open_handle(path, GENERIC_READ, 0)?;
        Ok(unsafe { File::from_raw_handle(h as _) })
    }

    /// Open (or create) the destination for a byte copy; an existing file is
    /// truncated in place so its attributes survive, matching `open(O_TRUNC)`.
    pub fn open_write(path: &str) -> Result<File, DWORD> {
        let w = wide(path);
        // A pseudo-device (NUL, CONOUT$) exists already and refuses
        // OPEN_ALWAYS; GNU's open(O_WRONLY) on the device is OPEN_EXISTING.
        let disposition = if winux_core::path::resolve_pseudo_device(path).is_some() {
            OPEN_EXISTING
        } else {
            OPEN_ALWAYS
        };
        let h = unsafe {
            CreateFileW(
                w.as_ptr(),
                GENERIC_WRITE,
                FILE_SHARE_ALL,
                std::ptr::null_mut(),
                disposition,
                FILE_ATTRIBUTE_NORMAL,
                std::ptr::null_mut(),
            )
        };
        if h == INVALID_HANDLE_VALUE || h.is_null() {
            return Err(last_error());
        }
        // A device has no file pointer/EOF to truncate; SetFilePointerEx
        // fails with ERROR_INVALID_FUNCTION there (GNU writes without a
        // ftruncate on character devices).
        if disposition != OPEN_EXISTING
            && (unsafe { SetFilePointerEx(h, 0, std::ptr::null_mut(), 0) } == 0
                || unsafe { SetEndOfFile(h) } == 0)
        {
            let e = last_error();
            unsafe { CloseHandle(h) };
            return Err(e);
        }
        Ok(unsafe { File::from_raw_handle(h as _) })
    }

    /// Buffered read through the raw handle (keeps the std File wrapper but
    /// reports the Win32 error, whose GNU spelling the caller prints).
    pub fn read_chunk(f: &mut File, buf: &mut [u8]) -> Result<usize, DWORD> {
        use std::os::windows::io::AsRawHandle;
        let mut read: DWORD = 0;
        let ok = unsafe {
            ReadFile(
                f.as_raw_handle() as Handle,
                buf.as_mut_ptr() as *mut c_void,
                buf.len() as DWORD,
                &mut read,
                std::ptr::null_mut(),
            )
        };
        if ok == 0 {
            let e = last_error();
            // A pipe/console source hitting EOF surfaces as these; for
            // regular files they never appear mid-stream.
            if e == 109 || e == 232 {
                return Ok(0);
            }
            return Err(e);
        }
        Ok(read as usize)
    }

    pub fn write_all(f: &mut File, buf: &[u8]) -> Result<(), DWORD> {
        use std::os::windows::io::AsRawHandle;
        let mut off = 0usize;
        while off < buf.len() {
            let mut written: DWORD = 0;
            let ok = unsafe {
                WriteFile(
                    f.as_raw_handle() as Handle,
                    buf[off..].as_ptr() as *const c_void,
                    (buf.len() - off).min(u32::MAX as usize) as DWORD,
                    &mut written,
                    std::ptr::null_mut(),
                )
            };
            if ok == 0 {
                return Err(last_error());
            }
            if written == 0 {
                return Err(ERROR_ACCESS_DENIED);
            }
            off += written as usize;
        }
        Ok(())
    }

    /// GNU strerror spelling of a Win32 error (C locale), the `copy.c`
    /// diagnostics' tail. `cp` uses the ENOENT/EACCES/EEXIST/ENOTDIR/
    /// ENAMETOOLONG/ELOOP set the C++ applet's win32_posix_error_text maps.
    pub fn reason(err: DWORD) -> &'static str {
        match err {
            2 | 3 | 1920 | 2062 | 161 | 123 => "No such file or directory",
            5 | 32 | 33 => "Permission denied",
            80 | 183 => "File exists",
            145 => "Directory not empty",
            267 => "Not a directory",
            206 => "File name too long",
            1921 => "Too many levels of symbolic links",
            109 | 232 => "Broken pipe",
            13 => "Invalid argument",
            1 | ERROR_PRIVILEGE_NOT_HELD => "Operation not permitted",
            112 => "No space left on device",
            _ => "Invalid argument",
        }
    }
}

#[cfg(not(windows))]
mod imp {
    use super::*;
    use std::fs::File;

    pub fn wide(_s: &str) -> Vec<u16> {
        Vec::new()
    }
    pub fn attributes(_path: &str) -> Option<DWORD> {
        None
    }
    pub fn find_record(_path: &str) -> Result<(DWORD, DWORD, u64), DWORD> {
        Err(2)
    }
    pub fn meta(_path: &str, _follow: bool) -> Result<Meta, DWORD> {
        Err(2)
    }
    pub fn link_target(_path: &str) -> Result<(String, bool), DWORD> {
        Err(2)
    }
    pub fn create_symlink(_link: &str, _target: &str, _dir: bool) -> Result<(), DWORD> {
        Err(1)
    }
    pub fn create_junction(_link: &str, _target: &str) -> Result<(), DWORD> {
        Err(1)
    }
    pub fn delete(_path: &str, _is_dir: bool) -> Result<(), DWORD> {
        Err(2)
    }
    pub fn ensure_dir(_path: &str) -> Result<(), DWORD> {
        Err(5)
    }
    pub fn rename(_from: &str, _to: &str) -> Result<(), DWORD> {
        Err(2)
    }
    pub fn set_read_only(_path: &str, _read_only: bool) -> Result<(), DWORD> {
        Err(5)
    }
    pub fn set_times(_path: &str, _b: i64, _a: i64, _m: i64) -> Result<(), DWORD> {
        Err(5)
    }
    pub fn lx_target(_path: &str) -> Option<String> {
        None
    }
    pub fn hard_link(_existing: &str, _link: &str) -> Result<(), DWORD> {
        Err(1)
    }
    pub fn read_dir(_dir: &str) -> Result<Vec<(String, DWORD, DWORD, u64)>, DWORD> {
        Err(2)
    }
    pub fn open_read(_path: &str) -> Result<File, DWORD> {
        Err(2)
    }
    pub fn open_write(_path: &str) -> Result<File, DWORD> {
        Err(5)
    }
    pub fn read_chunk(_f: &mut File, _buf: &mut [u8]) -> Result<usize, DWORD> {
        Err(2)
    }
    pub fn write_all(_f: &mut File, _buf: &[u8]) -> Result<(), DWORD> {
        Err(5)
    }
    pub fn reason(_err: DWORD) -> &'static str {
        "Invalid argument"
    }
}

pub use imp::*;

/// Win32 MAX_PATH escape: an absolute API path longer than the legacy
/// limit is handed to the OS in the `\\?\` verbatim form (and `\\?\UNC\`
/// for UNC), exactly like the 1.x `native_path::make_api_path_operand`
/// that every cp file operation used.  Device names and already-extended
/// paths pass through untouched.  Relative and short paths keep the plain
/// spelling so the OS-level trailing-separator and reparse semantics the
/// gate measures stay intact.  Without this, `cp <310-char source> dest`
/// failed with ENOENT on a host with LongPathsEnabled=0 while the 1.x
/// exe copied it (the >MAX_PATH matrix row).
pub fn extended_path(path: &str) -> String {
    if path.is_empty()
        || path.starts_with(r"\\?\")
        || winux_core::path::resolve_pseudo_device(path).is_some()
        || path.len() < 240
    {
        return path.to_string();
    }
    if !std::path::Path::new(path).is_absolute() {
        return path.to_string();
    }
    let mut text = path.replace('/', "\\");
    // `\\?\` disables the Win32 normalizer, so fold `.`/`..` lexically
    // before crossing the boundary.
    let mut parts: Vec<&str> = Vec::new();
    for comp in text.split('\\') {
        match comp {
            "" | "." => {}
            ".." => {
                parts.pop();
            }
            other => parts.push(other),
        }
    }
    text = parts.join("\\");
    if let Some(rest) = text.strip_prefix(r"\\") {
        format!(r"\\?\UNC\{rest}")
    } else {
        format!(r"\\?\{text}")
    }
}

/// Resolve every WSL/LX symlink in the path's prefix chain into the target
/// Win32 can open, keeping the caller's spelling for diagnostics.  GNU's
/// stat/open walk resolves the whole chain; Win32 leaves the LX tag
/// unresolved, so `cp -r -H link/f` (and every child of a followed LX
/// directory link) has to be expanded here before the handle open (#1732).
pub fn expand_lx(ctx: &winux_core::Ctx, path: &str) -> String {
    use std::path::{Component, Path, PathBuf};
    // A trailing separator is semantically load-bearing (stat("a/") is
    // ENOTDIR, open("nodir/") must not create a file); Path components drop
    // it, so restore it on the way out.
    let trailing = path.ends_with('/') || path.ends_with('\\');
    let mut out = PathBuf::new();
    let mut hops = 0u32;
    for comp in Path::new(path).components() {
        out.push(comp.as_os_str());
        if matches!(
            comp,
            Component::Prefix(_) | Component::RootDir | Component::CurDir
        ) {
            continue;
        }
        loop {
            if hops >= crate::fs_win::MAX_SYMLINK_FOLLOW {
                break;
            }
            let shown = out.to_string_lossy().into_owned();
            let Some(target) = lx_target(&shown) else {
                break;
            };
            hops += 1;
            let resolved = crate::fs_win::resolve_symlink_target(ctx, &shown, &target);
            out = PathBuf::from(resolved);
        }
    }
    let mut text = out.to_string_lossy().into_owned();
    if trailing && !text.ends_with(['/', '\\']) && !text.is_empty() {
        text.push(std::path::MAIN_SEPARATOR);
    }
    text
}

/// `stat(2)` link-chain resolution for the name-surrogate reparse points
/// Win32 stat misreports: a symlink whose reparse point is file-flagged but
/// whose target is a directory comes back as a non-directory entry, so
/// `-H`/`-L` read the command-line dir link as "not a dir" and the copy
/// died before creating the destination (#1693, #1761).  `expand_lx` only
/// decodes the LX tag; native symlinks and junctions resolve here in user
/// space, bounded like MAXSYMLINKS — the retired C++
/// `dereferenced_source_path` rule.
fn resolve_deref_chain(ctx: &winux_core::Ctx, path: &str) -> String {
    let mut cur = path.to_string();
    for _ in 0..crate::fs_win::MAX_SYMLINK_FOLLOW {
        let (_, tag, _) = match find_record(&cur) {
            Ok(v) => v,
            Err(_) => break,
        };
        if !is_link_tag(tag) {
            break;
        }
        let target = match link_target(&cur) {
            Ok((t, _)) if !t.is_empty() => t,
            _ => break,
        };
        cur = crate::fs_win::resolve_symlink_target(ctx, &cur, &target);
    }
    cur
}

/// The follow-side operand expansion: LX links first (`expand_lx`), then
/// the name-surrogate chain Win32 stat misreports — native symlinks and
/// junctions resolve in user space so `-H`/`-L` see the real target type
/// (#1693, #1761).  `lstat` callers keep `expand_lx_parent` and see the
/// link itself.
pub fn expand_for_follow(ctx: &winux_core::Ctx, path: &str) -> String {
    let expanded = expand_lx(ctx, path);
    resolve_deref_chain(ctx, &expanded)
}

/// `stat(2)` through the follow expansion: snapshot the resolved target.
/// `lstat` callers keep `meta(..., false)` and see the link.
pub fn meta_follow(ctx: &winux_core::Ctx, path: &str) -> Result<Meta, DWORD> {
    let resolved = expand_for_follow(ctx, path);
    meta(&resolved, true)
}

/// Expand LX links in the parent chain only, keeping the final component
/// as spelled: the lstat view of `linkdir/child` still has to traverse
/// `linkdir` (#1732).
pub fn expand_lx_parent(ctx: &winux_core::Ctx, path: &str) -> String {
    let p = std::path::Path::new(path);
    match (p.parent(), p.file_name()) {
        (Some(parent), Some(name)) if !parent.as_os_str().is_empty() => {
            let expanded = expand_lx(ctx, &parent.to_string_lossy());
            std::path::Path::new(&expanded)
                .join(name)
                .to_string_lossy()
                .into_owned()
        }
        _ => path.to_string(),
    }
}
