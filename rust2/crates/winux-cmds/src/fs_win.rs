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
        let delta = ticks - 11_644_473_600_000_000i64;
        (delta.div_euclid(10_000_000), delta.rem_euclid(10_000_000))
    }
    pub fn from_epoch(secs: i64, nanos: u32) -> Self {
        let ticks = secs * 10_000_000 + (nanos / 100) as i64 + 11_644_473_600_000_000i64;
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
        fn SetFileTime(
            h: Handle,
            ctime: *const FileTime,
            atime: *const FileTime,
            mtime: *const FileTime,
        ) -> i32;
        fn CreateHardLinkW(link: *const u16, existing: *const u16, security: *mut c_void) -> i32;
        fn CreateSymbolicLinkW(link: *const u16, target: *const u16, flags: DWORD) -> i32;
        fn SystemTimeToFileTime(st: *const SystemTime, ft: *mut FileTime) -> i32;
        fn FileTimeToSystemTime(ft: *const FileTime, st: *mut SystemTime) -> i32;
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

    pub fn wide(s: &str) -> Vec<u16> {
        OsStr::new(s)
            .encode_wide()
            .chain(std::iter::once(0))
            .collect()
    }

    /// Build an extended-length (\\?\) path for API calls; relative paths
    /// are absolutized against `base` first. Forward slashes are folded to
    /// backslashes because \\?\-prefixed paths get no normalization.
    /// Map the POSIX device paths the 1.x C++ applets honor onto their
    /// Windows DOS devices (native_path::file_attribute_data_w family).
    pub fn map_device(operand: &str) -> &str {
        match operand {
            "/dev/null" => "NUL",
            "/dev/zero" => "NUL",
            _ => operand,
        }
    }

    pub fn api_path(base: &Path, operand: &str) -> String {
        let operand = map_device(operand);
        let p = if Path::new(operand).is_absolute() {
            std::path::PathBuf::from(operand)
        } else {
            base.join(operand)
        };
        let s = p.to_string_lossy().replace('/', "\\");
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
    pub fn file_meta(base: &Path, operand: &str, lstat: bool) -> Result<FileMeta, u32> {
        let extended = api_path(base, operand);
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
                return find_first_meta(base, &extended);
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
            CloseHandle(handle);
        }
        Ok(meta)
    }

    fn find_first_meta(base: &Path, extended: &str) -> Result<FileMeta, u32> {
        // FindFirstFileW on the \\?\ path's parent + name
        let full = api_path(base, &strip_extended(extended));
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
        base: &Path,
        operand: &str,
        ctime: Option<FileTime>,
        atime: Option<FileTime>,
        mtime: Option<FileTime>,
        is_dir_hint: bool,
        no_dereference: bool,
    ) -> Result<(), u32> {
        let extended = api_path(base, operand);
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
                return set_file_times(base, operand, ctime, atime, mtime, is_dir_hint, false);
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

    /// Create the file if missing and set both times (GNU touch default).
    pub fn create_file(base: &Path, operand: &str) -> Result<(), u32> {
        let extended = api_path(base, operand);
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

    pub fn hard_link(base: &Path, existing: &str, link: &str) -> Result<(), u32> {
        let e = wide(&api_path(base, existing));
        let l = wide(&api_path(base, link));
        let rc = unsafe { CreateHardLinkW(l.as_ptr(), e.as_ptr(), std::ptr::null_mut()) };
        if rc == 0 {
            Err(kernel32_last_error())
        } else {
            Ok(())
        }
    }

    pub fn create_symlink(base: &Path, target: &str, link: &str, dir: bool) -> Result<(), u32> {
        let t = wide(&api_path(base, target));
        let l = wide(&api_path(base, link));
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
    pub fn file_accounts(base: &Path, operand: &str) -> ((String, String), (String, String)) {
        let extended = api_path(base, operand);
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

    pub fn file_meta(_base: &Path, _operand: &str, _lstat: bool) -> Result<FileMeta, u32> {
        Err(2)
    }
    pub fn set_file_times(
        _base: &Path,
        _operand: &str,
        _ctime: Option<FileTime>,
        _atime: Option<FileTime>,
        _mtime: Option<FileTime>,
        _is_dir_hint: bool,
        _no_dereference: bool,
    ) -> Result<(), u32> {
        Err(1)
    }
    pub fn create_file(_base: &Path, _operand: &str) -> Result<(), u32> {
        Err(1)
    }
    pub fn hard_link(_base: &Path, _e: &str, _l: &str) -> Result<(), u32> {
        Err(1)
    }
    pub fn create_symlink(_base: &Path, _t: &str, _l: &str, _d: bool) -> Result<(), u32> {
        Err(1)
    }
    pub fn file_accounts(_base: &Path, _operand: &str) -> ((String, String), (String, String)) {
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
    pub fn api_path(base: &Path, operand: &str) -> String {
        if Path::new(operand).is_absolute() {
            operand.to_string()
        } else {
            base.join(operand).to_string_lossy().into_owned()
        }
    }
}

pub use imp::map_device;
pub use imp::*;

/// Win32 error -> GNU strerror-style reason string (the same mapping the
/// 1.x C++ applets use; 1314 is the documented no-Developer-Mode case).
pub fn win32_reason(err: u32) -> &'static str {
    match err {
        2 => "No such file or directory",    // ERROR_FILE_NOT_FOUND
        3 => "No such file or directory",    // ERROR_PATH_NOT_FOUND
        5 => "Permission denied",            // ERROR_ACCESS_DENIED
        80 => "File exists",                 // ERROR_FILE_EXISTS
        183 => "File exists",                // ERROR_ALREADY_EXISTS
        206 => "File name too long",         // ERROR_FILENAME_EXCED_RANGE
        2062 => "No such file or directory", // ERROR_CANT_ACCESS_FILE alt
        1314 => "Operation not permitted",   // ERROR_PRIVILEGE_NOT_HELD
        1 => "Operation not permitted",      // ERROR_INVALID_FUNCTION
        1920 => "No such file or directory", // ERROR_CANT_ACCESS_FILE
        _ => "Invalid argument",
    }
}
