//! Windows filesystem layer for `find` — hand-written kernel32/advapi32
//! FFI in the fs_win.rs / winsys.rs family (zero crates).
//!
//! * Directory reads use `FindFirstFileExW(FindExInfoBasic,
//!   FIND_FIRST_EX_LARGE_FETCH)`: one enumeration call yields name,
//!   attributes, size, creation/access/write times and the reparse tag, so
//!   the common predicates (`-name -type -size -mtime -newer`, `-printf
//!   %p %s %t`) never open the entry (GNU's d_type + lazy stat analog).
//! * Everything an enumeration record lacks (change time, link count,
//!   file index, volume serial, allocation size, the followed target of a
//!   link) comes from one handle open — `CreateFileW(FILE_READ_ATTRIBUTES,
//!   BACKUP_SEMANTICS [| OPEN_REPARSE_POINT])` +
//!   `GetFileInformationByHandle` + `GetFileInformationByHandleEx`
//!   (FileBasicInfo / FileStandardInfo / FileAttributeTagInfo) — fetched
//!   lazily and cached per entry by the caller.
//! * Owner/group come from `GetNamedSecurityInfoW`; SIDs are rendered with
//!   the shared 1.x id formula (src/utils/win32.cppm, fs_win.rs
//!   `sid_to_id`: 0x30000+RID for NT-domain accounts, 0x60000+RID for
//!   integrity labels, else the RID) so `-uid`/`%U` agree with rust2
//!   `id`/`stat`; account names are cached per SID by the caller.

use std::ffi::c_void;

pub type DWORD = u32;
type Handle = *mut c_void;

const INVALID_HANDLE_VALUE: Handle = -1isize as Handle;

pub const FILE_ATTRIBUTE_READONLY: DWORD = 0x1;
pub const FILE_ATTRIBUTE_SYSTEM: DWORD = 0x4;
pub const FILE_ATTRIBUTE_DIRECTORY: DWORD = 0x10;
pub const FILE_ATTRIBUTE_DEVICE: DWORD = 0x40;
pub const FILE_ATTRIBUTE_REPARSE_POINT: DWORD = 0x400;

pub const IO_REPARSE_TAG_MOUNT_POINT: DWORD = 0xA000_0003;
pub const IO_REPARSE_TAG_SYMLINK: DWORD = 0xA000_000C;

const FILE_READ_ATTRIBUTES: DWORD = 0x80;
const FILE_LIST_DIRECTORY: DWORD = 0x1;
const GENERIC_READ: DWORD = 0x8000_0000;
const GENERIC_WRITE: DWORD = 0x4000_0000;
const FILE_SHARE_ALL: DWORD = 0x7;
const OPEN_EXISTING: DWORD = 3;
const FILE_FLAG_BACKUP_SEMANTICS: DWORD = 0x0200_0000;
const FILE_FLAG_OPEN_REPARSE_POINT: DWORD = 0x0020_0000;
const FIND_EX_INFO_BASIC: i32 = 1;
const FIND_EX_SEARCH_NAME_MATCH: i32 = 0;
const FIND_FIRST_EX_LARGE_FETCH: DWORD = 2;
const ERROR_NO_MORE_FILES: DWORD = 18;
const ERROR_FILE_NOT_FOUND: DWORD = 2;

const FILE_BASIC_INFO_CLASS: i32 = 0;
const FILE_STANDARD_INFO_CLASS: i32 = 1;
const FILE_ATTRIBUTE_TAG_INFO_CLASS: i32 = 9;

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
struct FileStandardInfo {
    allocation: i64,
    end_of_file: i64,
    links: DWORD,
    delete_pending: u8,
    directory: u8,
}

#[repr(C)]
#[derive(Default)]
struct FileAttributeTagInfo {
    attrs: DWORD,
    tag: DWORD,
}

#[link(name = "kernel32")]
extern "system" {
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
    fn GetFileInformationByHandleEx(h: Handle, class: i32, info: *mut c_void, size: DWORD) -> i32;
    fn GetFileAttributesW(name: *const u16) -> DWORD;
    fn SetFileAttributesW(name: *const u16, attrs: DWORD) -> i32;
    fn GetLastError() -> DWORD;
    fn GetVolumePathNameW(name: *const u16, out: *mut u16, len: DWORD) -> i32;
    fn GetVolumeInformationW(
        root: *const u16,
        vol_name: *mut u16,
        vol_len: DWORD,
        serial: *mut DWORD,
        max_component: *mut DWORD,
        flags: *mut DWORD,
        fs_name: *mut u16,
        fs_len: DWORD,
    ) -> i32;
    fn RemoveDirectoryW(name: *const u16) -> i32;
    fn DeleteFileW(name: *const u16) -> i32;
    fn LocalFree(h: Handle) -> Handle;
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
        name: *mut u16,
        name_size: *mut DWORD,
        domain: *mut u16,
        domain_size: *mut DWORD,
        use_: *mut DWORD,
    ) -> i32;
    fn IsValidSid(sid: *mut c_void) -> i32;
    fn GetLengthSid(psid: *const u8) -> u32;
    fn GetSidSubAuthorityCount(sid: *mut c_void) -> *mut u8;
    fn GetSidSubAuthority(sid: *mut c_void, index: DWORD) -> *mut DWORD;
    fn GetSidIdentifierAuthority(sid: *mut c_void) -> *mut u8;
}

fn last_error() -> DWORD {
    unsafe { GetLastError() }
}

/// NUL-terminated copy of a wide path.
pub fn z(path: &[u16]) -> Vec<u16> {
    let mut v = Vec::with_capacity(path.len() + 1);
    v.extend_from_slice(path);
    v.push(0);
    v
}

/// One directory record (or the record of a single path).
#[derive(Clone, Debug, Default)]
pub struct FindData {
    pub attrs: DWORD,
    pub btime: i64,
    pub atime: i64,
    pub mtime: i64,
    pub size: u64,
    pub tag: DWORD,
    pub wname: Vec<u16>,
}

fn convert(d: &Win32FindDataW) -> FindData {
    let len = d.name.iter().position(|&c| c == 0).unwrap_or(d.name.len());
    FindData {
        attrs: d.attrs,
        btime: d.creation.ticks(),
        atime: d.access.ticks(),
        mtime: d.write.ticks(),
        size: (d.size_high as u64) << 32 | d.size_low as u64,
        tag: if d.attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0 {
            d.reserved0
        } else {
            0
        },
        wname: d.name[..len].to_vec(),
    }
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

/// Lazy directory enumeration (GNU readdir order analog: the volume's
/// native order — NTFS returns names in its case-insensitive B-tree
/// order, so output order differs from ext4 hash order; the gate compares
/// such rows sorted).
pub struct DirIter {
    handle: Handle,
    pending: Option<FindData>,
    done: bool,
}

impl DirIter {
    /// Open `dir` (a native path without trailing NUL) for enumeration.
    pub fn open(dir: &[u16]) -> Result<DirIter, DWORD> {
        let mut pat = Vec::with_capacity(dir.len() + 3);
        pat.extend_from_slice(dir);
        if pat.last() != Some(&(b'\\' as u16)) {
            pat.push(b'\\' as u16);
        }
        pat.push(b'*' as u16);
        pat.push(0);
        let mut data = empty_find_data();
        let h = unsafe {
            FindFirstFileExW(
                pat.as_ptr(),
                FIND_EX_INFO_BASIC,
                &mut data,
                FIND_EX_SEARCH_NAME_MATCH,
                std::ptr::null_mut(),
                FIND_FIRST_EX_LARGE_FETCH,
            )
        };
        if h == INVALID_HANDLE_VALUE {
            let e = last_error();
            if e == ERROR_FILE_NOT_FOUND {
                // an empty volume root has not even "." entries
                return Ok(DirIter {
                    handle: INVALID_HANDLE_VALUE,
                    pending: None,
                    done: true,
                });
            }
            return Err(e);
        }
        Ok(DirIter {
            handle: h,
            pending: Some(convert(&data)),
            done: false,
        })
    }

    fn raw_next(&mut self) -> Option<Result<FindData, DWORD>> {
        if let Some(p) = self.pending.take() {
            return Some(Ok(p));
        }
        if self.done {
            return None;
        }
        let mut data = empty_find_data();
        if unsafe { FindNextFileW(self.handle, &mut data) } == 0 {
            self.done = true;
            let e = last_error();
            if e == ERROR_NO_MORE_FILES {
                return None;
            }
            return Some(Err(e));
        }
        Some(Ok(convert(&data)))
    }
}

impl Iterator for DirIter {
    type Item = Result<FindData, DWORD>;
    fn next(&mut self) -> Option<Self::Item> {
        loop {
            match self.raw_next()? {
                Ok(d) => {
                    let n = &d.wname;
                    let dot = b'.' as u16;
                    if (n.len() == 1 && n[0] == dot) || (n.len() == 2 && n[0] == dot && n[1] == dot)
                    {
                        continue;
                    }
                    return Some(Ok(d));
                }
                Err(e) => return Some(Err(e)),
            }
        }
    }
}

impl Drop for DirIter {
    fn drop(&mut self) {
        if self.handle != INVALID_HANDLE_VALUE {
            unsafe { FindClose(self.handle) };
        }
    }
}

/// The enumeration record of one path itself (lstat-level, works for
/// dangling links); `None` for paths FindFirstFile cannot name (volume
/// roots, devices).
pub fn find_one(path: &[u16]) -> Result<FindData, DWORD> {
    let p = z(path);
    let mut data = empty_find_data();
    let h = unsafe {
        FindFirstFileExW(
            p.as_ptr(),
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
    Ok(convert(&data))
}

/// Everything one handle open reports.
#[derive(Clone, Debug, Default)]
pub struct HandleInfo {
    pub attrs: DWORD,
    pub tag: DWORD,
    pub size: u64,
    pub nlink: u64,
    pub ino: u64,
    pub dev: u32,
    pub btime: i64,
    pub atime: i64,
    pub mtime: i64,
    pub ctime: i64,
    pub alloc: u64,
}

/// Open `path` and snapshot it; `follow` resolves links (stat), else the
/// link itself (lstat).
pub fn handle_info(path: &[u16], follow: bool) -> Result<HandleInfo, DWORD> {
    let p = z(path);
    let mut flags = FILE_FLAG_BACKUP_SEMANTICS;
    if !follow {
        flags |= FILE_FLAG_OPEN_REPARSE_POINT;
    }
    let h = unsafe {
        CreateFileW(
            p.as_ptr(),
            FILE_READ_ATTRIBUTES,
            FILE_SHARE_ALL,
            std::ptr::null_mut(),
            OPEN_EXISTING,
            flags,
            std::ptr::null_mut(),
        )
    };
    if h == INVALID_HANDLE_VALUE {
        return Err(last_error());
    }
    let mut out = HandleInfo::default();
    unsafe {
        let mut info = ByHandleFileInformation::default();
        if GetFileInformationByHandle(h, &mut info) == 0 {
            let e = last_error();
            CloseHandle(h);
            return Err(e);
        }
        out.attrs = info.attrs;
        out.size = (info.size_high as u64) << 32 | info.size_low as u64;
        out.nlink = info.links.max(1) as u64;
        out.ino = (info.index_high as u64) << 32 | info.index_low as u64;
        out.dev = info.volume_serial;
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
        let mut std_info = FileStandardInfo::default();
        if GetFileInformationByHandleEx(
            h,
            FILE_STANDARD_INFO_CLASS,
            &mut std_info as *mut _ as *mut c_void,
            std::mem::size_of::<FileStandardInfo>() as DWORD,
        ) != 0
        {
            out.alloc = std_info.allocation.max(0) as u64;
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
        }
        CloseHandle(h);
    }
    Ok(out)
}

/// `GetFileAttributesW` (no open, no follow).
pub fn attributes(path: &[u16]) -> Option<DWORD> {
    let p = z(path);
    let a = unsafe { GetFileAttributesW(p.as_ptr()) };
    if a == 0xFFFF_FFFF {
        None
    } else {
        Some(a)
    }
}

/// Is `tag` a name-surrogate reparse point (symlink, junction, WSL/LX
/// symlink) — i.e. a POSIX symbolic link analog? App-exec aliases and
/// cloud placeholders are not.
pub fn is_link_tag(tag: DWORD) -> bool {
    tag == IO_REPARSE_TAG_SYMLINK
        || tag == IO_REPARSE_TAG_MOUNT_POINT
        || (tag & 0x2000_0000 != 0 && tag & 0x8000_0000 != 0)
}

/// Can the current token open `path` with `access`? (C++ win32_accessible
/// model: the read-only attribute refuses write first.)
fn can_open(path: &[u16], access: DWORD, dir: bool) -> bool {
    let p = z(path);
    let flags = if dir {
        FILE_FLAG_BACKUP_SEMANTICS
    } else {
        0x80
    };
    let h = unsafe {
        CreateFileW(
            p.as_ptr(),
            access,
            FILE_SHARE_ALL,
            std::ptr::null_mut(),
            OPEN_EXISTING,
            flags,
            std::ptr::null_mut(),
        )
    };
    if h == INVALID_HANDLE_VALUE {
        return false;
    }
    unsafe { CloseHandle(h) };
    true
}

/// `-readable`.
pub fn readable(path: &[u16], dir: bool) -> bool {
    can_open(
        path,
        if dir {
            FILE_LIST_DIRECTORY
        } else {
            GENERIC_READ
        },
        dir,
    )
}

/// `-writable`.
pub fn writable(path: &[u16], dir: bool, attrs: DWORD) -> bool {
    if !dir && attrs & FILE_ATTRIBUTE_READONLY != 0 {
        return false;
    }
    can_open(path, GENERIC_WRITE, dir)
}

/// The filesystem type name of the volume holding `path` ("NTFS", ...).
pub fn fs_type(path: &[u16]) -> String {
    let p = z(path);
    let mut root = [0u16; 1024];
    unsafe {
        if GetVolumePathNameW(p.as_ptr(), root.as_mut_ptr(), root.len() as DWORD) == 0 {
            return String::new();
        }
        let mut fs = [0u16; 64];
        let (mut serial, mut maxc, mut flags) = (0u32, 0u32, 0u32);
        if GetVolumeInformationW(
            root.as_ptr(),
            std::ptr::null_mut(),
            0,
            &mut serial,
            &mut maxc,
            &mut flags,
            fs.as_mut_ptr(),
            fs.len() as DWORD,
        ) == 0
        {
            return String::new();
        }
        let n = fs.iter().position(|&c| c == 0).unwrap_or(0);
        String::from_utf16_lossy(&fs[..n])
    }
}

/// Owner and group SIDs (raw bytes) of `path`.
pub fn owner_group(path: &[u16]) -> Option<(Vec<u8>, Vec<u8>)> {
    let p = z(path);
    let mut owner: *mut c_void = std::ptr::null_mut();
    let mut group: *mut c_void = std::ptr::null_mut();
    let mut sd: *mut c_void = std::ptr::null_mut();
    let status = unsafe {
        GetNamedSecurityInfoW(
            p.as_ptr(),
            1, // SE_FILE_OBJECT
            0x1 | 0x2,
            &mut owner,
            &mut group,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            &mut sd,
        )
    };
    if status != 0 {
        if !sd.is_null() {
            unsafe { LocalFree(sd) };
        }
        return None;
    }
    let copy = |sid: *mut c_void| -> Vec<u8> {
        if sid.is_null() || unsafe { IsValidSid(sid) } == 0 {
            return Vec::new();
        }
        let len = unsafe { GetLengthSid(sid as *const u8) } as usize;
        unsafe { std::slice::from_raw_parts(sid as *const u8, len) }.to_vec()
    };
    let out = (copy(owner), copy(group));
    if !sd.is_null() {
        unsafe { LocalFree(sd) };
    }
    Some(out)
}

/// (numeric id, account name) of a SID — the shared 1.x id formula.
pub fn sid_account(sid: &[u8]) -> (u64, Option<String>) {
    if sid.is_empty() {
        return (0, None);
    }
    let mut buf = sid.to_vec();
    let psid = buf.as_mut_ptr() as *mut c_void;
    let id = unsafe {
        let count = *GetSidSubAuthorityCount(psid);
        if count == 0 {
            0
        } else {
            let rid = *GetSidSubAuthority(psid, (count - 1) as DWORD) as u64;
            let auth = std::slice::from_raw_parts(GetSidIdentifierAuthority(psid), 6);
            let authority = auth.iter().fold(0u64, |a, &b| a << 8 | b as u64);
            let first = *GetSidSubAuthority(psid, 0);
            if authority == 5 && first == 21 && count >= 5 {
                0x30000 + rid
            } else if authority == 16 {
                0x60000 + rid
            } else {
                rid
            }
        }
    };
    let name = unsafe {
        let mut name = [0u16; 256];
        let mut domain = [0u16; 256];
        let mut nlen = name.len() as DWORD;
        let mut dlen = domain.len() as DWORD;
        let mut use_ = 0u32;
        if LookupAccountSidW(
            std::ptr::null(),
            psid,
            name.as_mut_ptr(),
            &mut nlen,
            domain.as_mut_ptr(),
            &mut dlen,
            &mut use_,
        ) == 0
        {
            None
        } else {
            Some(String::from_utf16_lossy(&name[..nlen as usize]))
        }
    };
    (id, name)
}

/// `-delete`: unlink a file / remove an empty directory. GNU unlink(2)
/// ignores the file's own mode, so a read-only attribute is cleared and
/// the call retried. Returns the Win32 error on failure.
pub fn delete(path: &[u16], is_dir: bool) -> Result<(), DWORD> {
    let p = z(path);
    let attempt = || unsafe {
        if is_dir {
            RemoveDirectoryW(p.as_ptr()) != 0
        } else {
            DeleteFileW(p.as_ptr()) != 0
        }
    };
    if attempt() {
        return Ok(());
    }
    let e = last_error();
    if e == 5 {
        if let Some(a) = attributes(path) {
            if a & FILE_ATTRIBUTE_READONLY != 0 {
                unsafe { SetFileAttributesW(p.as_ptr(), a & !FILE_ATTRIBUTE_READONLY) };
                if attempt() {
                    return Ok(());
                }
                let e2 = last_error();
                unsafe { SetFileAttributesW(p.as_ptr(), a) };
                return Err(e2);
            }
        }
    }
    Err(e)
}

/// Win32 error -> GNU strerror text (C locale).
pub fn reason(err: DWORD) -> &'static str {
    match err {
        2 | 3 | 1920 | 2062 | 161 | 123 => "No such file or directory",
        5 | 32 | 33 => "Permission denied",
        145 => "Directory not empty",
        267 => "Not a directory",
        206 => "File name too long",
        1921 => "Too many levels of symbolic links",
        _ => crate::fs_win::win32_reason(err),
    }
}
