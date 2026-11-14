//! `du` — GNU coreutils du.c (9.4) semantics, probed byte-for-byte against
//! WSL coreutils 9.4 (rust2/target/probe/w5-du.sh), with the 1.x C++
//! applet's Windows allocation model (du.cpp get_file_size:
//! GetCompressedFileSizeW rounded up to the volume cluster).
//!
//! Probed shapes: output rows are `SIZE\tPATH` in post-order (a directory
//! line follows its subtree, Savannah #13956 — subdirectories print
//! before the operand), paths spelled as the user gave them with '/'
//! joins; `-b` = `--apparent-size --block-size=1`; `-h` scales 1024-based
//! with one decimal ("4.0K"); `-B1` prints raw bytes; `-c` appends a
//! grand `total` row; `-s` = `-d 0`; `-d1` shows one level; `--inodes`
//! counts entries (1 per file, 1 per directory — GNU d1 subtree=4);
//! `--exclude` matches path components; missing operands report
//! `du: cannot access 'X': No such file or directory` rc 1 and the run
//! continues; with no operand the traversal root is `.` (probed rows
//! print `./d1/d2`).
//!
//! Honest boundary (documented, matches the 1.x applet): a directory's
//! own allocation reads 0 on NTFS (ext4 charges it a block — GNU shows 4
//! per directory), so any row whose value includes a directory's own
//! allocation diverges from the GNU oracle; those rows are win-mode in
//! the gate. File rows agree byte-for-byte with GNU on small regular
//! files (both allocate one 4K unit; the NTFS resident-file shortcut
//! reads the logical size, which rounds to the same cluster).
//!
//! --time prints `SIZE\tYYYY-MM-DD HH:MM\tPATH` in the effective TZ
//! (date.rs POSIX-TZ engine; gate rows pin TZ=UTC and fix mtimes with
//! touch -t).

use std::collections::HashSet;
use std::ffi::OsString;
use std::path::{Path, PathBuf};

use crate::argopt::{self, OptSpec};
use crate::date::{effective_zone, strftime};
use crate::fsutil::resolve;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('0'),
        long: "null",
        takes_value: false,
    },
    OptSpec {
        short: Some('a'),
        long: "all",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "apparent-size",
        takes_value: false,
    },
    OptSpec {
        short: Some('B'),
        long: "block-size",
        takes_value: true,
    },
    OptSpec {
        short: Some('b'),
        long: "bytes",
        takes_value: false,
    },
    OptSpec {
        short: Some('c'),
        long: "total",
        takes_value: false,
    },
    OptSpec {
        short: Some('d'),
        long: "max-depth",
        takes_value: true,
    },
    OptSpec {
        short: Some('H'),
        long: "dereference-command-line",
        takes_value: false,
    },
    OptSpec {
        short: Some('h'),
        long: "human-readable",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "inodes",
        takes_value: false,
    },
    OptSpec {
        short: Some('k'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('l'),
        long: "count-links",
        takes_value: false,
    },
    OptSpec {
        short: Some('L'),
        long: "dereference",
        takes_value: false,
    },
    OptSpec {
        short: Some('m'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('P'),
        long: "no-dereference",
        takes_value: false,
    },
    OptSpec {
        short: Some('S'),
        long: "separate-dirs",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "si",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "summarize",
        takes_value: false,
    },
    OptSpec {
        short: Some('t'),
        long: "threshold",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "time",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "time-style",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "files0-from",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "exclude",
        takes_value: true,
    },
    OptSpec {
        short: Some('X'),
        long: "exclude-from",
        takes_value: true,
    },
    OptSpec {
        short: Some('x'),
        long: "one-file-system",
        takes_value: false,
    },
];

// `--help` body verbatim from `env du --help` under WSL coreutils 9.4.
const DU_HELP_BODY: &str = r#"Usage: du [OPTION]... [FILE]...
  or:  du [OPTION]... --files0-from=F
Summarize device usage of the set of FILEs, recursively for directories.

Mandatory arguments to long options are mandatory for short options too.
  -0, --null            end each output line with NUL, not newline
  -a, --all             write counts for all files, not just directories
      --apparent-size   print apparent sizes rather than device usage; although
                          the apparent size is usually smaller, it may be
                          larger due to holes in ('sparse') files, internal
                          fragmentation, indirect blocks, and the like
  -B, --block-size=SIZE  scale sizes by SIZE before printing them; e.g.,
                           '-BM' prints sizes in units of 1,048,576 bytes;
                           see SIZE format below
  -b, --bytes           equivalent to '--apparent-size --block-size=1'
  -c, --total           produce a grand total
  -D, --dereference-args  dereference only symlinks that are listed on the
                          command line
  -d, --max-depth=N     print the total for a directory (or file, with --all)
                          only if it is N or fewer levels below the command
                          line argument;  --max-depth=0 is the same as
                          --summarize
      --files0-from=F   summarize device usage of the
                          NUL-terminated file names specified in file F;
                          if F is -, then read names from standard input
  -H                    equivalent to --dereference-args (-D)
  -h, --human-readable  print sizes in human readable format (e.g., 1K 234M 2G)
      --inodes          list inode usage information instead of block usage
  -k                    like --block-size=1K
  -L, --dereference     dereference all symbolic links
  -l, --count-links     count sizes many times if hard linked
  -m                    like --block-size=1M
  -P, --no-dereference  don't follow any symbolic links (this is the default)
  -S, --separate-dirs   for directories do not include size of subdirectories
      --si              like -h, but use powers of 1000 not 1024
  -s, --summarize       display only a total for each argument
  -t, --threshold=SIZE  exclude entries smaller than SIZE if positive,
                          or entries greater than SIZE if negative
      --time            show time of the last modification of any file in the
                          directory, or any of its subdirectories
      --time=WORD       show time as WORD instead of modification time:
                          atime, access, use, ctime or status
      --time-style=STYLE  show times using STYLE, which can be:
                            full-iso, long-iso, iso, or +FORMAT;
                            FORMAT is interpreted like in 'date'
  -X, --exclude-from=FILE  exclude files that match any pattern in FILE
      --exclude=PATTERN    exclude files that match PATTERN
  -x, --one-file-system    skip directories on different file systems
      --help        display this help and exit
      --version     output version information and exit

Display values are in units of the first available SIZE from --block-size,
and the DU_BLOCK_SIZE, BLOCK_SIZE and BLOCKSIZE environment variables.
Otherwise, units default to 1024 bytes (or 512 if POSIXLY_CORRECT is set).

The SIZE argument is an integer and optional unit (example: 10K is 10*1024).
Units are K,M,G,T,P,E,Z,Y,R,Q (powers of 1024) or KB,MB,... (powers of 1000).
Binary prefixes can be used, too: KiB=K, MiB=M, and so on.

"#;

// ---------------------------------------------------------------------------
// Windows size probes (du.cpp get_file_size's model)
// ---------------------------------------------------------------------------

#[cfg(windows)]
mod imp {
    use std::path::Path;

    fn wide(s: &str) -> Vec<u16> {
        use std::os::windows::ffi::OsStrExt;
        std::ffi::OsStr::new(s)
            .encode_wide()
            .chain(Some(0))
            .collect()
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn GetCompressedFileSizeW(name: *const u16, high: *mut u32) -> u32;
        fn GetDiskFreeSpaceW(
            root: *const u16,
            sectors: *mut u32,
            bytes: *mut u32,
            free: *mut u32,
            total: *mut u32,
        ) -> i32;
        fn GetFileAttributesExW(name: *const u16, level: u32, data: *mut u8) -> i32;
        fn GetLastError() -> u32;
    }

    const INVALID_FILE_SIZE: u32 = 0xFFFF_FFFF;
    const GET_FILEEX_INFO_STANDARD: u32 = 0;

    /// WIN32_FILE_ATTRIBUTE_DATA layout (64-byte blob)
    pub struct FileData {
        pub attrs: u32,
        pub creation: u64,
        pub access: u64,
        pub write: u64,
        pub size: u64,
    }

    pub fn attributes_data(path: &str) -> Option<FileData> {
        let mut buf = [0u8; 64];
        let rc = unsafe {
            GetFileAttributesExW(
                wide(path).as_ptr(),
                GET_FILEEX_INFO_STANDARD,
                buf.as_mut_ptr(),
            )
        };
        if rc == 0 {
            return None;
        }
        let u32_at = |off: usize| u32::from_le_bytes(buf[off..off + 4].try_into().unwrap());
        let u64_at = |off: usize| u64::from_le_bytes(buf[off..off + 8].try_into().unwrap());
        Some(FileData {
            attrs: u32_at(0),
            creation: u64_at(4),
            access: u64_at(12),
            write: u64_at(20),
            // nFileSizeHigh at 28, nFileSizeLow at 32 (two DWORDs, not a u64)
            size: u64::from(u32_at(32)) | (u64::from(u32_at(28)) << 32),
        })
    }

    /// du.cpp get_file_size: on-disk allocation via GetCompressedFileSizeW
    /// rounded up to the volume cluster; falls back to the logical size.
    pub fn allocated(path: &Path) -> u64 {
        let text = path.to_string_lossy().into_owned();
        let mut high: u32 = 0;
        let low = unsafe { GetCompressedFileSizeW(wide(&text).as_ptr(), &mut high) };
        let physical = if low == INVALID_FILE_SIZE && unsafe { GetLastError() } != 0 {
            None
        } else {
            Some((u64::from(high) << 32) | u64::from(low))
        };
        let logical = match attributes_data(&text) {
            Some(d) => d.size,
            None => return physical.unwrap_or(0),
        };
        if let Some(phys) = physical {
            if let Some(cluster) = cluster_size(&text) {
                if cluster > 0 {
                    return phys.div_ceil(cluster) * cluster;
                }
            }
            return phys;
        }
        logical
    }

    /// Volume cluster bytes for the path's drive (du.cpp
    /// get_volume_cluster_size); 4096 when the query fails.
    pub fn cluster_size(path: &str) -> Option<u64> {
        let root: String = if let Some(rest) = path.strip_prefix(r"\\?\") {
            if let Some(end) = rest.find(['\\', '/']) {
                if end >= 2 && rest[1..end].ends_with(':') {
                    format!("{}\\", &rest[..end])
                } else {
                    return Some(4096);
                }
            } else {
                return Some(4096);
            }
        } else if path.len() >= 3 && path.as_bytes()[1] == b':' {
            format!("{}\\", &path[..2])
        } else {
            return Some(4096);
        };
        let (mut sectors, mut bytes, mut free, mut total) = (0u32, 0u32, 0u32, 0u32);
        let rc = unsafe {
            GetDiskFreeSpaceW(
                wide(&root).as_ptr(),
                &mut sectors,
                &mut bytes,
                &mut free,
                &mut total,
            )
        };
        if rc == 0 {
            return Some(4096);
        }
        Some(u64::from(sectors) * u64::from(bytes))
    }

    /// (volume serial, file index) for the hardlink dedupe (du's st_dev/
    /// st_ino pairing); None when the open fails (device names, etc.).
    pub fn file_handle(path: &Path) -> Option<(u64, u64)> {
        // BY_HANDLE_FILE_INFORMATION's exact layout: dwFileAttributes, three
        // FILETIMEs, dwVolumeSerialNumber, nFileSizeHigh/Low, nNumberOfLinks,
        // nFileIndexHigh/Low. (The wave5 first session mis-declared this as
        // serial+4-pad+index and read the WRITE TIME as the index — files
        // written in the same tick collided and the cross-operand skip ate
        // distinct operands, caught by the du11 gate row.)
        #[repr(C)]
        struct Filetime {
            low: u32,
            high: u32,
        }
        #[repr(C)]
        struct Info {
            attrs: u32,
            creation: Filetime,
            access: Filetime,
            write: Filetime,
            serial: u32,
            size_high: u32,
            size_low: u32,
            nlinks: u32,
            index_high: u32,
            index_low: u32,
        }
        #[link(name = "kernel32")]
        extern "system" {
            fn CreateFileW(
                name: *const u16,
                access: u32,
                share: u32,
                security: *mut core::ffi::c_void,
                disposition: u32,
                flags: u32,
                template: *mut core::ffi::c_void,
            ) -> *mut core::ffi::c_void;
            fn GetFileInformationByHandle(handle: *mut core::ffi::c_void, info: *mut Info) -> i32;
            fn CloseHandle(handle: *mut core::ffi::c_void) -> i32;
        }
        const GENERIC_READ: u32 = 0x8000_0000;
        const SHARE_ALL: u32 = 0x7;
        const OPEN_EXISTING: u32 = 3;
        const BACKUP_SEMANTICS: u32 = 0x0200_0000;
        let text = path.to_string_lossy().into_owned();
        let w = wide(&text);
        let handle = unsafe {
            CreateFileW(
                w.as_ptr(),
                GENERIC_READ,
                SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                BACKUP_SEMANTICS,
                std::ptr::null_mut(),
            )
        };
        if handle as isize == -1 || handle.is_null() {
            return None;
        }
        let mut info: Info = unsafe { std::mem::zeroed() };
        let ok = unsafe { GetFileInformationByHandle(handle, &mut info) };
        unsafe { CloseHandle(handle) };
        if ok == 0 {
            return None;
        }
        Some((
            u64::from(info.serial),
            (u64::from(info.index_high) << 32) | u64::from(info.index_low),
        ))
    }
}

#[cfg(not(windows))]
mod imp {
    pub struct FileData {
        pub attrs: u32,
        pub creation: u64,
        pub access: u64,
        pub write: u64,
        pub size: u64,
    }
    pub fn attributes_data(_path: &str) -> Option<FileData> {
        None
    }
    pub fn allocated(_path: &Path) -> u64 {
        0
    }
    pub fn cluster_size(_path: &str) -> Option<u64> {
        Some(4096)
    }
}

// ---------------------------------------------------------------------------
// traversal
// ---------------------------------------------------------------------------

struct DuConfig {
    apparent: bool,
    block_size: u64,
    human: bool,
    si: bool,
    inodes: bool,
    all: bool,
    separate_dirs: bool,
    max_depth: Option<usize>,
    total: bool,
    count_links: bool,
    one_fs: bool,
    threshold: Option<i64>,
    time_mode: Option<String>,
    time_style: String,
    excludes: Vec<String>,
    nul: bool,
    deref_all: bool,
}

struct Summary {
    /// grand total across operands (bytes/inodes depending on mode)
    grand: u64,
}

/// Match a --exclude pattern against every '/'-component of the relative
/// path (GNU matches file names the way fnmatch sees components).
fn is_excluded(rel: &str, patterns: &[String]) -> bool {
    if patterns.is_empty() {
        return false;
    }
    let mut ok = |name: &str, pat: &str| -> bool { glob_match(pat, name) };
    for comp in rel.split('/') {
        for pat in patterns {
            if ok(comp, pat) {
                return true;
            }
        }
    }
    false
}

/// A small fnmatch: * ? [class] (the subset --exclude patterns use).
fn glob_match(pat: &str, name: &str) -> bool {
    fn inner(p: &[u8], n: &[u8]) -> bool {
        if p.is_empty() {
            return n.is_empty();
        }
        match p[0] {
            b'*' => {
                let mut i = 0;
                loop {
                    if inner(&p[1..], &n[i..]) {
                        return true;
                    }
                    if i >= n.len() {
                        return false;
                    }
                    i += 1;
                }
            }
            b'?' => !n.is_empty() && inner(&p[1..], &n[1..]),
            b'[' => {
                if n.is_empty() {
                    return false;
                }
                let close = match p[1..].iter().position(|&c| c == b']') {
                    Some(i) => i + 1,
                    None => return n[0] == b'[' && inner(&p[1..], &n[1..]),
                };
                let set = &p[1..close];
                let (negated, set) = if set.first() == Some(&b'^') || set.first() == Some(&b'!') {
                    (true, &set[1..])
                } else {
                    (false, set)
                };
                let c = n[0];
                let mut matched = false;
                let mut i = 0;
                while i < set.len() {
                    if i + 2 < set.len() && set[i + 1] == b'-' {
                        if set[i] <= c && c <= set[i + 2] {
                            matched = true;
                        }
                        i += 3;
                    } else {
                        if set[i] == c {
                            matched = true;
                        }
                        i += 1;
                    }
                }
                matched != negated && inner(&p[close + 1..], &n[1..])
            }
            c => !n.is_empty() && n[0] == c && inner(&p[1..], &n[1..]),
        }
    }
    inner(pat.as_bytes(), name.as_bytes())
}

/// Compute one entry's contribution.
fn entry_size(cfg: &DuConfig, path: &Path, is_dir: bool) -> u64 {
    if cfg.inodes {
        return if is_dir { 1 } else { 1 };
    }
    if cfg.apparent {
        let text = path.to_string_lossy();
        match imp::attributes_data(&text) {
            Some(d) => {
                if is_dir {
                    0 // NTFS dirs carry no logical content (module head)
                } else {
                    d.size
                }
            }
            None => 0,
        }
    } else if is_dir {
        0 // honest NTFS: a directory's own allocation reads 0 (module head)
    } else {
        imp::allocated(path)
    }
}

/// Gather mtime (FILETIME epoch seconds) for --time.
fn mtime_of(path: &Path) -> i64 {
    let text = path.to_string_lossy().into_owned();
    match imp::attributes_data(&text) {
        Some(d) => {
            // FILETIME -> epoch seconds
            let ft = d.write as i64;
            (ft - 116444736000000000) / 10_000_000
        }
        None => 0,
    }
}

/// Scale a byte count the way GNU -h/--si does (one decimal, rollover).
fn human_size(mut v: u64, base: u64, si: bool) -> String {
    if v < base {
        return v.to_string();
    }
    let units: [&str; 8] = if si {
        ["k", "M", "G", "T", "P", "E", "Z", "Y"]
    } else {
        ["K", "M", "G", "T", "P", "E", "Z", "Y"]
    };
    let mut idx = 0usize;
    let mut scaled = v as f64 / base as f64;
    while scaled >= base as f64 && idx < units.len() - 1 {
        scaled /= base as f64;
        idx += 1;
    }
    if scaled >= base as f64 {
        return v.to_string();
    }
    // rollover: 1023.96K displays 1.0M (GNU rounds to nearest)
    let tenths = (scaled * 10.0).round() as u64;
    if tenths >= base * 10 && idx + 1 < units.len() {
        return format!("1.0{}", units[idx + 1]);
    }
    if tenths >= base * 10 {
        idx += 1;
    }
    let whole = tenths / 10;
    let frac = tenths % 10;
    format!("{whole}.{frac}{}", units[idx])
}

/// The printed number for a size under cfg.
fn display_size(cfg: &DuConfig, v: u64) -> String {
    if cfg.human {
        return human_size(v, 1024, false);
    }
    if cfg.si {
        return human_size(v, 1000, true);
    }
    if cfg.block_size <= 1 {
        return v.to_string();
    }
    v.div_ceil(cfg.block_size).to_string()
}

/// Walk one operand, printing post-order rows. Returns the subtree size.
#[allow(clippy::too_many_arguments)]
fn walk(
    cfg: &DuConfig,
    display: &str,
    path: &Path,
    depth: usize,
    volume_root: &mut Option<String>,
    seen_handles: &mut HashSet<(u64, u64)>,
    zone: &crate::date::TimeZone,
    out: &mut dyn FnMut(u64, &str, bool, &DuConfig, &Path, &crate::date::TimeZone),
    summaries: &mut Summary,
) -> u64 {
    let meta = match std::fs::symlink_metadata(path) {
        Ok(m) => m,
        Err(_) => return 0,
    };
    let is_symlink = meta.file_type().is_symlink();
    let follow = cfg.deref_all && is_symlink;
    let is_dir = if follow {
        std::fs::metadata(path).map(|m| m.is_dir()).unwrap_or(false)
    } else {
        meta.is_dir() && !is_symlink
    };

    if is_dir {
        // -x: skip subtrees on other volumes
        if cfg.one_fs {
            let root = drive_root(&path.to_string_lossy());
            match volume_root {
                None => *volume_root = Some(root),
                Some(v) if *v != root => return 0,
                _ => {}
            }
        }
        let mut total = if cfg.inodes {
            1
        } else {
            entry_size(cfg, path, true)
        };
        let own = total;
        let read = match std::fs::read_dir(path) {
            Ok(r) => r,
            Err(_) => return total,
        };
        let mut children: Vec<PathBuf> = Vec::new();
        for entry in read.flatten() {
            let child = entry.path();
            let child_rel = format!("{}/{}", display, entry.file_name().to_string_lossy());
            if is_excluded(&child_rel, &cfg.excludes) {
                continue;
            }
            children.push(child);
        }
        for child in children {
            let name = child
                .file_name()
                .map(|n| n.to_string_lossy().into_owned())
                .unwrap_or_default();
            let child_rel = format!("{display}/{name}");
            let child_meta = match std::fs::symlink_metadata(&child) {
                Ok(m) => m,
                Err(_) => continue,
            };
            let child_is_symlink = child_meta.file_type().is_symlink();
            let child_is_dir = child_meta.is_dir()
                && !child_is_symlink
                && !(cfg.deref_all && {
                    std::fs::metadata(&child)
                        .map(|m| m.is_dir())
                        .unwrap_or(false)
                });
            if child_is_dir {
                // [GNU] every counted directory enters the already-counted
                // hash so a later operand nested inside it is skipped whole
                // (du.c hash_ins on each visited directory).
                if !cfg.count_links {
                    if let Some(k) = file_handle(&child) {
                        seen_handles.insert(k);
                    }
                }
                let sub = walk(
                    cfg,
                    &child_rel,
                    &child,
                    depth + 1,
                    volume_root,
                    seen_handles,
                    zone,
                    out,
                    summaries,
                );
                if !cfg.separate_dirs {
                    total += sub;
                }
            } else {
                let size = if cfg.inodes {
                    1
                } else if cfg.apparent {
                    imp::attributes_data(&child.to_string_lossy())
                        .map(|d| d.size)
                        .unwrap_or(0)
                } else {
                    // hardlink dedupe unless -l (GNU st_nlink>1 dedupe)
                    let mut key = None;
                    if !cfg.count_links {
                        key = file_handle(&child);
                    }
                    let sz = imp::allocated(&child);
                    if let Some(k) = key {
                        // zero-size files enter the hash too: the dedupe
                        // gates a later OPERAND, not just the size addend.
                        if !seen_handles.insert(k) {
                            0
                        } else {
                            sz
                        }
                    } else {
                        sz
                    }
                };
                if cfg.inodes {
                    total += 1;
                } else {
                    total += size;
                }
                let passes = threshold_passes(cfg, size);
                if cfg.all && passes {
                    print_row(cfg, size, &child_rel, false, &child, zone, out);
                } else if cfg.all && passes {
                    print_row(cfg, size, &child_rel, false, &child, zone, out);
                }
            }
        }
        let passes = threshold_passes(cfg, total);
        let within_depth = cfg.max_depth.map(|md| depth <= md).unwrap_or(true);
        if !cfg.separate_dirs || passes {
            if within_depth && (passes || total == 0) {
                print_row(cfg, total, display, true, path, zone, out);
            }
        }
        return total;
    }

    // a file operand
    let size = if cfg.inodes {
        1
    } else if cfg.apparent {
        imp::attributes_data(&path.to_string_lossy())
            .map(|d| d.size)
            .unwrap_or(0)
    } else {
        imp::allocated(path)
    };
    let passes = threshold_passes(cfg, size);
    if passes {
        print_row(cfg, size, display, true, path, zone, out);
    }
    size
}

fn threshold_passes(cfg: &DuConfig, size: u64) -> bool {
    match cfg.threshold {
        None => true,
        Some(t) if t >= 0 => size >= t as u64,
        Some(t) => size <= (-t) as u64,
    }
}

fn drive_root(path: &str) -> String {
    let p = path.replace('/', "\\");
    if p.len() >= 2 && p.as_bytes()[1] == b':' {
        p[..2].to_ascii_uppercase()
    } else if p.starts_with(r"\\") {
        // UNC share
        let rest = &p[2..];
        match rest.find('\\') {
            Some(i) => {
                let after = &rest[i + 1..];
                match after.find('\\') {
                    Some(j) => p[..2 + i + 1 + j].to_string(),
                    None => p.clone(),
                }
            }
            None => p.clone(),
        }
    } else {
        "?".to_string()
    }
}

fn print_row(
    cfg: &DuConfig,
    size: u64,
    display: &str,
    _is_root_row: bool,
    path: &Path,
    zone: &crate::date::TimeZone,
    out: &mut dyn FnMut(u64, &str, bool, &DuConfig, &Path, &crate::date::TimeZone),
) {
    if let Some(word) = &cfg.time_mode {
        let epoch = mtime_of(path);
        let (off, name) = zone.at(epoch);
        let dt = crate::date::DateTime::from_epoch(epoch, off, name);
        let text = strftime(&dt, &style_format(cfg));
        out(
            size,
            &format!("{text}\t{display}"),
            cfg.nul,
            cfg,
            path,
            zone,
        );
    } else {
        out(size, &display, cfg.nul, cfg, path, zone);
    }
}

fn style_format(cfg: &DuConfig) -> String {
    match cfg.time_style.as_str() {
        "full-iso" => "%Y-%m-%d %H:%M:%S.%N %z".to_string(),
        "iso" => "%F".to_string(),
        "long-iso" => "%F %H:%M".to_string(),
        other if other.starts_with('+') => other[1..].to_string(),
        _ => "%F %H:%M".to_string(),
    }
}

fn file_handle(path: &Path) -> Option<(u64, u64)> {
    imp::file_handle(path)
}

fn parse_block_size(raw: &str) -> Option<u64> {
    // split at the first alphabetic (the unit letter is PART of the
    // suffix, unlike splitn-with-predicate which eats it)
    let (num, suffix): (String, String) = match raw.find(|c: char| c.is_ascii_alphabetic()) {
        Some(i) => (raw[..i].to_string(), raw[i..].to_string()),
        None => (raw.to_string(), String::new()),
    };
    let n: u64 = num.parse().ok()?;
    if suffix.is_empty() {
        return Some(n);
    }
    let upper = suffix.to_ascii_uppercase();
    fn strip_b(s: &str) -> &str {
        // KiB/MiB/GiB: strip the IB pair first, then a plain B
        let s = s.strip_suffix("IB").unwrap_or(s);
        s.strip_suffix('B').unwrap_or(s)
    }
    let unit = strip_b(&upper);
    let pow = match unit {
        "" => return Some(n),
        "K" => 1u64,
        "M" => 2,
        "G" => 3,
        "T" => 4,
        "P" => 5,
        "E" => 6,
        "Z" => 7,
        "Y" => 8,
        _ => return None,
    };
    Some(n.checked_mul(1024u64.checked_pow(pow as u32)?)?)
}

pub fn du_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // --time[=WORD] is GNU optional_argument: strip the value-carrying
    // spelling before argopt (argopt::extract_optional_long_values), the
    // bare --time flag stays in OPTS.
    let (time_values, rest) = argopt::extract_optional_long_values(args, "time", "time");
    let p = match argopt::parse("du", &rest, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                write!(
                    io.stdout,
                    "{}{}",
                    DU_HELP_BODY,
                    gnuhelp::footer("du", "du invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version(
                        "du",
                        "Torbj\u{f6}rn Granlund, David MacKenzie, Paul Eggert,\nand Jim Meyering"
                    )
                )?;
                0
            }
        });
    }

    let mut cfg = DuConfig {
        apparent: p.has("apparent-size") || p.has("bytes"),
        block_size: 1024,
        human: p.has("human-readable"),
        si: p.has("si"),
        inodes: p.has("inodes"),
        all: p.has("all"),
        separate_dirs: p.has("separate-dirs"),
        max_depth: None,
        total: p.has("total"),
        count_links: p.has("count-links"),
        one_fs: p.has("one-file-system"),
        threshold: None,
        time_mode: None,
        time_style: "long-iso".to_string(),
        excludes: Vec::new(),
        nul: p.has("null"),
        deref_all: p.has("dereference"),
    };

    if p.has("bytes") || p.has("inodes") {
        cfg.block_size = 1;
    }
    if p.has("k") {
        cfg.block_size = 1024;
    }
    if p.has("m") {
        cfg.block_size = 1024 * 1024;
    }
    if let Some(raw) = p.value_of("block-size") {
        match parse_block_size(raw) {
            Some(v) => cfg.block_size = v,
            None => {
                let _ = writeln!(io.stderr, "du: invalid --block-size argument '{raw}'");
                return Ok(1);
            }
        }
    }
    if let Some(raw) = p.value_of("max-depth") {
        match raw.parse::<usize>() {
            Ok(v) => cfg.max_depth = Some(v),
            Err(_) => {
                let _ = writeln!(
                    io.stderr,
                    "du: invalid maximum depth '{}' (expected an integer 0 or larger)",
                    raw
                );
                return Ok(1);
            }
        }
    }
    if p.has("summarize") {
        cfg.max_depth = Some(0);
    }
    if let Some(raw) = p.value_of("threshold") {
        match raw.parse::<i64>() {
            Ok(v) => cfg.threshold = Some(v),
            Err(_) => {
                let _ = writeln!(io.stderr, "du: invalid --threshold argument '{}'", raw);
                return Ok(1);
            }
        }
    }
    if let Some(style) = p.value_of("time-style") {
        cfg.time_style = style.to_string();
    }
    // The --time WORD arrives through the optional-argument extractor (argv
    // order; the last spelling wins the way getopt's assignments do).
    if let Some(word) = time_values.iter().rev().find(|(k, _)| k == "time") {
        cfg.time_mode = Some(word.1.clone());
    } else if p.has("time") {
        cfg.time_mode = Some("mtime".to_string());
    }
    for pat in &p.values {
        if pat.0 == "exclude" {
            cfg.excludes.push(pat.1.clone());
        }
    }
    if let Some(file) = p.value_of("exclude-from") {
        let path = resolve(ctx, file);
        match std::fs::read_to_string(&path) {
            Ok(t) => {
                for line in t.lines() {
                    if !line.trim().is_empty() {
                        cfg.excludes.push(line.trim().to_string());
                    }
                }
            }
            Err(e) => {
                let reason = crate::fsutil::reason(&e);
                let _ = writeln!(io.stderr, "du: {file}: {reason}");
                return Ok(1);
            }
        }
    }

    let zone = effective_zone(false, ctx.env);
    let mut failed = false;
    let mut operands: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    if let Some(list) = p.value_of("files0-from") {
        // [GNU] du.c --files0-from (the shared gnulib read-file-names shape,
        // probed under WSL 9.4): the operand list is replaced by the
        // NUL-separated names in F ('-' = stdin). A trailing separator does
        // not produce a name; any other zero-length name reports
        // "F:N: invalid zero-length file name" (1-based ordinal) on stderr
        // and processing CONTINUES, with final status 1. File operands
        // cannot be combined ("extra operand", rc 1); an open failure reads
        // "cannot open 'F' for reading: <reason>", rc 1. An EMPTY list
        // summarizes nothing and exits 0 — it does NOT fall back to ".".
        if !operands.is_empty() {
            let _ = writeln!(
                io.stderr,
                "du: {}",
                crate::messages::extra_operand(ctx, &operands[0])
            );
            let _ = writeln!(
                io.stderr,
                "file operands cannot be combined with --files0-from"
            );
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "du"));
            return Ok(1);
        }
        let data = if list == "-" {
            let mut buf = Vec::new();
            if io.stdin.read_to_end(&mut buf).is_err() {
                let _ = writeln!(io.stderr, "du: -: read error");
                return Ok(1);
            }
            String::from_utf8_lossy(&buf).into_owned()
        } else {
            let path = resolve(ctx, list);
            match std::fs::read(&path) {
                Ok(bytes) => String::from_utf8_lossy(&bytes).into_owned(),
                Err(e) => {
                    let reason = crate::fsutil::reason(&e);
                    let _ = writeln!(io.stderr, "du: cannot open '{list}' for reading: {reason}");
                    return Ok(1);
                }
            }
        };
        // An empty file holds NO names (probed: rc 0, silent); a non-empty
        // body splits on NUL and a trailing separator adds no name.
        let mut names: Vec<&str> = if data.is_empty() {
            Vec::new()
        } else {
            data.split('\0').collect()
        };
        if data.ends_with('\0') {
            names.pop();
        }
        // GNU keeps processing the valid names and exits 1 at the end
        // (probed: bad.nul -> rows for a and b, the diagnostic, rc 1);
        // the access-error flag below carries the status.
        let mut bad_names = false;
        operands = Vec::new();
        for (i, name) in names.iter().enumerate() {
            if name.is_empty() {
                let _ = writeln!(
                    io.stderr,
                    "du: {list}:{}: invalid zero-length file name",
                    i + 1
                );
                bad_names = true;
                continue;
            }
            operands.push((*name).to_string());
        }
        if bad_names {
            failed = true;
        }
    } else if operands.is_empty() {
        operands = vec![".".to_string()];
    }

    let mut summaries = Summary { grand: 0 };
    // [GNU] one process-wide already-counted hash (du.c hash_ins/seen_file):
    // every counted entry — files AND directories, zero-size included — is
    // recorded, and an OPERAND whose inode is already counted is skipped
    // silently (`du a a` prints one row; `du a a/sub` prints the subtree
    // rows from the first walk and drops the nested operand; probed WSL
    // 9.4). -l/--count-links bypasses the hash entirely: repeated operands
    // are re-counted and re-printed.
    let mut seen = HashSet::new();
    for operand in &operands {
        let path = resolve(ctx, operand);
        if std::fs::symlink_metadata(&path).is_err() {
            let _ = writeln!(
                io.stderr,
                "du: cannot access '{operand}': No such file or directory"
            );
            failed = true;
            continue;
        }
        if !cfg.count_links {
            match file_handle(&path) {
                Some(k) => {
                    if !seen.insert(k) {
                        continue;
                    }
                }
                None => {
                    let _ = seen.insert((0, 0));
                }
            }
        }
        let mut volume_root = None;
        let mut sink = |size: u64,
                        tail: &str,
                        nul: bool,
                        cfg: &DuConfig,
                        _p: &Path,
                        _z: &crate::date::TimeZone| {
            let num = display_size(cfg, size);
            let _ = if nul {
                write!(io.stdout, "{num}\t{tail}\0")
            } else {
                writeln!(io.stdout, "{num}\t{tail}")
            };
        };
        let sub = walk(
            &cfg,
            operand,
            &path,
            0,
            &mut volume_root,
            &mut seen,
            &zone,
            &mut sink,
            &mut summaries,
        );
        summaries.grand += sub;
    }
    if cfg.total && !operands.is_empty() {
        let num = display_size(&cfg, summaries.grand);
        let _ = if cfg.nul {
            write!(io.stdout, "{num}\ttotal\0")
        } else {
            writeln!(io.stdout, "{num}\ttotal")
        };
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_in(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = du_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn fixture(sc: &ScratchCtx) {
        std::fs::write(sc.path("f5"), "12345").unwrap();
        std::fs::File::create(sc.path("empty")).unwrap();
        std::fs::create_dir_all(sc.path("d1/d2")).unwrap();
        std::fs::write(sc.path("d1/x"), "abcdefghij").unwrap();
        std::fs::write(sc.path("d1/d2/y"), "0123456789abcdefghijklmnopqrst").unwrap();
    }

    #[test]
    fn apparent_sizes_are_byte_exact() {
        // probed d2/d30: -b gives logical sizes; dirs contribute 0 own
        let sc = ScratchCtx::new("du-a");
        fixture(&sc);
        let (r, out, err) = run_in(&sc, &["-b", "f5"]);
        assert_eq!(r, 0);
        assert_eq!(out, "5\tf5\n");
        assert!(err.is_empty());
        let (r, out, _) = run_in(&sc, &["-b", "d1"]);
        assert_eq!(r, 0);
        // probed d30 model: dir rows SUM their children's apparent sizes
        // (d1/d2 = y 30; d1 = x 10 + 30 = 40; dirs carry no own content)
        assert_eq!(out, "30\td1/d2\n40\td1\n");
        let (_, out, _) = run_in(&sc, &["-b", "d1/d2"]);
        assert_eq!(out, "30\td1/d2\n");
    }

    #[test]
    fn default_blocks_match_cluster_allocation() {
        // probed d1: a 5-byte regular file answers one 4K unit -> "4"
        let sc = ScratchCtx::new("du-b");
        fixture(&sc);
        let (r, out, _) = run_in(&sc, &["f5"]);
        assert_eq!(r, 0);
        assert_eq!(out, "4\tf5\n");
        let (_, out, _) = run_in(&sc, &["empty"]);
        assert_eq!(out, "0\tempty\n");
    }

    #[test]
    fn human_and_si_scaling() {
        // probed d4: 4096 -> 4.0K; 4097 -> 8.0K (two clusters)
        let sc = ScratchCtx::new("du-c");
        fixture(&sc);
        std::fs::write(sc.path("f4k1"), vec![0u8; 4097]).unwrap();
        let (r, out, _) = run_in(&sc, &["-h", "f5"]);
        assert_eq!(out, "4.0K\tf5\n");
        let (_, out, _) = run_in(&sc, &["-h", "f4k1"]);
        assert_eq!(out, "8.0K\tf4k1\n");
        let (_, out, _) = run_in(&sc, &["--si", "f5"]);
        assert_eq!(out, "4.1k\tf5\n");
    }

    #[test]
    fn block_size_forms() {
        // probed d6: -B1 -> raw allocated bytes; -b == apparent+1
        let sc = ScratchCtx::new("du-d");
        fixture(&sc);
        let (r, out, _) = run_in(&sc, &["-B1", "f5"]);
        assert_eq!(out, "4096\tf5\n");
        let (_, out, _) = run_in(&sc, &["-b", "f5"]);
        assert_eq!(out, "5\tf5\n");
        let (_, out, _) = run_in(&sc, &["-B1M", "f5"]);
        assert_eq!(out, "1\tf5\n");
    }

    #[test]
    fn directory_post_order_and_depth() {
        // post-order: subdir before parent (Savannah #13956)
        let sc = ScratchCtx::new("du-e");
        fixture(&sc);
        let (r, out, _) = run_in(&sc, &["-b", "d1"]);
        assert_eq!(out, "30\td1/d2\n40\td1\n");
        let (r, out, _) = run_in(&sc, &["d1"]);
        let rows: Vec<&str> = out.lines().collect();
        assert_eq!(rows.len(), 2);
        assert!(rows[0].ends_with("d1/d2"));
        assert!(rows[1].ends_with("d1"));
        // -d0 == -s: only the operand row
        let (_, out, _) = run_in(&sc, &["-d0", "d1"]);
        assert_eq!(out.lines().count(), 1);
        // -a lists files too (x, y and the two dirs)
        let (_, out, _) = run_in(&sc, &["-b", "-a", "d1"]);
        assert_eq!(out.lines().count(), 4);
    }

    #[test]
    fn inodes_count_entries() {
        // probed d18: --inodes d1 -> 2 for the sub, 4 for the operand
        let sc = ScratchCtx::new("du-f");
        fixture(&sc);
        let (r, out, _) = run_in(&sc, &["--inodes", "d1"]);
        let rows: Vec<&str> = out.lines().collect();
        assert_eq!(rows.len(), 2);
        assert!(rows[0].starts_with("2\t"));
        assert!(rows[1].starts_with("4\t"));
        let (_, out, _) = run_in(&sc, &["--inodes", "f5"]);
        assert_eq!(out, "1\tf5\n");
    }

    #[test]
    fn exclude_and_threshold() {
        // probed d22: --exclude=x drops d1/x rows; -t filters small entries
        let sc = ScratchCtx::new("du-g");
        fixture(&sc);
        let (r, out, _) = run_in(&sc, &["-b", "--exclude=x", "-a", "d1"]);
        assert!(!out.contains("d1/x"));
        let (_, out, _) = run_in(&sc, &["-b", "-a", "-t", "20", "d1"]);
        assert!(out.contains("d1/d2/y"));
        assert!(!out.contains("d1/x"));
    }

    #[test]
    fn total_and_missing_operands() {
        // probed d12: grand "total" row; d13: missing continues rc 1
        let sc = ScratchCtx::new("du-h");
        fixture(&sc);
        let (r, out, _) = run_in(&sc, &["-c", "f5", "empty"]);
        let rows: Vec<&str> = out.lines().collect();
        assert_eq!(rows.len(), 3);
        assert!(rows[2].ends_with("total"));
        let (r, out, err) = run_in(&sc, &["nosuch", "f5"]);
        assert_eq!(r, 1);
        assert!(err.contains("du: cannot access 'nosuch': No such file or directory"));
        assert_eq!(out, "4\tf5\n");
    }

    #[test]
    fn no_operand_uses_dot() {
        // probed d23: rows prefix "./"
        let sc = ScratchCtx::new("du-i");
        fixture(&sc);
        let (r, out, _) = run_in(&sc, &["-b", "-a"]);
        assert!(out.contains("./d1/d2"));
        assert!(out.lines().last().unwrap().ends_with("."));
    }

    #[test]
    fn time_rows_shape() {
        // du --time = SIZE\tYYYY-MM-DD HH:MM\tPATH (probed d26)
        let mut sc = ScratchCtx::new("du-j");
        fixture(&sc);
        sc.env.set("TZ", "UTC");
        let (r, out, _) = run_in(&sc, &["--time", "-b", "f5"]);
        assert_eq!(r, 0);
        let cols: Vec<&str> = out.split('\t').collect();
        assert_eq!(cols.len(), 3);
        assert_eq!(cols[0], "5");
        assert_eq!(cols[1].len(), 16);
        assert_eq!(&cols[1][4..5], "-");
        assert_eq!(cols[2], "f5\n");
    }

    #[test]
    fn separate_dirs_shape() {
        let sc = ScratchCtx::new("du-k");
        fixture(&sc);
        // -S: dir rows exclude subtree totals (d1/d2 = y 30; d1 = x 10;
        // each directory's own NTFS allocation is the documented 0)
        let (r, out, _) = run_in(&sc, &["-b", "-S", "d1"]);
        assert_eq!(out, "30\td1/d2\n10\td1\n");
    }

    #[test]
    fn help_version_surface() {
        let sc = ScratchCtx::new("du-l");
        let (r, out, err) = run_in(&sc, &["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: du [OPTION]... [FILE]...\n"));
        assert!(out
            .contains("  -b, --bytes           equivalent to '--apparent-size --block-size=1'\n"));
        assert!(out.contains("Binary prefixes can be used, too: KiB=K, MiB=M, and so on."));
        let (r, out, _) = run_in(&sc, &["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("du (GNU coreutils) 9.4\n"));
        assert!(out.ends_with(
            "Written by Torbj\u{f6}rn Granlund, David MacKenzie, Paul Eggert,\nand Jim Meyering.\n"
        ));
    }
}

#[cfg(test)]
mod files0_from_tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = du_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn fixture(sc: &ScratchCtx) {
        std::fs::create_dir_all(sc.path("a")).unwrap();
        std::fs::write(sc.path("a/f"), "xy").unwrap();
        std::fs::write(sc.path("b-ignored"), "z").unwrap();
        std::fs::write(sc.path("list.nul"), "a\0b-ignored\0").unwrap();
        std::fs::write(sc.path("bad.nul"), "a\0\0b\0").unwrap();
        std::fs::write(sc.path("empty.nul"), "").unwrap();
    }

    #[test]
    fn files0_from_replaces_operands() {
        // Probed GNU 9.4: names come from the NUL list, one row per name.
        let sc = ScratchCtx::new("du-f0f");
        fixture(&sc);
        let (r, out, err) = run(&sc, &["--files0-from=list.nul", "-b"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.contains("\ta\n"));
        assert!(out.contains("\tb-ignored\n"));
        assert!(!out.contains("b-ignored\t"));
    }

    #[test]
    fn files0_from_missing_list() {
        // Probed: "du: cannot open 'nosuch.nul' for reading: No such file
        // or directory", rc 1, empty stdout.
        let sc = ScratchCtx::new("du-f0f-miss");
        fixture(&sc);
        let (r, out, err) = run(&sc, &["--files0-from=nosuch.nul", "-b"]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("du: cannot open 'nosuch.nul' for reading:"));
    }

    #[test]
    fn files0_from_empty_list_is_silent_success() {
        // Probed: empty list -> no output, rc 0 — it does NOT default to ".".
        let sc = ScratchCtx::new("du-f0f-empty");
        fixture(&sc);
        let (r, out, err) = run(&sc, &["--files0-from=empty.nul", "-b"]);
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert!(err.is_empty());
    }

    #[test]
    fn files0_from_zero_length_name_reports_and_exits_one() {
        // Probed: "du: bad.nul:2: invalid zero-length file name", the valid
        // names still process, final rc 1. The trailing separator does not
        // produce a name (list.nul case is clean).
        let sc = ScratchCtx::new("du-f0f-bad");
        fixture(&sc);
        let (r, out, err) = run(&sc, &["--files0-from=bad.nul", "-b"]);
        assert_eq!(r, 1);
        assert!(out.contains("\ta\n"));
        assert!(err.contains("du: bad.nul:2: invalid zero-length file name"));
    }

    #[test]
    fn repeated_operands_dedupe_across_one_process() {
        // Probed GNU 9.4 (probe-du-dedupe.sh): one process-wide
        // already-counted hash; a repeated operand is skipped silently and
        // -l/--count-links bypasses it.
        let sc = ScratchCtx::new("du-dedupe");
        std::fs::create_dir_all(sc.path("d1/d2")).unwrap();
        std::fs::write(sc.path("d1/f"), "x").unwrap();
        let (r, out, _) = run(&sc, &["-b", "d1", "d1"]);
        assert_eq!(r, 0);
        assert_eq!(out.matches("\td1\n").count(), 1);
        assert!(out.contains("\td1/d2\n"));
        let (r, out, _) = run(&sc, &["-lb", "d1", "d1"]);
        assert_eq!(r, 0);
        assert_eq!(out.matches("\td1\n").count(), 2);
        // A nested operand already counted inside the first walk is dropped.
        let (r, out, _) = run(&sc, &["-b", "d1", "d1/d2"]);
        assert_eq!(r, 0);
        assert_eq!(out.matches("\td1/d2\n").count(), 1);
        assert!(out.ends_with("\td1\n"));
    }

    #[test]
    fn files0_from_rejects_file_operands() {
        // Probed: "extra operand", the combine rule, try-help, rc 1.
        let sc = ScratchCtx::new("du-f0f-combine");
        fixture(&sc);
        let (r, out, err) = run(&sc, &["--files0-from=list.nul", "-b", "c"]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("du: extra operand 'c'"));
        assert!(err.contains("file operands cannot be combined with --files0-from"));
        assert!(err.contains("Try 'du --help' for more information."));
    }
}
