//! Win32 surface for `tail` follow mode (#1488): handle/path identity,
//! the `--pid` liveness handle, and the directory change notifications
//! that wake the follow loop.
//!
//! Every handle here is the command's OWN, created from explicit
//! arguments (operand paths, the `--pid` value): opened files, directory
//! change notifications, the opened process. The process stdin handle is
//! never touched — its class and follow target arrive injected
//! (`tail::StdinMeta`; the gate driver composes them from the process
//! side, which is the only sanctioned process-IO tier).
//!
//! Zero crates — raw kernel32 declarations in the fs_win.rs/winsys.rs
//! style. Non-Windows builds get std-only fallbacks so the workspace still
//! compiles (pure polling, no pid tracking).

use std::fs::File;
use std::path::Path;
use std::time::Duration;

/// What `fstat` would say about an open file (or a path), reduced to the
/// fields the follow loop compares.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Stat {
    pub size: u64,
    pub mtime: u64,
    /// (volume serial, file index): GNU's (st_dev, st_ino)
    pub id: (u32, u64),
    pub links: u32,
    pub is_dir: bool,
    /// a regular disk file (S_ISREG); false for char devices like NUL
    pub regular: bool,
}

/// GNU's fstat(STDIN_FILENO) classes, as far as Windows can tell them.
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum StdinKind {
    /// redirected regular file (S_ISREG): followed by descriptor
    Disk,
    /// pipe / FIFO: `-f` is ignored for it (POSIX, tail.c
    /// ignore_fifo_and_pipe)
    Pipe,
    /// an interactive console (isatty)
    Console,
    /// other character device (NUL): tailable, never yields data
    CharDev,
    /// no usable handle (unit tests inject this): treated like a pipe
    Unknown,
}

#[cfg(windows)]
mod imp {
    use super::Stat;
    use std::ffi::c_void;
    use std::fs::File;
    use std::os::windows::ffi::OsStrExt;
    use std::os::windows::io::AsRawHandle;
    use std::path::{Path, PathBuf};

    type Handle = *mut c_void;
    const INVALID_HANDLE_VALUE: Handle = -1isize as Handle;

    #[repr(C)]
    #[derive(Default)]
    struct ByHandleInfo {
        attributes: u32,
        // FILETIMEs are two DWORDs (4-byte aligned): a u64 here would pad
        creation: [u32; 2],
        access: [u32; 2],
        write: [u32; 2],
        volume_serial: u32,
        size_high: u32,
        size_low: u32,
        links: u32,
        index_high: u32,
        index_low: u32,
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn GetFileInformationByHandle(h: Handle, info: *mut c_void) -> i32;
        fn GetFileType(h: Handle) -> u32;
        fn CreateFileW(
            name: *const u16,
            access: u32,
            share: u32,
            security: *mut c_void,
            disposition: u32,
            flags: u32,
            template: Handle,
        ) -> Handle;
        fn CloseHandle(h: Handle) -> i32;
        fn OpenProcess(access: u32, inherit: i32, pid: u32) -> Handle;
        fn WaitForSingleObject(h: Handle, ms: u32) -> u32;
        fn WaitForMultipleObjects(n: u32, hs: *const Handle, all: i32, ms: u32) -> u32;
        fn FindFirstChangeNotificationW(dir: *const u16, subtree: i32, filter: u32) -> Handle;
        fn FindNextChangeNotification(h: Handle) -> i32;
        fn FindCloseChangeNotification(h: Handle) -> i32;
        fn GetLastError() -> u32;
        fn OpenFileById(
            hint: Handle,
            id: *const FileIdDescriptor,
            access: u32,
            share: u32,
            security: *mut c_void,
            flags: u32,
        ) -> Handle;
        fn Sleep(ms: u32);
    }

    const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;
    const FILE_TYPE_DISK: u32 = 1;
    const FILE_TYPE_CHAR: u32 = 2;
    const FILE_TYPE_PIPE: u32 = 3;
    const FILE_READ_ATTRIBUTES: u32 = 0x80;
    const SHARE_ALL: u32 = 0x1 | 0x2 | 0x4;
    const OPEN_EXISTING: u32 = 3;
    const FILE_FLAG_BACKUP_SEMANTICS: u32 = 0x0200_0000;
    const SYNCHRONIZE: u32 = 0x0010_0000;
    const WAIT_OBJECT_0: u32 = 0;
    const WAIT_TIMEOUT: u32 = 0x102;
    const ERROR_ACCESS_DENIED: u32 = 5;
    const INFINITE: u32 = 0xFFFF_FFFF;
    /// FILE_NAME | DIR_NAME | ATTRIBUTES | SIZE | LAST_WRITE
    const NOTIFY_FILTER: u32 = 0x1 | 0x2 | 0x4 | 0x8 | 0x10;
    const MAX_WAIT: usize = 64;

    fn wide(p: &Path) -> Vec<u16> {
        p.as_os_str().encode_wide().chain(Some(0)).collect()
    }

    fn info_of(h: Handle) -> std::io::Result<Stat> {
        let mut i = ByHandleInfo::default();
        if unsafe { GetFileInformationByHandle(h, &mut i as *mut ByHandleInfo as *mut c_void) } == 0
        {
            return Err(std::io::Error::last_os_error());
        }
        let regular = unsafe { GetFileType(h) } == FILE_TYPE_DISK;
        let is_dir = i.attributes & FILE_ATTRIBUTE_DIRECTORY != 0;
        Ok(Stat {
            size: ((i.size_high as u64) << 32) | i.size_low as u64,
            mtime: ((i.write[1] as u64) << 32) | i.write[0] as u64,
            id: (
                i.volume_serial,
                ((i.index_high as u64) << 32) | i.index_low as u64,
            ),
            links: i.links,
            is_dir,
            regular: regular && !is_dir,
        })
    }

    pub fn handle_stat(f: &File) -> std::io::Result<Stat> {
        match info_of(f.as_raw_handle() as Handle) {
            Ok(s) => Ok(s),
            // char devices (NUL) have no by-handle information
            Err(_) if unsafe { GetFileType(f.as_raw_handle() as Handle) } == FILE_TYPE_CHAR => {
                Ok(Stat::default())
            }
            Err(e) => Err(e),
        }
    }

    pub fn path_stat(p: &Path) -> std::io::Result<Stat> {
        let w = wide(p);
        let h = unsafe {
            CreateFileW(
                w.as_ptr(),
                FILE_READ_ATTRIBUTES,
                SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                std::ptr::null_mut(),
            )
        };
        if h == INVALID_HANDLE_VALUE {
            return Err(std::io::Error::last_os_error());
        }
        let r = info_of(h);
        unsafe { CloseHandle(h) };
        r
    }

    #[repr(C)]
    struct FileIdDescriptor {
        size: u32,
        kind: u32, // FileIdType
        // the FILE_ID_128-sized union; FileId (LARGE_INTEGER) is the low half
        id: [u64; 2],
    }

    /// Whether the file with this (volume, index) identity still exists
    /// on the volume holding `dir` (GNU's "the inode is still alive":
    /// renamed away, or deleted but still held open by a writer).
    pub fn exists_by_id(dir: &Path, id: (u32, u64)) -> bool {
        let w = wide(dir);
        let hint = unsafe {
            CreateFileW(
                w.as_ptr(),
                FILE_READ_ATTRIBUTES,
                SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                FILE_FLAG_BACKUP_SEMANTICS,
                std::ptr::null_mut(),
            )
        };
        if hint == INVALID_HANDLE_VALUE {
            return false;
        }
        let same_volume = info_of(hint).is_ok_and(|st| st.id.0 == id.0);
        let desc = FileIdDescriptor {
            size: std::mem::size_of::<FileIdDescriptor>() as u32,
            kind: 0,
            id: [id.1, 0],
        };
        let h = if same_volume {
            unsafe {
                OpenFileById(
                    hint,
                    &desc,
                    FILE_READ_ATTRIBUTES,
                    SHARE_ALL,
                    std::ptr::null_mut(),
                    FILE_FLAG_BACKUP_SEMANTICS,
                )
            }
        } else {
            INVALID_HANDLE_VALUE
        };
        unsafe { CloseHandle(hint) };
        if h == INVALID_HANDLE_VALUE {
            return false;
        }
        unsafe { CloseHandle(h) };
        true
    }

    /// `--pid` liveness: a SYNCHRONIZE handle when the process can be
    /// opened (signaled == dead). GNU's kill(pid, 0) treats EPERM as
    /// alive; ERROR_ACCESS_DENIED is that case here and is re-probed
    /// every interval.
    pub struct Pid {
        pid: u32,
        handle: Handle,
    }

    impl Pid {
        pub fn new(pid: u32) -> Pid {
            let handle = unsafe { OpenProcess(SYNCHRONIZE, 0, pid) };
            Pid { pid, handle }
        }

        pub fn dead(&mut self) -> bool {
            if !self.handle.is_null() {
                return unsafe { WaitForSingleObject(self.handle, 0) } == WAIT_OBJECT_0;
            }
            let h = unsafe { OpenProcess(SYNCHRONIZE, 0, self.pid) };
            if !h.is_null() {
                self.handle = h;
                return unsafe { WaitForSingleObject(h, 0) } == WAIT_OBJECT_0;
            }
            (unsafe { GetLastError() }) != ERROR_ACCESS_DENIED
        }
    }

    impl Drop for Pid {
        fn drop(&mut self) {
            if !self.handle.is_null() {
                unsafe { CloseHandle(self.handle) };
            }
        }
    }

    /// Change notifications on the followed files' parent directories plus
    /// the --pid process handle: one WaitForMultipleObjects with the sleep
    /// interval as the timeout. A notification only shortens the wait —
    /// every wake re-stats every file, so missed or coalesced events (and
    /// network shares that never signal) degrade to interval polling.
    pub struct Waker {
        dirs: Vec<Handle>,
    }

    impl Waker {
        pub fn new(dirs: &[PathBuf]) -> Waker {
            let mut hs = Vec::new();
            for d in dirs.iter().take(MAX_WAIT - 1) {
                let w = wide(d);
                let h = unsafe { FindFirstChangeNotificationW(w.as_ptr(), 0, NOTIFY_FILTER) };
                if h != INVALID_HANDLE_VALUE && !h.is_null() {
                    hs.push(h);
                }
            }
            Waker { dirs: hs }
        }

        /// Block until a watched directory changes, the pid exits, or
        /// `timeout` elapses (`None` = no timeout).
        pub fn wait(&mut self, timeout: Option<std::time::Duration>, pid: Option<&Pid>) {
            let ms = match timeout {
                None => INFINITE,
                Some(d) => {
                    let ms = d.as_secs_f64() * 1000.0;
                    if !ms.is_finite() || ms >= (INFINITE - 1) as f64 {
                        INFINITE
                    } else {
                        // ceil, and never 0: a zero timeout would spin
                        (ms.ceil() as u32).max(1)
                    }
                }
            };
            let mut set: Vec<Handle> = self.dirs.clone();
            if let Some(p) = pid {
                if !p.handle.is_null() {
                    set.push(p.handle);
                }
            }
            if set.is_empty() {
                if ms == INFINITE {
                    // nothing can wake us and no timeout: GNU sleeps
                    // forever here too (-s inf); sleep in long slices
                    unsafe { Sleep(INFINITE - 1) };
                } else {
                    unsafe { Sleep(ms) };
                }
                return;
            }
            let r = unsafe { WaitForMultipleObjects(set.len() as u32, set.as_ptr(), 0, ms) };
            if r == WAIT_TIMEOUT {
                return;
            }
            // re-arm every signaled directory handle
            for &h in &self.dirs {
                if unsafe { WaitForSingleObject(h, 0) } == WAIT_OBJECT_0 {
                    unsafe { FindNextChangeNotification(h) };
                }
            }
        }
    }

    impl Drop for Waker {
        fn drop(&mut self) {
            for &h in &self.dirs {
                unsafe { FindCloseChangeNotification(h) };
            }
        }
    }
}

#[cfg(not(windows))]
mod imp {
    use super::Stat;
    use std::fs::File;
    use std::path::{Path, PathBuf};

    fn from_meta(m: std::fs::Metadata) -> Stat {
        use std::time::UNIX_EPOCH;
        Stat {
            size: m.len(),
            mtime: m
                .modified()
                .ok()
                .and_then(|t| t.duration_since(UNIX_EPOCH).ok())
                .map(|d| d.as_nanos() as u64)
                .unwrap_or(0),
            id: (0, 0),
            links: 1,
            is_dir: m.is_dir(),
            regular: m.is_file(),
        }
    }

    pub fn handle_stat(f: &File) -> std::io::Result<Stat> {
        f.metadata().map(from_meta)
    }

    pub fn path_stat(p: &Path) -> std::io::Result<Stat> {
        std::fs::metadata(p).map(from_meta)
    }

    pub fn exists_by_id(_dir: &Path, _id: (u32, u64)) -> bool {
        false
    }

    pub struct Pid {
        pid: u32,
    }

    impl Pid {
        pub fn new(pid: u32) -> Pid {
            Pid { pid }
        }
        /// GNU `kill(pid, 0)`: the process is alive when the probe
        /// succeeds or fails with EPERM (exists but unsignalable);
        /// any other failure means it is dead. This is the same rule
        /// the Windows arm documents (`ERROR_ACCESS_DENIED` is its
        /// EPERM), so `--pid` terminates the follow loop on every
        /// platform once the writer is gone. `--pid` is capped at
        /// i32::MAX by the argument parser, so the cast cannot wrap.
        /// Raw declaration, zero crates, in the `isatty` style of
        /// `test_cmd.rs` (no libc dependency).
        pub fn dead(&mut self) -> bool {
            #[cfg(unix)]
            {
                extern "C" {
                    fn kill(pid: i32, sig: i32) -> i32;
                }
                /// Operation not permitted (POSIX; 1 on Linux/macOS/BSD).
                const EPERM: i32 = 1;
                if unsafe { kill(self.pid as i32, 0) } == 0 {
                    return false;
                }
                std::io::Error::last_os_error().raw_os_error() != Some(EPERM)
            }
            #[cfg(not(unix))]
            {
                let _ = self.pid;
                false
            }
        }
    }

    pub struct Waker;

    impl Waker {
        pub fn new(_dirs: &[PathBuf]) -> Waker {
            Waker
        }
        pub fn wait(&mut self, timeout: Option<std::time::Duration>, _pid: Option<&Pid>) {
            std::thread::sleep(timeout.unwrap_or(std::time::Duration::from_secs(3600)));
        }
    }
}

pub use imp::{Pid, Waker};

pub fn handle_stat(f: &File) -> std::io::Result<Stat> {
    imp::handle_stat(f)
}

pub fn path_stat(p: &Path) -> std::io::Result<Stat> {
    imp::path_stat(p)
}

pub fn exists_by_id(dir: &Path, id: (u32, u64)) -> bool {
    imp::exists_by_id(dir, id)
}

/// Sleep-interval → wait timeout (`None` for an infinite interval).
pub fn interval(secs: f64) -> Option<Duration> {
    if secs.is_finite() && secs < 1e9 {
        Some(Duration::from_secs_f64(secs))
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn pid_dead_for_nonexistent_pid() {
        // i32::MAX can never be a live pid (the --pid parser caps
        // there, and the follow test uses the same value as DEAD):
        // the follow loop's only exit once the writer is gone.
        // Before the fix the non-Windows stub returned false here
        // and `follow_with_dead_writer_terminates_like_gnu` hung.
        assert!(Pid::new(2_147_483_647).dead());
    }

    #[test]
    fn pid_alive_for_own_pid() {
        assert!(!Pid::new(std::process::id()).dead());
    }
}
