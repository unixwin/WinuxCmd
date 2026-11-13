//! Windows filesystem FFI shared by the wave3a commands that touch the
//! Windows value model (`stat`, `touch`, `ln`): raw kernel32/advapi32
//! declarations in the same family the 1.x C++ applets use, so the Rust
//! value model matches `src/commands/{stat,touch,ln}.cpp` on main.
//!
//! * times: FILETIME (100ns since 1601-01-01) <-> Unix epoch seconds;
//!   naive local datetimes go through TzSpecificLocalTimeToSystemTime so
//!   DST-aware conversion never touches the CRT global timezone.
//! * metadata: CreateFileW(FILE_FLAG_BACKUP_SEMANTICS [+ OPEN_REPARSE_POINT
//!   for lstat]) -> GetFileInformationByHandleEx(FileBasicInfo) for the
//!   four timestamps plus GetFileInformationByHandle for size / link count
//!   / volume serial / file index.
//! * owner/group: GetNamedSecurityInfoW -> LookupAccountSidW; the numeric
//!   id is the 1.x formula from `src/utils/win32.cppm` (0x30000+RID for
//!   domain accounts, 0x60000+RID for mandatory-label authority, else RID).
//! * links: CreateHardLinkW; CreateSymbolicLinkW with the
//!   SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE bit (fails with
//!   ERROR_PRIVILEGE_NOT_HELD -> "Operation not permitted" outside
//!   Developer Mode, the documented 1.x behavior).
//!
//! Non-Windows builds get stubs so the workspace still compiles; the gate
//! runs on Windows only.

use std::ffi::c_void;
use std::path::Path;

use winux_core::Ctx;

#[allow(clippy::upper_case_acronyms)]
pub type DWORD = u32;
#[allow(clippy::upper_case_acronyms)]
pub type WCHAR = u16;
#[allow(clippy::upper_case_acronyms)]
pub type WORD = u16;
pub type Handle = *mut c_void;

pub const INVALID_HANDLE_VALUE: Handle = -1isize as Handle;
pub const FILE_FLAG_BACKUP_SEMANTICS: DWORD = 0x0200_0000;
pub const FILE_FLAG_OPEN_REPARSE_POINT: DWORD = 0x0020_0000;
pub const FILE_READ_ATTRIBUTES: DWORD = 0x0080;
pub const ERROR_PRIVILEGE_NOT_HELD: DWORD = 1314;
pub const ERROR_CANT_ACCESS_FILE: DWORD = 1920;

pub const SYMBOLIC_LINK_FLAG_FILE: DWORD = 0;
pub const SYMBOLIC_LINK_FLAG_DIRECTORY: DWORD = 0x1;
pub const SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE: DWORD = 0x2;

pub const FILE_BASIC_INFO_CLASS: i32 = 0; // FileBasicInfo

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct FileTime {
    pub low: DWORD,
    pub high: DWORD,
}

impl FileTime {
    pub fn as_u64(&self) -> u64 {
        (self.high as u64) << 32 | self.low as u64
    }
    pub fn from_u64(v: u64) -> Self {
        FileTime {
            low: v as u32,
            high: (v >> 32) as u32,
        }
    }
    /// Unix epoch seconds (may be negative) and the 100ns remainder.
    pub fn to_epoch(&self) -> (i64, i64) {
        let ticks = self.as_u64() as i64;
        let delta = ticks - 116_444_736_000_000_000i64;
        (delta.div_euclid(10_000_000), delta.rem_euclid(10_000_000))
    }
    pub fn from_epoch(secs: i64, nanos: u32) -> Self {
        let ticks = secs * 10_000_000 + (nanos / 100) as i64 + 116_444_736_000_000_000i64;
        FileTime::from_u64(ticks as u64)
    }
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct SystemTime {
    pub year: WORD,
    pub month: WORD,
    pub day_of_week: WORD,
    pub day: WORD,
    pub hour: WORD,
    pub minute: WORD,
    pub second: WORD,
    pub milliseconds: WORD,
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct FileBasicInfo {
    pub creation_time: i64,
    pub last_access_time: i64,
    pub last_write_time: i64,
    pub change_time: i64,
    pub file_attributes: DWORD,
}

#[repr(C)]
#[derive(Default, Clone, Copy)]
pub struct ByHandleFileInformation {
    pub attributes: DWORD,
    pub creation_time: FileTime,
    pub last_access_time: FileTime,
    pub last_write_time: FileTime,
    pub volume_serial: DWORD,
    pub size_high: DWORD,
    pub size_low: DWORD,
    pub links: DWORD,
    pub index_high: DWORD,
    pub index_low: DWORD,
}

/// Metadata snapshot for one path (stat's Windows value model).
#[derive(Default, Clone)]
pub struct FileMeta {
    pub is_symlink: bool,
    pub is_dir: bool,
    pub size: u64,
    pub links: u64,
    pub volume_serial: u32,
    pub index: u64,
    pub atime: FileTime,
    pub mtime: FileTime,
    pub ctime: FileTime,
    pub btime: FileTime,
    pub read_only: bool,
    pub character_device: bool,
}

#[cfg(windows)]
pub mod imp {
    use super::*;
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;

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
        fn GetFileInformationByHandle(h: Handle, info: *mut ByHandleFileInformation) -> i32;
        fn GetFileInformationByHandleEx(
            h: Handle,
            class: i32,
            info: *mut c_void,
            size: DWORD,
        ) -> i32;
        fn GetStdHandle(which: DWORD) -> Handle;
        fn DeleteFileW(name: *const u16) -> i32;
        fn SetFileTime(
            h: Handle,
            ctime: *const FileTime,
            atime: *const FileTime,
            mtime: *const FileTime,
        ) -> i32;
        fn CreateHardLinkW(link: *const u16, existing: *const u16, security: *mut c_void) -> i32;
        // CreateSymbolicLinkW returns BOOLEAN (winnt.h: UCHAR), NOT BOOL
        // (windef.h: int, like every `-> i32` above). Declaring the 1-byte
        // return as i32 reads the full EAX whose upper bits are undefined
        // for a sub-register return on x64 — a refusal (AL = 0) could
        // masquerade as success. This declaration is the wave3a ln -s fix.
        fn CreateSymbolicLinkW(link: *const u16, target: *const u16, flags: DWORD) -> u8;
        fn SystemTimeToFileTime(st: *const SystemTime, ft: *mut FileTime) -> i32;
        fn FileTimeToSystemTime(ft: *const FileTime, st: *mut SystemTime) -> i32;
        // fifo-bridge endpoint plumbing (#1038 port; 1.x file_io.cppm
        // open_fifo_read/write_handle): the reader creates the server end
        // and blocks in ConnectNamedPipe, the writer polls CreateFileW.
        fn ReadFile(
            h: Handle,
            buf: *mut c_void,
            to_read: DWORD,
            read: *mut DWORD,
            overlapped: Handle,
        ) -> i32;
        fn CreateNamedPipeW(
            name: *const u16,
            open_mode: DWORD,
            pipe_mode: DWORD,
            max_instances: DWORD,
            out_size: DWORD,
            in_size: DWORD,
            default_timeout: DWORD,
            security: *mut c_void,
        ) -> Handle;
        fn ConnectNamedPipe(pipe: Handle, overlapped: Handle) -> i32;
        fn WaitNamedPipeW(name: *const u16, timeout: DWORD) -> i32;
        fn Sleep(ms: DWORD);
        fn SystemTimeToTzSpecificLocalTime(
            tz: *mut c_void,
            utc: *const SystemTime,
            local: *mut SystemTime,
        ) -> i32;
        fn TzSpecificLocalTimeToSystemTime(
            tz: *mut c_void,
            local: *const SystemTime,
            utc: *mut SystemTime,
        ) -> i32;
        fn GetFileAttributesW(name: *const u16) -> DWORD;
        #[allow(dead_code)]
        fn SetFileAttributesW(name: *const u16, attrs: DWORD) -> i32;
    }

    #[link(name = "advapi32")]
    extern "system" {
        fn GetNamedSecurityInfoW(
            name: *const u16,
            object_type: DWORD,
            info: DWORD,
            owner: *mut *mut c_void,
            group: *mut *mut c_void,
            dacl: *mut c_void,
            sacl: *mut c_void,
            security: *mut *mut c_void,
        ) -> DWORD;
        fn LookupAccountSidW(
            system: *const u16,
            sid: *mut c_void,
            name: *const u16,
            name_size: *mut DWORD,
            domain: *const u16,
            domain_size: *mut DWORD,
            use_: *mut DWORD,
        ) -> i32;
        fn IsValidSid(sid: *mut c_void) -> i32;
        fn GetSidSubAuthorityCount(sid: *mut c_void) -> *mut u8;
        fn GetSidSubAuthority(sid: *mut c_void, index: DWORD) -> *mut DWORD;
        fn GetSidIdentifierAuthority(sid: *mut c_void) -> *mut u8;
        fn LocalFree(h: Handle) -> Handle;
    }

    const SE_FILE_OBJECT: DWORD = 1;
    const OWNER_SECURITY_INFORMATION: DWORD = 0x1;
    const GROUP_SECURITY_INFORMATION: DWORD = 0x2;
    const GENERIC_READ: DWORD = 0x8000_0000;
    const FILE_SHARE_ALL: DWORD = 0x7;
    const OPEN_EXISTING: DWORD = 3;
    const FILE_ATTRIBUTE_READONLY: DWORD = 0x1;
    const FILE_ATTRIBUTE_DIRECTORY: DWORD = 0x10;
    const FILE_ATTRIBUTE_REPARSE_POINT: DWORD = 0x400;
    const FILE_ATTRIBUTE_DEVICE: DWORD = 0x40;
    const FILE_ATTRIBUTE_NORMAL: DWORD = 0x80;
    const FILE_ATTRIBUTE_SYSTEM: DWORD = 0x4;

    // Named-pipe open modes / error codes for the fifo bridge (1.x
    // file_io.cppm used the same literals).
    const PIPE_ACCESS_INBOUND: DWORD = 0x1;
    const PIPE_TYPE_BYTE: DWORD = 0x0;
    const PIPE_READMODE_BYTE: DWORD = 0x0;
    const PIPE_WAIT: DWORD = 0x0;
    const PIPE_UNLIMITED_INSTANCES: DWORD = 0xff;
    const NMPWAIT_WAIT_FOREVER: DWORD = 0xffff_ffff;
    const ERROR_FILE_NOT_FOUND: DWORD = 2;
    const ERROR_PIPE_BUSY: DWORD = 231;
    const ERROR_PIPE_CONNECTED: DWORD = 539;

    pub fn wide(s: &str) -> Vec<u16> {
        OsStr::new(s)
            .encode_wide()
            .chain(std::iter::once(0))
            .collect()
    }

    /// Whole-operand DOS device probe — the existence gate that replaces
    /// the CRT stat / GetFileAttributesExW path, both of which reject
    /// device names outright (1.x native_path file_attribute_data_w). The
    /// probe itself lives in winux_core::path::imp since the wave3b
    /// shell-namespace port (#1488): one table, one probe, shared by every
    /// crate.
    fn dos_device_meta(dev: &'static str) -> Result<FileMeta, u32> {
        if !winux_core::path::imp::probe_dos_device(dev) && raw_attributes(dev).is_none() {
            // neither an open nor an attribute answer: report the probe
            // error (raw_attributes was the last failing call)
            return Err(kernel32_last_error());
        }
        Ok(FileMeta {
            links: 1,
            character_device: true,
            ..FileMeta::default()
        })
    }

    /// Build an extended-length (\\?\) path for API calls; relative paths
    /// are absolutized against `ctx.cwd` first. Forward slashes are folded
    /// to backslashes because \\?\-prefixed paths get no normalization.
    ///
    /// This is THE operand boundary (1.x native_path.cppm
    /// make_api_path_operand_w + normalize_api_operand_w, routed through
    /// the winux-core port in wave3b #1488): pseudo-devices resolve to
    /// their Windows device names, the /cygdrive/X and /mnt/X prefixes
    /// fold onto the MSYS /X spelling, /X folds onto X:\, carrier colons
    /// fold onto U+F03A, and the remaining POSIX spellings (/tmp, /home,
    /// /usr/..., the bare "/") map through the configured shell tree.
    pub fn api_path(ctx: &Ctx, operand: &str) -> String {
        // A DOS device name keeps its device magic only WITHOUT the \\?\
        // prefix ("\\?\D:\dir\NUL" is a literal file named NUL, not the
        // null device), so the canonical device name passes through
        // verbatim (any reserved name: NUL, CON, COM4, ...;
        // make_api_path_operand_w keeps it in both normalized and
        // extended).
        if let Some(dev) = winux_core::path::resolve_pseudo_device(operand) {
            return dev.to_string();
        }
        let operand = winux_core::path::normalize_operand(ctx.env, &ctx.cwd, operand);
        let p = if Path::new(&operand).is_absolute() {
            std::path::PathBuf::from(&operand)
        } else {
            ctx.cwd.join(&operand)
        };
        let s = p.to_string_lossy().replace('/', "\\");
        // Collapse "." components lexically: the \\?\ prefix gets no
        // normalization from the NT loader, so a caller-spelled
        // ".\name" joined against the cwd would reach CreateFileW /
        // CreateHardLinkW as "...\.\name" and fail with
        // ERROR_INVALID_PARAMETER (1.x native_path normalized the same
        // segments at the operand boundary). ".." components are left
        // alone: collapsing them needs component math the 1.x kept out of
        // this path too.
        let mut s = s;
        while let Some(pos) = s.find(r"\.\") {
            s.replace_range(pos..pos + 3, r"\");
        }
        while s.ends_with(r"\.") {
            s.truncate(s.len() - 2);
        }
        let s = s;
        if let Some(rest) = s.strip_prefix(r"\\?\") {
            return format!(r"\\?\{rest}");
        }
        if let Some(rest) = s.strip_prefix(r"\\") {
            // UNC: \\?\UNC\server\share
            return format!(r"\\?\UNC\{rest}");
        }
        format!(r"\\?\{s}")
    }

    fn filetime_to_systemtime(ft: i64) -> Option<SystemTime> {
        let raw = FileTime::from_u64(ft as u64);
        let mut st = SystemTime::default();
        unsafe {
            if FileTimeToSystemTime(&raw, &mut st) == 0 {
                return None;
            }
        }
        Some(st)
    }

    fn systemtime_to_filetime(st: &SystemTime) -> Option<FileTime> {
        let mut ft = FileTime::default();
        unsafe {
            if SystemTimeToFileTime(st, &mut ft) == 0 {
                return None;
            }
        }
        Some(ft)
    }

    /// UTC FILETIME -> local broken-down time plus the UTC offset in
    /// minutes (local = utc - offset... printed offset = utc - local).
    pub fn to_local(ft: i64) -> Option<(SystemTime, i32)> {
        let utc = filetime_to_systemtime(ft)?;
        let mut local = SystemTime::default();
        unsafe {
            if SystemTimeToTzSpecificLocalTime(std::ptr::null_mut(), &utc, &mut local) == 0 {
                return None;
            }
        }
        // offset minutes = local - utc (positive east of Greenwich),
        // rounded to the nearest minute (SYSTEMTIME drops sub-seconds)
        let local_ft = systemtime_to_filetime(&local)?;
        let delta = local_ft.as_u64() as i64 - ft;
        let diff_min = ((delta + 30_000_000) / 600_000_000) as i32;
        Some((local, diff_min))
    }

    /// Naive local broken-down time -> UTC FILETIME (DST aware).
    pub fn from_local(st: &SystemTime) -> Option<FileTime> {
        let mut utc = SystemTime::default();
        unsafe {
            if TzSpecificLocalTimeToSystemTime(std::ptr::null_mut(), st, &mut utc) == 0 {
                return None;
            }
        }
        systemtime_to_filetime(&utc)
    }

    #[allow(dead_code)]
    fn now_filetime() -> FileTime {
        let now = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap_or_default();
        FileTime::from_epoch(now.as_secs() as i64, now.subsec_nanos())
    }

    /// GetFileAttributesW-based probe (no handle open) for existence/type.
    fn raw_attributes(extended: &str) -> Option<DWORD> {
        let w = wide(extended);
        let attrs = unsafe { GetFileAttributesW(w.as_ptr()) };
        if attrs == 0xFFFF_FFFF {
            None
        } else {
            Some(attrs)
        }
    }

    /// Open the path and snapshot its metadata. `lstat` keeps reparse
    /// points unopened (symlink operands report the link itself).
    pub fn file_meta(ctx: &Ctx, operand: &str, lstat: bool) -> Result<FileMeta, u32> {
        // DOS device operands take the synthesized device record before the
        // ordinary handle snapshot (see dos_device_meta): the C++ 1.x stat
        // applet took the same attribute-record path (#276).
        if let Some(dev) = winux_core::path::resolve_pseudo_device(operand) {
            return dos_device_meta(dev);
        }
        let extended = api_path(ctx, operand);
        let mut meta = FileMeta::default();
        // symlink check first: affects both open flags and reporting
        let attrs = raw_attributes(&extended);
        let is_reparse = attrs.map(|a| a & FILE_ATTRIBUTE_REPARSE_POINT != 0);
        let handle = unsafe {
            let mut flags = FILE_FLAG_BACKUP_SEMANTICS;
            if lstat && is_reparse == Some(true) {
                flags |= FILE_FLAG_OPEN_REPARSE_POINT;
            }
            let w = wide(&extended);
            CreateFileW(
                w.as_ptr(),
                GENERIC_READ | FILE_READ_ATTRIBUTES,
                FILE_SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                flags,
                std::ptr::null_mut(),
            )
        };
        if handle == INVALID_HANDLE_VALUE || handle.is_null() {
            // Dangling symlink with lstat: FindFirstFileW gives the link's
            // own metadata (the C++ lstat path does the same).
            if lstat {
                return find_first_meta(ctx, &extended);
            }
            return Err(kernel32_last_error());
        }
        unsafe {
            let mut info = ByHandleFileInformation::default();
            if GetFileInformationByHandle(handle, &mut info) == 0 {
                let e = kernel32_last_error();
                CloseHandle(handle);
                return Err(e);
            }
            let mut basic = FileBasicInfo::default();
            let got_basic = GetFileInformationByHandleEx(
                handle,
                FILE_BASIC_INFO_CLASS,
                &mut basic as *mut _ as *mut c_void,
                std::mem::size_of::<FileBasicInfo>() as DWORD,
            ) != 0;
            meta.is_symlink = info.attributes & FILE_ATTRIBUTE_REPARSE_POINT != 0;
            meta.is_dir = info.attributes & FILE_ATTRIBUTE_DIRECTORY != 0;
            meta.read_only = info.attributes & FILE_ATTRIBUTE_READONLY != 0;
            meta.character_device = info.attributes & FILE_ATTRIBUTE_DEVICE != 0;
            meta.size = (info.size_high as u64) << 32 | info.size_low as u64;
            meta.links = info.links as u64;
            meta.volume_serial = info.volume_serial;
            meta.index = (info.index_high as u64) << 32 | info.index_low as u64;
            meta.btime = info.creation_time;
            meta.atime = info.last_access_time;
            meta.mtime = info.last_write_time;
            if got_basic {
                meta.ctime = FileTime::from_u64(basic.change_time as u64);
                if basic.creation_time != 0 {
                    meta.btime = FileTime::from_u64(basic.creation_time as u64);
                }
                if basic.last_access_time != 0 {
                    meta.atime = FileTime::from_u64(basic.last_access_time as u64);
                }
                if basic.last_write_time != 0 {
                    meta.mtime = FileTime::from_u64(basic.last_write_time as u64);
                }
            }
            if lstat && meta.is_symlink {
                // GNU lstat: a symlink's st_size is the target-path byte
                // length (platform contract #1310, src/commands/find.cpp
                // find_size_bytes: "target-path length on this platform");
                // a reparse-point-opened handle reports 0/allocated bytes,
                // so the size comes from the stored target like the
                // find_first_meta fallback path below.
                if let Ok(t) = std::fs::read_link(&extended) {
                    meta.size = t.to_string_lossy().len() as u64;
                }
            }
            CloseHandle(handle);
        }
        Ok(meta)
    }

    fn find_first_meta(ctx: &Ctx, extended: &str) -> Result<FileMeta, u32> {
        // FindFirstFileW on the \\?\ path's parent + name
        let full = api_path(ctx, &strip_extended(extended));
        let w = wide(&full);
        let mut meta = FileMeta::default();
        #[repr(C)]
        struct FindData {
            attrs: DWORD,
            ctime: FileTime,
            atime: FileTime,
            mtime: FileTime,
            size_high: DWORD,
            size_low: DWORD,
            _reserved0: DWORD,
            _reserved1: DWORD,
            _reserved2: DWORD,
            _reserved3: DWORD,
            _reserved4: DWORD,
            _reserved5: DWORD,
            name: [u16; 260],
            alt: [u16; 14],
        }
        #[link(name = "kernel32")]
        extern "system" {
            fn FindFirstFileW(name: *const u16, data: *mut FindData) -> Handle;
            fn FindClose(h: Handle) -> i32;
        }
        let mut data = FindData {
            attrs: 0,
            ctime: FileTime::default(),
            atime: FileTime::default(),
            mtime: FileTime::default(),
            size_high: 0,
            size_low: 0,
            _reserved0: 0,
            _reserved1: 0,
            _reserved2: 0,
            _reserved3: 0,
            _reserved4: 0,
            _reserved5: 0,
            name: [0; 260],
            alt: [0; 14],
        };
        let h = unsafe { FindFirstFileW(w.as_ptr(), &mut data) };
        if h == INVALID_HANDLE_VALUE || h.is_null() {
            return Err(kernel32_last_error());
        }
        unsafe {
            FindClose(h);
            meta.is_symlink = data.attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0;
            meta.is_dir = data.attrs & FILE_ATTRIBUTE_DIRECTORY != 0;
            meta.read_only = data.attrs & FILE_ATTRIBUTE_READONLY != 0;
            meta.character_device = data.attrs & FILE_ATTRIBUTE_DEVICE != 0;
            meta.size = (data.size_high as u64) << 32 | data.size_low as u64;
            meta.links = 1;
            meta.btime = data.ctime;
            meta.atime = data.atime;
            meta.mtime = data.mtime;
            meta.ctime = data.mtime;
        }
        // lstat of a symlink reports the target name length as the size
        if meta.is_symlink {
            if let Ok(target) = std::fs::read_link(strip_extended(&full)) {
                meta.size = target.to_string_lossy().len() as u64;
            }
        }
        Ok(meta)
    }

    fn strip_extended(p: &str) -> String {
        if let Some(rest) = p.strip_prefix(r"\\?\UNC\") {
            format!(r"\\{rest}")
        } else if let Some(rest) = p.strip_prefix(r"\\?\") {
            rest.to_string()
        } else {
            p.to_string()
        }
    }

    pub fn kernel32_last_error() -> u32 {
        #[link(name = "kernel32")]
        extern "system" {
            fn GetLastError() -> DWORD;
        }
        unsafe { GetLastError() }
    }

    /// SetFileTime on the path; None fields keep their current value.
    /// `no_dereference` opens reparse points directly (touch -h).
    pub fn set_file_times(
        ctx: &Ctx,
        operand: &str,
        ctime: Option<FileTime>,
        atime: Option<FileTime>,
        mtime: Option<FileTime>,
        is_dir_hint: bool,
        no_dereference: bool,
    ) -> Result<(), u32> {
        let extended = api_path(ctx, operand);
        let mut flags = FILE_FLAG_BACKUP_SEMANTICS;
        if no_dereference {
            flags |= FILE_FLAG_OPEN_REPARSE_POINT;
        }
        let w = wide(&extended);
        let handle = unsafe {
            CreateFileW(
                w.as_ptr(),
                0x0100, // FILE_WRITE_ATTRIBUTES
                FILE_SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                flags,
                std::ptr::null_mut(),
            )
        };
        if handle == INVALID_HANDLE_VALUE || handle.is_null() {
            // directory operands may need the backup-semantics open only
            if is_dir_hint && no_dereference {
                return set_file_times(ctx, operand, ctime, atime, mtime, is_dir_hint, false);
            }
            return Err(kernel32_last_error());
        }
        let c = ctime.unwrap_or_else(zero_ft);
        let a = atime.unwrap_or_else(zero_ft);
        let m = mtime.unwrap_or_else(zero_ft);
        let rc = unsafe { SetFileTime(handle, &c, &a, &m) };
        let err = if rc == 0 {
            Some(kernel32_last_error())
        } else {
            None
        };
        unsafe { CloseHandle(handle) };
        match err {
            Some(e) => Err(e),
            None => Ok(()),
        }
    }

    fn zero_ft() -> FileTime {
        FileTime { low: 0, high: 0 }
    }

    /// Remove a file through DeleteFileW directly (1.x remove semantics):
    /// the NT delete fails on a read-only file with ERROR_ACCESS_DENIED,
    /// which is the GNU-on-Windows/MSYS "failed to remove 'X': Permission
    /// denied" shape the ln -f corpus pins. std::fs::remove_file is NOT
    /// usable here — it silently clears the read-only attribute first.
    pub fn delete_file(ctx: &Ctx, operand: &str) -> Result<(), u32> {
        let w = wide(&api_path(ctx, operand));
        let rc = unsafe { DeleteFileW(w.as_ptr()) };
        if rc == 0 {
            Err(kernel32_last_error())
        } else {
            Ok(())
        }
    }

    /// Retime the process's standard output handle — the POSIX `touch -`
    /// operand (GNU touch.c:127-128 retimes stdout instead of naming a
    /// literal file). Returns the Win32 error on failure (pipes and other
    /// untimable handles reject SetFileTime, the same EINVAL shape GNU
    /// reports under a redirected-pipe consumer).
    pub fn set_stdout_file_times(
        atime: Option<FileTime>,
        mtime: Option<FileTime>,
    ) -> Result<(), u32> {
        const STD_OUTPUT_HANDLE: DWORD = (-11i32) as u32; // (DWORD)-11
        let handle = unsafe { GetStdHandle(STD_OUTPUT_HANDLE) };
        if handle.is_null() || handle == INVALID_HANDLE_VALUE {
            return Err(6); // ERROR_INVALID_HANDLE
        }
        let a = atime.unwrap_or_else(zero_ft);
        let m = mtime.unwrap_or_else(zero_ft);
        let rc = unsafe { SetFileTime(handle, std::ptr::null(), &a, &m) };
        if rc == 0 {
            Err(kernel32_last_error())
        } else {
            Ok(())
        }
    }

    // ------------------------------------------------------------------
    // The #1038 fifo-marker bridge (rust2 port of 1.x file_io.cppm
    // open_fifo_read_handle/open_fifo_write_handle and native_path.cppm
    // is_winux_fifo_w / winux_fifo_pipe_name_w). An on-disk `!<fifo>`
    // marker (FILE_ATTRIBUTE_SYSTEM regular file, mkfifo's product) is
    // bridged to a \\.\pipe\winuxcmd-fifo-<hash> endpoint with POSIX
    // open() semantics: a reader creates the server end and blocks in
    // ConnectNamedPipe until a writer arrives; a writer polls CreateFileW
    // until a reader has created the endpoint. The 16-hex hash key is
    // FNV-1a over the UTF-16LE bytes of the lowercased, backslash-folded
    // canonical path — byte-identical to the C++ derivation so the two
    // sides rendezvous on the same endpoint. rust2 commands that touch
    // operand files (tee writes, paste reads, stat classifies) MUST go
    // through these helpers for markers; a plain std::fs open would clobber
    // the marker / read its 7 bytes (the wave3a CI regression family).
    // ------------------------------------------------------------------

    const FIFO_MARKER: &[u8; 7] = b"!<fifo>";

    /// \\.\pipe endpoint name for a fifo marker (native_path.cppm
    /// winux_fifo_pipe_name_w port). The C++ side derives the name from its
    /// canonicalized path; callers here pass the api_path() spelling, so
    /// the \\?\ and \\?\UNC\ prefixes are stripped first to recover the
    /// same plain path shape, then the identical fold (lowercase,
    /// '/'->'\\') and FNV-1a hash run.
    fn winux_fifo_pipe_name(extended: &str) -> String {
        let plain = if let Some(rest) = extended.strip_prefix(r"\\?\UNC\") {
            format!(r"\\{rest}")
        } else if let Some(rest) = extended.strip_prefix(r"\\?\") {
            rest.to_string()
        } else {
            extended.to_string()
        };
        let mut canon: Vec<u16> = plain
            .chars()
            .map(|ch| {
                let ch = if ch == '/' { '\\' } else { ch };
                let ch = if ch.is_ascii_uppercase() {
                    ch.to_ascii_lowercase()
                } else {
                    ch
                };
                ch as u16
            })
            .collect();
        let mut hash: u64 = 14_695_981_039_346_656_037; // FNV-1a 64-bit offset
        for ch in canon.drain(..) {
            for byte in [ch & 0xFF, ch >> 8] {
                hash = (hash ^ u64::from(byte)).wrapping_mul(1_099_511_628_211);
            }
        }
        format!(r"\\.\pipe\winuxcmd-fifo-{hash:016x}")
    }

    /// Does `extended` name a fifo marker? (native_path.cppm
    /// is_winux_fifo_w port: SYSTEM attribute, non-directory, content is
    /// exactly the 7-byte marker.)
    pub fn is_winux_fifo(extended: &str) -> bool {
        // strip trailing separators like the C++ probe (1.x
        // strip_trailing_separators); api_path output rarely carries one.
        let path = extended.trim_end_matches(['\\', '/']);
        if path.is_empty() {
            return false;
        }
        let attrs = match raw_attributes(path) {
            Some(a) => a,
            None => return false,
        };
        if attrs & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_SYSTEM) != FILE_ATTRIBUTE_SYSTEM {
            return false;
        }
        let w = wide(path);
        let handle = unsafe {
            CreateFileW(
                w.as_ptr(),
                GENERIC_READ,
                FILE_SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                FILE_ATTRIBUTE_NORMAL,
                std::ptr::null_mut(),
            )
        };
        if handle == INVALID_HANDLE_VALUE || handle.is_null() {
            return false;
        }
        let mut buffer = [0u8; FIFO_MARKER.len()];
        let mut read: DWORD = 0;
        let ok = unsafe {
            ReadFile(
                handle,
                buffer.as_mut_ptr().cast(),
                buffer.len() as DWORD,
                &mut read,
                std::ptr::null_mut(),
            )
        };
        unsafe { CloseHandle(handle) };
        ok != 0 && read == buffer.len() as DWORD && buffer.as_slice() == FIFO_MARKER
    }

    /// Reader side of the bridge (file_io.cppm open_fifo_read_handle port):
    /// create the endpoint server and block in ConnectNamedPipe until a
    /// writer connects. Returns a std::fs::File owning the pipe handle so
    /// reads stream and the handle closes on drop.
    pub fn open_fifo_read(extended: &str) -> std::io::Result<std::fs::File> {
        use std::os::windows::io::FromRawHandle;
        let name = winux_fifo_pipe_name(extended);
        let w = wide(&name);
        let pipe = unsafe {
            CreateNamedPipeW(
                w.as_ptr(),
                PIPE_ACCESS_INBOUND,
                PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
                PIPE_UNLIMITED_INSTANCES,
                64 * 1024,
                64 * 1024,
                0,
                std::ptr::null_mut(),
            )
        };
        if pipe == INVALID_HANDLE_VALUE || pipe.is_null() {
            return Err(std::io::Error::from_raw_os_error(
                kernel32_last_error() as i32
            ));
        }
        let connected = unsafe { ConnectNamedPipe(pipe, std::ptr::null_mut()) };
        if connected == 0 && kernel32_last_error() != ERROR_PIPE_CONNECTED {
            unsafe { CloseHandle(pipe) };
            return Err(std::io::Error::from_raw_os_error(
                kernel32_last_error() as i32
            ));
        }
        Ok(unsafe { std::fs::File::from_raw_handle(pipe as _) })
    }

    /// Writer side of the bridge (file_io.cppm open_fifo_write_handle
    /// port): poll CreateFileW until the reader's endpoint appears —
    /// ERROR_PIPE_BUSY waits via WaitNamedPipeW, ERROR_FILE_NOT_FOUND
    /// sleeps 1ms and retries, exactly like the C++ loop. GNU O_WRONLY on
    /// a fifo blocks until a reader opens; the marker file itself is never
    /// opened for writing, so its content stays intact.
    pub fn open_fifo_write(extended: &str) -> std::io::Result<std::fs::File> {
        use std::os::windows::io::FromRawHandle;
        let name = winux_fifo_pipe_name(extended);
        let w = wide(&name);
        loop {
            let pipe = unsafe {
                CreateFileW(
                    w.as_ptr(),
                    0x4000_0000, // GENERIC_WRITE
                    0,
                    std::ptr::null_mut(),
                    OPEN_EXISTING,
                    FILE_ATTRIBUTE_NORMAL,
                    std::ptr::null_mut(),
                )
            };
            if pipe != INVALID_HANDLE_VALUE && !pipe.is_null() {
                return Ok(unsafe { std::fs::File::from_raw_handle(pipe as _) });
            }
            match kernel32_last_error() {
                ERROR_PIPE_BUSY => {
                    unsafe { WaitNamedPipeW(w.as_ptr(), NMPWAIT_WAIT_FOREVER) };
                }
                ERROR_FILE_NOT_FOUND => unsafe { Sleep(1) },
                err => {
                    return Err(std::io::Error::from_raw_os_error(err as i32));
                }
            }
        }
    }

    /// Create the file if missing and set both times (GNU touch default).
    pub fn create_file(ctx: &Ctx, operand: &str) -> Result<(), u32> {
        let extended = api_path(ctx, operand);
        let w = wide(&extended);
        const CREATE_ALWAYS: DWORD = 2;
        const GENERIC_WRITE: DWORD = 0x4000_0000;
        let handle = unsafe {
            CreateFileW(
                w.as_ptr(),
                GENERIC_WRITE,
                FILE_SHARE_ALL,
                std::ptr::null_mut(),
                CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL,
                std::ptr::null_mut(),
            )
        };
        if handle == INVALID_HANDLE_VALUE || handle.is_null() {
            return Err(kernel32_last_error());
        }
        unsafe { CloseHandle(handle) };
        Ok(())
    }

    pub fn hard_link(ctx: &Ctx, existing: &str, link: &str) -> Result<(), u32> {
        let e = wide(&api_path(ctx, existing));
        let l = wide(&api_path(ctx, link));
        let rc = unsafe { CreateHardLinkW(l.as_ptr(), e.as_ptr(), std::ptr::null_mut()) };
        if rc == 0 {
            Err(kernel32_last_error())
        } else {
            Ok(())
        }
    }

    pub fn create_symlink(ctx: &Ctx, target: &str, link: &str, dir: bool) -> Result<(), u32> {
        // GNU symlink(2) stores the target string verbatim and readlink
        // prints it back unchanged; the 1.x pipeline fed CreateSymbolicLinkW
        // the raw text with only '/' folded to '\' (src/utils/win32.cppm
        // win32_normalize_symlink_target: "store the user-visible link text
        // unchanged, but normalize what goes into the reparse point").
        // The operand-boundary folds (normalize_operand) stay applied to the
        // TARGET because a POSIX-absolute spelling cannot resolve natively
        // from a reparse point: "/c/x" must land on drive C (the drive
        // dialect fold, ctest ln_symlink_drive_dialect_lands_on_operand_drive).
        // normalize_operand is identity for relative and already-native
        // spellings, so those keep the verbatim text — api_path would have
        // absolutized a relative target against ctx.cwd, baking an absolute
        // path into the reparse point. The LINK operand keeps api_path.
        let t = wide(
            &winux_core::path::normalize_operand(ctx.env, &ctx.cwd, target).replace('/', "\\"),
        );
        let l = wide(&api_path(ctx, link));
        let mut flags = if dir {
            SYMBOLIC_LINK_FLAG_DIRECTORY
        } else {
            SYMBOLIC_LINK_FLAG_FILE
        };
        let mut rc = unsafe { CreateSymbolicLinkW(l.as_ptr(), t.as_ptr(), flags) };
        if rc == 0 {
            // retry with the unprivileged-create bit when the first call
            // is rejected (older systems: ERROR_INVALID_PARAMETER)
            flags |= SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
            rc = unsafe { CreateSymbolicLinkW(l.as_ptr(), t.as_ptr(), flags) };
            if rc == 0 {
                flags = if dir {
                    SYMBOLIC_LINK_FLAG_DIRECTORY
                } else {
                    SYMBOLIC_LINK_FLAG_FILE
                };
                rc = unsafe { CreateSymbolicLinkW(l.as_ptr(), t.as_ptr(), flags) };
            }
        }
        if rc == 0 {
            Err(kernel32_last_error())
        } else {
            Ok(())
        }
    }

    #[repr(C)]
    #[allow(dead_code)]
    struct SidAuthority {
        value: [u8; 6],
    }

    /// Owner/group account of a path: (name, numeric id) per side, using
    /// the 1.x id formula (src/utils/win32.cppm win32_account_id_from_sid).
    pub fn file_accounts(ctx: &Ctx, operand: &str) -> ((String, String), (String, String)) {
        let extended = api_path(ctx, operand);
        let w = wide(&extended);
        let mut owner: *mut c_void = std::ptr::null_mut();
        let mut group: *mut c_void = std::ptr::null_mut();
        let mut sd: *mut c_void = std::ptr::null_mut();
        let status = unsafe {
            GetNamedSecurityInfoW(
                w.as_ptr(),
                SE_FILE_OBJECT,
                OWNER_SECURITY_INFORMATION | GROUP_SECURITY_INFORMATION,
                &mut owner,
                &mut group,
                std::ptr::null_mut(),
                std::ptr::null_mut(),
                &mut sd,
            )
        };
        let fallback = (
            ("0".to_string(), String::new()),
            ("0".to_string(), String::new()),
        );
        if status != 0 {
            return fallback;
        }
        let o = if owner.is_null() {
            ("0".to_string(), String::new())
        } else {
            account_from_sid(owner)
        };
        let g = if group.is_null() {
            ("0".to_string(), String::new())
        } else {
            account_from_sid(group)
        };
        if !sd.is_null() {
            unsafe { LocalFree(sd) };
        }
        (o, g)
    }

    fn account_from_sid(sid: *mut c_void) -> (String, String) {
        let id = sid_to_id(sid).unwrap_or_else(|| "0".to_string());
        let name = lookup_sid_name(sid);
        (id, name)
    }

    fn sid_to_id(sid: *mut c_void) -> Option<String> {
        unsafe {
            if sid.is_null() || IsValidSid(sid) == 0 {
                return None;
            }
            let count = *GetSidSubAuthorityCount(sid);
            if count == 0 {
                return None;
            }
            let rid = *GetSidSubAuthority(sid, (count - 1) as DWORD);
            let auth = GetSidIdentifierAuthority(sid);
            let mut authority: u64 = 0;
            let value: [u8; 6] = std::ptr::read_unaligned(auth as *const [u8; 6]);
            for b in value {
                authority = (authority << 8) | b as u64;
            }
            let first = *GetSidSubAuthority(sid, 0);
            if authority == 5 && first == 21 && count >= 5 {
                return Some((0x30000u64 + rid as u64).to_string());
            }
            if authority == 16 {
                return Some((0x60000u64 + rid as u64).to_string());
            }
            Some(rid.to_string())
        }
    }

    fn lookup_sid_name(sid: *mut c_void) -> String {
        unsafe {
            let mut name_size = 0u32;
            let mut domain_size = 0u32;
            let mut use_: DWORD = 0;
            LookupAccountSidW(
                std::ptr::null(),
                sid,
                std::ptr::null(),
                &mut name_size,
                std::ptr::null(),
                &mut domain_size,
                &mut use_,
            );
            if name_size == 0 {
                return String::new();
            }
            let mut name = vec![0u16; name_size as usize];
            let mut domain = vec![0u16; domain_size.max(1) as usize];
            let mut dsize = domain_size;
            let rc = LookupAccountSidW(
                std::ptr::null(),
                sid,
                name.as_mut_ptr(),
                &mut name_size,
                domain.as_mut_ptr(),
                &mut dsize,
                &mut use_,
            );
            if rc == 0 {
                return String::new();
            }
            let len = name.iter().take_while(|&&c| c != 0).count();
            String::from_utf16_lossy(&name[..len])
        }
    }
}

#[cfg(not(windows))]
pub mod imp {
    use super::*;

    pub fn file_meta(_ctx: &Ctx, _operand: &str, _lstat: bool) -> Result<FileMeta, u32> {
        Err(2)
    }
    pub fn set_file_times(
        _ctx: &Ctx,
        _operand: &str,
        _ctime: Option<FileTime>,
        _atime: Option<FileTime>,
        _mtime: Option<FileTime>,
        _is_dir_hint: bool,
        _no_dereference: bool,
    ) -> Result<(), u32> {
        Err(1)
    }
    pub fn set_stdout_file_times(
        _atime: Option<FileTime>,
        _mtime: Option<FileTime>,
    ) -> Result<(), u32> {
        Err(1)
    }
    pub fn delete_file(_ctx: &Ctx, _operand: &str) -> Result<(), u32> {
        Err(1)
    }
    pub fn create_file(_ctx: &Ctx, _operand: &str) -> Result<(), u32> {
        Err(1)
    }
    pub fn hard_link(_ctx: &Ctx, _e: &str, _l: &str) -> Result<(), u32> {
        Err(1)
    }
    pub fn create_symlink(_ctx: &Ctx, _t: &str, _l: &str, _d: bool) -> Result<(), u32> {
        Err(1)
    }
    pub fn file_accounts(_ctx: &Ctx, _operand: &str) -> ((String, String), (String, String)) {
        (
            ("0".to_string(), String::new()),
            ("0".to_string(), String::new()),
        )
    }
    pub fn to_local(_ft: i64) -> Option<(SystemTime, i32)> {
        None
    }
    pub fn from_local(_st: &SystemTime) -> Option<FileTime> {
        None
    }
    pub fn api_path(ctx: &Ctx, operand: &str) -> String {
        let mapped = winux_core::path::normalize_operand(ctx.env, &ctx.cwd, operand);
        if Path::new(&mapped).is_absolute() {
            mapped
        } else {
            ctx.cwd.join(&mapped).to_string_lossy().into_owned()
        }
    }
}

pub use imp::*;

/// Bare-name `.exe` transparency for the stat family (unixwin/WinuxCmd#1572):
/// `file_meta` on an extensionless operand that does not exist retries with
/// `.exe` appended, so `stat`/`ls` answer the same bare name the engine's
/// `test -e/-x` and the child-process PATHEXT resolution already answer.
///
/// The retry is deliberately narrow and stat-family-only: it lives in this
/// wrapper (used by `stat.rs` only), NOT in `file_meta`/`api_path`, so
/// create/open callers (`touch`, `ln`, `cp`, ...) keep creating exactly the
/// name they were given. Only `.exe` is tried (the issue's `java.exe` case
/// and the MSYS/Cygwin stat rule); `.com`/`.bat`/`.cmd` stay exec-only.
/// An explicit extension suppresses the retry (the childproc PATHEXT rule),
/// as does a trailing separator (a directory operand must stay a directory
/// probe). On retry failure the ORIGINAL error is returned, so the message
/// still names the operand as typed.
fn exe_retry_candidate(operand: &str) -> Option<String> {
    if operand.is_empty() || operand.ends_with('/') || operand.ends_with('\\') {
        return None;
    }
    // A dot in the final component means the caller named the extension
    // (mirrors childproc::lowercase_extension).
    let slash = operand.rfind(['/', '\\']);
    if let Some(dot) = operand.rfind('.') {
        if slash.is_none_or(|s| dot > s) {
            return None;
        }
    }
    Some(format!("{operand}.exe"))
}

/// `file_meta` plus the bare-name `.exe` retry above. Returns the metadata
/// and the operand actually statted (the typed name, or the `+ ".exe"`
/// candidate); callers that re-probe the path (owner/group, symlink target)
/// must use the returned name, while display keeps the typed name.
pub fn file_meta_exe(ctx: &Ctx, operand: &str, lstat: bool) -> Result<(FileMeta, String), u32> {
    match file_meta(ctx, operand, lstat) {
        Ok(m) => Ok((m, operand.to_string())),
        Err(e) if e == 2 || e == 3 => match exe_retry_candidate(operand) {
            Some(candidate) => match file_meta(ctx, &candidate, lstat) {
                Ok(m) => Ok((m, candidate)),
                Err(_) => Err(e),
            },
            None => Err(e),
        },
        Err(e) => Err(e),
    }
}

/// Win32 error -> GNU strerror-style reason string (the same mapping the
/// 1.x C++ applets use; 1314 is the documented no-Developer-Mode case).
pub fn win32_reason(err: u32) -> &'static str {
    match err {
        2 => "No such file or directory", // ERROR_FILE_NOT_FOUND
        3 => "No such file or directory", // ERROR_PATH_NOT_FOUND
        5 => "Permission denied",         // ERROR_ACCESS_DENIED
        80 => "File exists",              // ERROR_FILE_EXISTS
        183 => "File exists",             // ERROR_ALREADY_EXISTS
        206 => "File name too long",      // ERROR_FILENAME_EXCED_RANGE
        267 => "Not a directory",         // ERROR_DIRECTORY (ENOTDIR shape;
        // touch's trailing-separator operand reports GNU's exact wording
        // with it: probed `touch f.txt/` -> "setting times of 'f.txt/':
        // Not a directory")
        2062 => "No such file or directory", // ERROR_CANT_ACCESS_FILE alt
        1314 => "Operation not permitted",   // ERROR_PRIVILEGE_NOT_HELD
        1 => "Operation not permitted",      // ERROR_INVALID_FUNCTION
        1920 => "No such file or directory", // ERROR_CANT_ACCESS_FILE
        _ => "Invalid argument",
    }
}

/// Reader end of the #1038 fifo bridge (`open_fifo_read`): a std File
/// over the named-pipe handle whose EOF contract matches the C++
/// read_handle_to_string rule — once the writer closes, ReadFile fails
/// with ERROR_BROKEN_PIPE / ERROR_NO_DATA; that is clean EOF (GNU
/// read(2) returns 0), not an error, so buffered bytes survive.
pub struct FifoRead(std::fs::File);

impl std::io::Read for FifoRead {
    fn read(&mut self, buf: &mut [u8]) -> std::io::Result<usize> {
        match self.0.read(buf) {
            Ok(n) => Ok(n),
            Err(e)
                if e.kind() == std::io::ErrorKind::BrokenPipe
                    || e.raw_os_error() == Some(232) /* ERROR_NO_DATA */ =>
            {
                Ok(0)
            }
            Err(e) => Err(e),
        }
    }
}

/// Writer end of the #1038 fifo bridge (`open_fifo_write`): a std File
/// over the named-pipe handle whose flush is a no-op. GNU tee writes a
/// fifo through unbuffered write(2) and closes — there is no flushable
/// user-space layer; Win32's FlushFileBuffers on a pipe handle instead
/// blocks until the reader consumes every buffered byte, which deadlocks
/// against line-interleaving readers (paste over two fifos) and reports
/// failure shapes GNU never produces.
pub struct FifoWrite(std::fs::File);

impl std::io::Write for FifoWrite {
    fn write(&mut self, buf: &[u8]) -> std::io::Result<usize> {
        self.0.write(buf)
    }
    fn flush(&mut self) -> std::io::Result<()> {
        Ok(())
    }
}

/// Does `path` (api_path spelling) name a fifo marker? Non-Windows builds
/// have no markers (the marker is a WinuxCmd-on-Windows convention), so
/// the answer is always false there.
#[cfg(windows)]
pub fn is_winux_fifo_path(path: &str) -> bool {
    imp::is_winux_fifo(path)
}

#[cfg(not(windows))]
pub fn is_winux_fifo_path(_path: &str) -> bool {
    false
}

/// Blocking read open of a fifo operand (#1038). Non-Windows builds have
/// no bridge (see `is_winux_fifo_path`).
#[cfg(windows)]
pub fn open_fifo_read(path: &str) -> std::io::Result<FifoRead> {
    imp::open_fifo_read(path).map(FifoRead)
}

#[cfg(not(windows))]
pub fn open_fifo_read(_path: &str) -> std::io::Result<FifoRead> {
    Err(std::io::Error::new(
        std::io::ErrorKind::Unsupported,
        "no fifo bridge on this platform",
    ))
}

/// Blocking write open of a fifo operand (#1038). Non-Windows builds have
/// no bridge (see `is_winux_fifo_path`).
#[cfg(windows)]
pub fn open_fifo_write(path: &str) -> std::io::Result<FifoWrite> {
    imp::open_fifo_write(path).map(FifoWrite)
}

#[cfg(not(windows))]
pub fn open_fifo_write(_path: &str) -> std::io::Result<FifoWrite> {
    Err(std::io::Error::new(
        std::io::ErrorKind::Unsupported,
        "no fifo bridge on this platform",
    ))
}

#[cfg(test)]
mod api_path_probes {
    use super::*;
    use winux_core::MapEnv;

    /// A Ctx with cwd = base and no environment (no shell root: the
    /// shell-namespace legs stay inert and the drive folds run bare).
    #[cfg(windows)]
    fn plain_ctx(base: &Path) -> Ctx<'static> {
        let env: &'static MapEnv = Box::leak(Box::new(MapEnv::new()));
        Ctx {
            cwd: base.to_path_buf(),
            env,
        }
    }

    #[test]
    fn probe_drive_folds() {
        #[cfg(windows)]
        {
            let ctx = plain_ctx(Path::new(r"D:\wd"));
            let out = imp::api_path(&ctx, "/cygdrive/d/repo/x/f.txt");
            assert_eq!(out, r"\\?\D:\repo\x\f.txt", "cygdrive fold");
            let out = imp::api_path(&ctx, "/mnt/d/repo/x/f.txt");
            assert_eq!(out, r"\\?\D:\repo\x\f.txt", "mnt fold");
            let out = imp::api_path(&ctx, "/d/repo/x/f.txt");
            assert_eq!(out, r"\\?\D:\repo\x\f.txt", "dialect fold");
        }
    }

    #[test]
    fn dos_device_names_resolve() {
        // POSIX pseudo-devices and bare reserved names (1.x
        // resolve_pseudo_device_w / is_dos_device_name_w; the table moved
        // to winux_core::path in the wave3b port — C++ table verbatim,
        // which has no /dev/zero entry)
        assert_eq!(
            winux_core::path::resolve_pseudo_device("/dev/null"),
            Some("NUL")
        );
        assert_eq!(
            winux_core::path::resolve_pseudo_device("/dev/tty"),
            Some("CONIN$")
        );
        assert_eq!(winux_core::path::resolve_pseudo_device("nul"), Some("NUL"));
        assert_eq!(
            winux_core::path::resolve_pseudo_device("ConIN$"),
            Some("CONIN$")
        );
        assert_eq!(
            winux_core::path::resolve_pseudo_device("com4"),
            Some("COM4")
        );
        assert_eq!(
            winux_core::path::resolve_pseudo_device("LPT7"),
            Some("LPT7")
        );
        // whole operand only: a separator or extension names an ordinary file
        assert_eq!(winux_core::path::resolve_pseudo_device("dir/NUL"), None);
        assert_eq!(winux_core::path::resolve_pseudo_device("dir\\NUL"), None);
        assert_eq!(winux_core::path::resolve_pseudo_device("nul.txt"), None);
        assert_eq!(winux_core::path::resolve_pseudo_device("NULX"), None);
        assert_eq!(winux_core::path::resolve_pseudo_device("COM0"), None);
        assert_eq!(winux_core::path::resolve_pseudo_device("COM10"), None);
    }

    #[test]
    #[cfg(windows)]
    fn nul_device_probe_and_meta() {
        // the CI-safe device assertion: CreateFileW opens NUL
        assert!(winux_core::path::imp::probe_dos_device("NUL"));
        // file_meta synthesizes a character-device record (GNU %F
        // "character special file", size 0) instead of the CRT stat
        // EINVAL/EPERM refusal (#276)
        let ctx = plain_ctx(Path::new("wd"));
        let m = imp::file_meta(&ctx, "/dev/null", true).expect("/dev/null meta");
        assert!(m.character_device);
        assert!(!m.is_dir && !m.is_symlink);
        assert_eq!(m.size, 0);
        assert_eq!(m.links, 1);
        let m = imp::file_meta(&ctx, "NUL", false).expect("NUL meta");
        assert!(m.character_device);
        // a device name with a separator is an ordinary (missing) file
        assert!(imp::file_meta(&ctx, "dir/NUL", true).is_err());
        // the device name passes through api_path verbatim (no \\?\ prefix)
        assert_eq!(imp::api_path(&ctx, "/dev/null"), "NUL");
        assert_eq!(imp::api_path(&ctx, "con"), "CON");
    }

    #[test]
    fn exe_retry_candidate_rules() {
        // #1572: bare names retry with ".exe"; anything that already names
        // an extension, or a directory probe, does not.
        assert_eq!(
            super::exe_retry_candidate("java"),
            Some("java.exe".to_string())
        );
        assert_eq!(
            super::exe_retry_candidate(r"bin\java"),
            Some(r"bin\java.exe".to_string())
        );
        assert_eq!(
            super::exe_retry_candidate("bin/java"),
            Some("bin/java.exe".to_string())
        );
        assert_eq!(super::exe_retry_candidate("java.exe"), None);
        assert_eq!(super::exe_retry_candidate("java.txt"), None);
        assert_eq!(
            super::exe_retry_candidate("dir.with.dots/java"),
            Some("dir.with.dots/java.exe".to_string())
        );
        assert_eq!(super::exe_retry_candidate("java/"), None);
        assert_eq!(super::exe_retry_candidate(r"java\"), None);
        assert_eq!(super::exe_retry_candidate(""), None);
    }

    #[test]
    #[cfg(windows)]
    fn shell_tree_maps_through_winux_core() {
        // wave3b: with a shell root configured, the "/"-prefixed spellings
        // resolve through the tree; /tmp pins to TMPDIR (the per-user temp
        // namespace, niubash#94).
        let env: &'static MapEnv = Box::leak(Box::new({
            let mut e = MapEnv::new();
            e.set("WINUXSH_ROOT", r"D:\shellroot");
            e.set("TMPDIR", r"C:\tmproot");
            e
        }));
        let ctx = Ctx {
            cwd: std::path::PathBuf::from(r"D:\wd"),
            env,
        };
        assert_eq!(imp::api_path(&ctx, "/"), r"\\?\D:\shellroot");
        assert_eq!(
            imp::api_path(&ctx, "/tmp/probe/dir"),
            r"\\?\C:\tmproot\probe\dir"
        );
        assert_eq!(
            imp::api_path(&ctx, "/etc/passwd"),
            r"\\?\D:\shellroot\etc\passwd"
        );
        assert_eq!(imp::api_path(&ctx, "/mnt/d/x"), r"\\?\D:\x");
    }
}
