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
use winux_core::{tr, CmdResult, Ctx, Io};

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
    use crate::fs_win::ByHandleFileInformation;
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
    ///
    /// `reparse` opens the reparse point ITSELF (FILE_FLAG_OPEN_REPARSE_POINT)
    /// instead of following it — the link identity GNU hashes for an
    /// unfollowed link entry (#1670 keeps `-P` link and real directory
    /// distinct).
    fn identity_of(path: &Path, reparse: bool) -> Option<(u64, u64)> {
        // The info struct is fs_win's shared ByHandleFileInformation
        // (winuxcmd#1638): one signature for GetFileInformationByHandle
        // across fs_win/du/tail. Its layout is BY_HANDLE_FILE_INFORMATION's
        // exact field order (dwFileAttributes, three FILETIMEs,
        // dwVolumeSerialNumber, nFileSizeHigh/Low, nNumberOfLinks,
        // nFileIndexHigh/Low) — the wave5 first session mis-declared this
        // as serial+4-pad+index and read the WRITE TIME as the index,
        // caught by the du11 gate row.
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
            fn GetFileInformationByHandle(
                handle: *mut core::ffi::c_void,
                info: *mut ByHandleFileInformation,
            ) -> i32;
            fn CloseHandle(handle: *mut core::ffi::c_void) -> i32;
        }
        const GENERIC_READ: u32 = 0x8000_0000;
        const SHARE_ALL: u32 = 0x7;
        const OPEN_EXISTING: u32 = 3;
        const BACKUP_SEMANTICS: u32 = 0x0200_0000;
        const OPEN_REPARSE_POINT: u32 = 0x0020_0000;
        let text = path.to_string_lossy().into_owned();
        let w = wide(&text);
        let flags = if reparse {
            BACKUP_SEMANTICS | OPEN_REPARSE_POINT
        } else {
            BACKUP_SEMANTICS
        };
        let access = if reparse { 0 } else { GENERIC_READ };
        let handle = unsafe {
            CreateFileW(
                w.as_ptr(),
                access,
                SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                flags,
                std::ptr::null_mut(),
            )
        };
        if handle as isize == -1 || handle.is_null() {
            return None;
        }
        let mut info = ByHandleFileInformation::default();
        let ok = unsafe { GetFileInformationByHandle(handle, &mut info) };
        unsafe { CloseHandle(handle) };
        if ok == 0 {
            return None;
        }
        Some((
            u64::from(info.volume_serial),
            (u64::from(info.index_high) << 32) | u64::from(info.index_low),
        ))
    }

    /// Identity of the path the OS resolves (native symlinks/junctions are
    /// followed; a DrvFs LX link is opaque and answers None).
    pub fn file_handle(path: &Path) -> Option<(u64, u64)> {
        identity_of(path, false)
    }

    /// Identity of the link object itself (never followed).
    pub fn link_handle(path: &Path) -> Option<(u64, u64)> {
        identity_of(path, true)
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
    pub fn file_handle(_path: &Path) -> Option<(u64, u64)> {
        None
    }
    pub fn link_handle(_path: &Path) -> Option<(u64, u64)> {
        None
    }
}

// ---------------------------------------------------------------------------
// traversal
// ---------------------------------------------------------------------------

/// --time[=WORD] selector (GNU du.c time_args/time_types).
#[derive(Clone, Copy, PartialEq, Eq)]
enum TimeMode {
    Modification,
    Access,
    Status,
}

struct DuConfig {
    apparent: bool,
    block_size: u64,
    /// Unit letter appended for a BARE-suffix block size (`-BM` -> "M",
    /// `--block-size=KiB` -> "KiB"); empty for numeric sizes.
    display_suffix: String,
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
    time_mode: TimeMode,
    /// --time was given (the timestamp column is printed).
    show_time: bool,
    time_style: String,
    excludes: Vec<String>,
    nul: bool,
    deref_all: bool,
    /// -H/--dereference-args: follow links only for operands listed on the
    /// command line (#1656).
    deref_args: bool,
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

/// Scale a byte count the way GNU -h/--si does (one decimal, rollover).
fn human_size(v: u64, base: u64, si: bool) -> String {
    // [GNU] lib/human.c human_readable: ceiling to tenths of the chosen
    // unit; one decimal only while the value is below 10 units ("4.0K",
    // "1.5K"), an integer above it ("20K", "21k", "1.2M"). The pre-wiring
    // port always printed one decimal, so `du --si` on 20000 bytes said
    // "19.6k" where GNU says "20k" and the ctest pinned "20k".
    if v < base {
        return v.to_string();
    }
    let units: [&str; 8] = if si {
        ["k", "M", "G", "T", "P", "E", "Z", "Y"]
    } else {
        ["K", "M", "G", "T", "P", "E", "Z", "Y"]
    };
    let mut idx = 0usize;
    let mut unit = base;
    while idx + 1 < units.len() && v / unit >= base {
        unit = unit.saturating_mul(base);
        idx += 1;
    }
    let q = v / unit;
    let r = v % unit;
    let mut tenths = q * 10 + (r * 10).div_ceil(unit);
    while tenths >= base * 10 && idx + 1 < units.len() {
        tenths = tenths.div_ceil(base);
        idx += 1;
    }
    if tenths < 100 {
        format!("{}.{}{}", tenths / 10, tenths % 10, units[idx])
    } else {
        format!("{}{}", tenths.div_ceil(10), units[idx])
    }
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
        return format!("{v}{}", cfg.display_suffix);
    }
    format!("{}{}", v.div_ceil(cfg.block_size), cfg.display_suffix)
}

/// Selected timestamp (seconds since the Unix epoch) for one entry.
fn entry_time(cfg: &DuConfig, path: &Path) -> Option<i64> {
    let text = path.to_string_lossy();
    let data = imp::attributes_data(&text)?;
    let ft = match cfg.time_mode {
        TimeMode::Modification => data.write,
        TimeMode::Access => data.access,
        TimeMode::Status => data.creation,
    };
    Some((ft as i64 - 116444736000000000) / 10_000_000)
}

/// result of walking one node: its size contribution and the latest
/// selected timestamp in the subtree (GNU du --time prints the max for a
/// directory row, not the directory's own stamp).
struct Walked {
    size: u64,
    latest: Option<i64>,
}

fn latest_of(a: Option<i64>, b: Option<i64>) -> Option<i64> {
    match (a, b) {
        (Some(x), Some(y)) => Some(x.max(y)),
        (x, None) => x,
        (None, y) => y,
    }
}

/// Walk one operand, printing post-order rows.
#[allow(clippy::too_many_arguments)]
fn walk(
    ctx: &Ctx,
    cfg: &DuConfig,
    display: &str,
    path: &Path,
    depth: usize,
    volume_root: &mut Option<String>,
    seen_handles: &mut HashSet<(u64, u64)>,
    visited: &mut HashSet<String>,
    zone: &crate::date::TimeZone,
    out: &mut dyn FnMut(u64, &str, bool, &DuConfig),
    summaries: &mut Summary,
) -> Walked {
    let meta = match std::fs::symlink_metadata(path) {
        Ok(m) => m,
        Err(_) => {
            return Walked {
                size: 0,
                latest: None,
            }
        }
    };
    let is_symlink = meta.file_type().is_symlink();
    // [GNU] -L dereferences every link; -H/--dereference-args dereferences
    // only links LISTED ON THE COMMAND LINE, i.e. the depth-0 operand
    // (#1656: `du -H dirlink` descends into the target's subtree).
    let follow = (cfg.deref_all || (cfg.deref_args && depth == 0)) && is_symlink;
    // [#1748] Win32 follows native symlinks/junctions inside the kernel,
    // but a DrvFs LX link (the shape `ln -s` writes on a /mnt volume) is
    // opaque to it: depending on the build, `metadata()` reports the link
    // itself and `read_dir()` cannot enumerate it, so `du -L` printed just
    // the link's own row (or a 0-byte row) instead of the target subtree.
    // Resolve the stored target once and walk THAT path; `display` keeps
    // the operand spelling, exactly like GNU's dereference.
    let lx_follow = if follow {
        followed_lx_dir_target(ctx, path)
    } else {
        None
    };
    let walk_path = lx_follow.as_deref().unwrap_or(path);
    let is_dir = if follow {
        std::fs::metadata(walk_path)
            .map(|m| m.is_dir())
            .unwrap_or(false)
    } else {
        meta.is_dir() && !is_symlink
    };

    // GNU prints a child file under -a only while its own depth is within
    // --max-depth (the root is depth 0).
    let file_depth_ok = cfg.max_depth.map(|md| depth + 1 <= md).unwrap_or(true);
    let child_display = |name: &str| -> String {
        let base = display.trim_end_matches(['/', '\\']);
        if base.is_empty() {
            format!("/{name}")
        } else {
            format!("{base}/{name}")
        }
    };

    if is_dir {
        // -x: skip subtrees on other volumes
        if cfg.one_fs {
            let root = drive_root(&walk_path.to_string_lossy());
            match volume_root {
                None => *volume_root = Some(root),
                Some(v) if *v != root => {
                    return Walked {
                        size: 0,
                        latest: None,
                    }
                }
                _ => {}
            }
        }
        let mut total = if cfg.inodes {
            1
        } else {
            entry_size(cfg, walk_path, true)
        };
        let mut latest = if cfg.show_time {
            entry_time(cfg, walk_path)
        } else {
            None
        };
        let read = match std::fs::read_dir(walk_path) {
            Ok(r) => r,
            Err(_) => {
                return Walked {
                    size: total,
                    latest,
                }
            }
        };
        let mut children: Vec<PathBuf> = Vec::new();
        for entry in read.flatten() {
            let child = entry.path();
            let child_rel = child_display(&entry.file_name().to_string_lossy());
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
            let child_rel = child_display(&name);
            let child_meta = match std::fs::symlink_metadata(&child) {
                Ok(m) => m,
                Err(_) => continue,
            };
            let child_is_symlink = child_meta.file_type().is_symlink();
            // [GNU] a plain directory is walked; under -L a LINK that
            // resolves to a directory is walked too. The pre-wiring
            // expression negated the dereference test, so -L skipped every
            // real subdirectory (`du -L dir` printed one row).
            // [#1748] `metadata()` follows native symlinks/junctions but
            // not a DrvFs LX link, so fall back to the stored target.
            let child_is_dir = if child_is_symlink {
                cfg.deref_all
                    && (std::fs::metadata(&child)
                        .map(|m| m.is_dir())
                        .unwrap_or(false)
                        || followed_lx_dir_target(ctx, &child).is_some())
            } else {
                child_meta.is_dir()
            };
            if child_is_dir {
                // [GNU] A subtree already traversed under an earlier argument
                // contributes nothing to this parent (Savannah #10397): the
                // directory enters the visited set before descending, keyed
                // by the FOLLOWED target's identity under -L (#1670) and by
                // the lexical spelling otherwise. --count-links disables the
                // whole dedup (GNU's `! opt_count_all`, du.c:546-549).
                if !cfg.count_links {
                    let key = visited_dir_key(ctx, &child, cfg.deref_all);
                    if !visited.insert(key) {
                        continue;
                    }
                }
                let sub = walk(
                    ctx,
                    cfg,
                    &child_rel,
                    &child,
                    depth + 1,
                    volume_root,
                    seen_handles,
                    visited,
                    zone,
                    out,
                    summaries,
                );
                if !cfg.separate_dirs {
                    total += sub.size;
                }
                latest = latest_of(latest, sub.latest);
            } else {
                // [GNU] hardlink dedupe by (volume, file index) in EVERY
                // mode, --apparent-size and --inodes included: the addend
                // is 0 for an already-counted inode. -l/--count-links
                // bypasses the hash (du.c hash_ins/seen_file). An UNFOLLOWED
                // link entry is hashed by the LINK's own identity, so `-P`
                // never dedupes a link against the directory it names
                // (#1670).
                let link_entry = child_is_symlink && !cfg.deref_all;
                let mut counted = true;
                if !cfg.count_links {
                    let key = if link_entry {
                        link_handle(&child)
                    } else {
                        file_handle(&child)
                    };
                    if let Some(k) = key {
                        counted = seen_handles.insert(k);
                    }
                }
                let size = if !counted {
                    0
                } else if cfg.inodes {
                    1
                } else if cfg.apparent {
                    imp::attributes_data(&child.to_string_lossy())
                        .map(|d| d.size)
                        .unwrap_or(0)
                } else {
                    imp::allocated(&child)
                };
                let child_time = if cfg.show_time {
                    entry_time(cfg, &child)
                } else {
                    None
                };
                latest = latest_of(latest, child_time);
                if cfg.inodes {
                    total += if counted { 1 } else { 0 };
                } else {
                    total += size;
                }
                let passes = threshold_passes(cfg, size);
                if cfg.all && passes && file_depth_ok {
                    print_row(cfg, size, &child_rel, child_time, zone, out);
                }
            }
        }
        let passes = threshold_passes(cfg, total);
        let within_depth = cfg.max_depth.map(|md| depth <= md).unwrap_or(true);
        if (!cfg.separate_dirs || passes) && within_depth && (passes || total == 0) {
            print_row(cfg, total, display, latest, zone, out);
        }
        return Walked {
            size: total,
            latest,
        };
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
    let time = if cfg.show_time {
        entry_time(cfg, path)
    } else {
        None
    };
    let passes = threshold_passes(cfg, size);
    if passes {
        print_row(cfg, size, display, time, zone, out);
    }
    Walked { size, latest: time }
}

fn threshold_passes(cfg: &DuConfig, size: u64) -> bool {
    match cfg.threshold {
        None => true,
        Some(t) if t >= 0 => size >= t as u64,
        Some(t) => size <= (-t) as u64,
    }
}

/// The printed spelling of an operand (#1174/#1364 contract, matching the
/// 1.x applet): a POSIX drive spelling ("/c/...") prints as the native
/// forward-slash path it resolves to, and a run of trailing separators
/// collapses to one ("src//" prints like "src/"). Relatives keep the
/// user's spelling.
fn display_operand(ctx: &Ctx, operand: &str) -> String {
    let base = if operand.starts_with('/') && operand != "/" {
        let resolved = resolve(ctx, operand);
        resolved.to_string_lossy().replace('\\', "/")
    } else {
        operand.replace('\\', "/")
    };
    let trimmed = base.trim_end_matches('/');
    if trimmed.len() != base.len() && !trimmed.is_empty() {
        format!("{trimmed}/")
    } else {
        base
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
    time: Option<i64>,
    zone: &crate::date::TimeZone,
    out: &mut dyn FnMut(u64, &str, bool, &DuConfig),
) {
    if cfg.show_time {
        let epoch = time.unwrap_or(0);
        let (off, name) = zone.at(epoch);
        let dt = crate::date::DateTime::from_epoch(epoch, off, name);
        let text = strftime(&dt, &style_format(cfg));
        out(size, &format!("{text}\t{display}"), cfg.nul, cfg);
    } else {
        out(size, display, cfg.nul, cfg);
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

/// Identity of the link object itself, never followed (#1670: keeping `-P`
/// link entries distinct from the directory they point at).
fn link_handle(path: &Path) -> Option<(u64, u64)> {
    imp::link_handle(path)
}

/// Lexical key for an UNFOLLOWED directory: the spelling's components with
/// `.`/`..` folded, separators normalized and case folded. It never touches
/// the filesystem, so a junction/symlink and its target keep DIFFERENT keys
/// under `-P` — the 1.x `du_dir_key` contract (#1288/#1670).
fn lexical_dir_key(path: &Path) -> String {
    use std::path::Component;
    let mut parts: Vec<String> = Vec::new();
    for component in path.components() {
        match component {
            Component::CurDir => {}
            Component::ParentDir => {
                parts.pop();
            }
            Component::RootDir => parts.push(String::new()),
            Component::Prefix(prefix) => {
                parts.push(prefix.as_os_str().to_string_lossy().replace('/', "\\"))
            }
            Component::Normal(name) => parts.push(name.to_string_lossy().into_owned()),
        }
    }
    parts.join("\\").to_uppercase()
}

/// The directory a DrvFs LX link points at, resolved from its stored target
/// (#1748). Win32 follows native symlinks and junctions for `metadata()`,
/// but an LX reparse point (what WSL `ln -s` writes on /mnt) is opaque to
/// the kernel and to `read_dir()`, so `-L` must descend on this path.
/// `None` for native links, dangling links and non-directory targets.
fn followed_lx_dir_target(ctx: &Ctx, path: &Path) -> Option<PathBuf> {
    let text = path.to_string_lossy().into_owned();
    let target = crate::fs_win::read_lx_symlink_target(&text)?;
    let resolved = PathBuf::from(crate::fs_win::resolve_symlink_target(ctx, &text, &target));
    std::fs::metadata(&resolved)
        .ok()
        .filter(|m| m.is_dir())
        .map(|_| resolved)
}

/// Identity key of the directory a FOLLOWED path names (#1670, GNU du.c
/// hash_all: `-L` hashes every entry by (st_dev, st_ino) and drops a pair
/// already seen at du.c:546-549). CreateFileW follows native symlinks and
/// junctions; a DrvFs LX link is opaque to the kernel, so its stored target
/// is resolved first. `None` when neither works (the caller falls back to
/// the lexical key).
fn followed_dir_key(ctx: &Ctx, path: &Path) -> Option<String> {
    if let Some((volume, index)) = file_handle(path) {
        return Some(format!("id:{volume}:{index}"));
    }
    let text = path.to_string_lossy().into_owned();
    let target = crate::fs_win::read_lx_symlink_target(&text)?;
    let resolved = crate::fs_win::resolve_symlink_target(ctx, &text, &target);
    let (volume, index) = file_handle(Path::new(&resolved))?;
    Some(format!("id:{volume}:{index}"))
}

/// Visited-directory key under the active dereference policy: the followed
/// target's identity for -L/-H, the lexical spelling otherwise.
fn visited_dir_key(ctx: &Ctx, path: &Path, follow: bool) -> String {
    if follow {
        if let Some(key) = followed_dir_key(ctx, path) {
            return key;
        }
    }
    lexical_dir_key(path)
}

/// Parse a SIZE argument exactly like the 1.x applet's parse_block_size
/// (du.cpp:163-260, a port of gnulib's human_options/xstrtoumax pairing):
/// the suffix table is tried longest-first, a BARE suffix means one unit,
/// and a bare suffix also selects the printed unit letter (`-BM` prints
/// "2M" while `-B1M` prints "2"). Returns (multiplier, display suffix).
fn parse_block_size(raw: &str) -> Option<(u64, String)> {
    if raw.is_empty() {
        return None;
    }
    const UNITS: [(&str, u64); 38] = [
        ("KiB", 1 << 10),
        ("MiB", 1 << 20),
        ("GiB", 1 << 30),
        ("TiB", 1u64 << 40),
        ("PiB", 1u64 << 50),
        ("EiB", 1u64 << 60),
        ("KB", 1000),
        ("MB", 1_000_000),
        ("GB", 1_000_000_000),
        ("TB", 1_000_000_000_000),
        ("PB", 1_000_000_000_000_000),
        ("EB", 1_000_000_000_000_000_000),
        ("kiB", 1 << 10),
        ("miB", 1 << 20),
        ("giB", 1 << 30),
        ("tiB", 1u64 << 40),
        ("piB", 1u64 << 50),
        ("eiB", 1u64 << 60),
        ("kb", 1000),
        ("mb", 1_000_000),
        ("gb", 1_000_000_000),
        ("tb", 1_000_000_000_000),
        ("pb", 1_000_000_000_000_000),
        ("eb", 1_000_000_000_000_000_000),
        ("K", 1 << 10),
        ("M", 1 << 20),
        ("G", 1 << 30),
        ("T", 1u64 << 40),
        ("P", 1u64 << 50),
        ("E", 1u64 << 60),
        ("k", 1 << 10),
        ("m", 1 << 20),
        ("g", 1 << 30),
        ("t", 1u64 << 40),
        ("p", 1u64 << 50),
        ("e", 1u64 << 60),
        ("b", 512),
        ("B", 1),
    ];
    let mut multiplier = 1u64;
    let mut number = raw;
    let mut matched_suffix: Option<&str> = None;
    for (suffix, mult) in UNITS {
        if raw.len() >= suffix.len() && raw.ends_with(suffix) {
            multiplier = mult;
            number = &raw[..raw.len() - suffix.len()];
            matched_suffix = Some(suffix);
            break;
        }
    }
    let value: u64 = if number.is_empty() {
        if matched_suffix.is_none() {
            return None;
        }
        1
    } else {
        number.parse().ok()?
    };
    if value == 0 {
        return None;
    }
    let display = match matched_suffix {
        Some(suffix) if number.is_empty() => suffix.to_string(),
        _ => String::new(),
    };
    Some((value.checked_mul(multiplier)?, display))
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
        display_suffix: String::new(),
        human: false,
        si: false,
        inodes: p.has("inodes"),
        all: p.has("all"),
        separate_dirs: p.has("separate-dirs"),
        max_depth: None,
        total: p.has("total"),
        count_links: p.has("count-links"),
        one_fs: p.has("one-file-system"),
        threshold: None,
        time_mode: TimeMode::Modification,
        show_time: false,
        time_style: "long-iso".to_string(),
        excludes: Vec::new(),
        nul: p.has("null"),
        deref_all: p.has("dereference"),
        deref_args: p.has("dereference-command-line"),
    };

    // [GNU] The size options are ONE sequential state: -b/-k/-m/-h/--si and
    // -B/--block-size each reset the previous choice, so the LAST one wins
    // (`-h -B1` scales by 1; `-B1 -h` prints human-readable). The pre-wiring
    // port applied them in a fixed order, so `-h -B1` stayed human-readable.
    let mut size_explicit = false;
    for event in &p.events {
        match event.key.as_str() {
            "bytes" | "k" | "m" | "human-readable" | "si" => {
                cfg.human = event.key == "human-readable";
                cfg.si = event.key == "si";
                cfg.display_suffix.clear();
                cfg.block_size = match event.key.as_str() {
                    "bytes" => 1,
                    "k" => 1024,
                    "m" => 1024 * 1024,
                    _ => cfg.block_size,
                };
                size_explicit = true;
            }
            "block-size" => {
                let raw = event.value.as_deref().unwrap_or("");
                let parsed = match raw {
                    "human-readable" => {
                        cfg.human = true;
                        cfg.si = false;
                        cfg.display_suffix.clear();
                        size_explicit = true;
                        Some(1)
                    }
                    "si" => {
                        cfg.human = false;
                        cfg.si = true;
                        cfg.display_suffix.clear();
                        size_explicit = true;
                        Some(1)
                    }
                    _ => parse_block_size(raw).map(|(mult, suffix)| {
                        cfg.human = false;
                        cfg.si = false;
                        cfg.block_size = mult;
                        cfg.display_suffix = suffix;
                        size_explicit = true;
                        mult
                    }),
                };
                if parsed.is_none() {
                    // [GNU] the diagnostic names the spelling the user used
                    // (`-B bogus` -> "-B", `--block-size=bogus` ->
                    // "--block-size").
                    let opt = &event.token;
                    let _ = writeln!(
                        io.stderr,
                        "du: {}",
                        tr!(
                            ctx,
                            "command.du.error.invalid_block_size",
                            "invalid {option} argument '{size}'",
                            option = opt,
                            size = raw
                        )
                    );
                    return Ok(1);
                }
            }
            _ => {}
        }
    }
    if cfg.inodes {
        cfg.block_size = 1;
        cfg.display_suffix.clear();
    }
    // [GNU] With no size option, the first set of DU_BLOCK_SIZE/BLOCK_SIZE/
    // BLOCKSIZE decides (an unparseable first value is ignored as a whole);
    // POSIXLY_CORRECT alone makes the default 512 bytes.
    if !size_explicit {
        for name in ["DU_BLOCK_SIZE", "BLOCK_SIZE", "BLOCKSIZE"] {
            let Some(value) =
                winux_core::win32_env_get(ctx.env, name).map(|v| v.to_string_lossy().into_owned())
            else {
                continue;
            };
            if value == "human-readable" {
                cfg.human = true;
            } else if value == "si" {
                cfg.si = true;
            } else if let Some((mult, _)) = parse_block_size(&value) {
                cfg.block_size = mult;
            }
            break;
        }
        if !cfg.human && !cfg.si && ctx.env.get("POSIXLY_CORRECT").is_some() {
            cfg.block_size = 512;
        }
    }

    // [GNU] --time[=WORD]: bare stays mtime, the last occurrence wins;
    // atime/access/use -> access, ctime/status -> status, mtime stays the
    // default (argmatch unique-prefix rules).
    let time_word = |value: &str| -> Option<TimeMode> {
        const WORDS: [(&str, TimeMode); 5] = [
            ("atime", TimeMode::Access),
            ("access", TimeMode::Access),
            ("use", TimeMode::Access),
            ("ctime", TimeMode::Status),
            ("status", TimeMode::Status),
        ];
        if value.is_empty() {
            return Some(TimeMode::Modification);
        }
        let mut matched: Option<TimeMode> = None;
        let mut count = 0usize;
        for (word, mode) in WORDS {
            if word == value {
                return Some(mode);
            }
            if word.starts_with(value) {
                matched = Some(mode);
                count += 1;
            }
        }
        if count == 1 {
            matched
        } else {
            None
        }
    };
    for (_, value) in &time_values {
        cfg.show_time = true;
        match time_word(value) {
            Some(mode) => cfg.time_mode = mode,
            None => {
                let _ = writeln!(io.stderr, "du: invalid argument '{value}' for '--time'");
                let _ = writeln!(io.stderr, "Valid arguments are:");
                let _ = writeln!(io.stderr, "  - 'atime', 'access', 'use'");
                let _ = writeln!(io.stderr, "  - 'ctime', 'status'");
                let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "du"));
                return Ok(1);
            }
        }
    }
    if p.has("time") {
        cfg.show_time = true;
    }
    if p.has("inodes") && cfg.apparent {
        let _ = writeln!(
            io.stderr,
            "du: {}",
            tr!(
                ctx,
                "command.du.warning.inodes_with_apparent",
                "warning: options --apparent-size and -b are ineffective with --inodes"
            )
        );
    }
    if let Some(raw) = p.value_of("max-depth") {
        match raw.parse::<usize>() {
            Ok(v) => cfg.max_depth = Some(v),
            Err(_) => {
                let _ = writeln!(
                    io.stderr,
                    "du: {}",
                    tr!(
                        ctx,
                        "command.du.error.invalid_max_depth",
                        "invalid maximum depth '{depth}' (expected an integer 0 or larger)",
                        depth = raw
                    )
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
                let _ = writeln!(
                    io.stderr,
                    "du: {}",
                    tr!(
                        ctx,
                        "command.du.error.invalid_threshold",
                        "invalid --threshold argument '{size}'",
                        size = raw
                    )
                );
                return Ok(1);
            }
        }
    }
    if let Some(style) = p.value_of("time-style") {
        cfg.time_style = style.to_string();
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
                let _ = writeln!(
                    io.stderr,
                    "du: {}",
                    tr!(
                        ctx,
                        "command.du.error.exclude_from_open",
                        "{file}: {reason}",
                        file = file,
                        reason = reason
                    )
                );
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
                "{}",
                tr!(
                    ctx,
                    "command.du.error.files0_with_operands",
                    "file operands cannot be combined with --files0-from"
                )
            );
            let _ = writeln!(io.stderr, "{}", winux_core::try_help(ctx, "du"));
            return Ok(1);
        }
        let data = if list == "-" {
            let mut buf = Vec::new();
            if io.stdin.read_to_end(&mut buf).is_err() {
                let _ = writeln!(
                    io.stderr,
                    "du: {}",
                    tr!(ctx, "command.du.error.stdin_read", "-: read error")
                );
                return Ok(1);
            }
            String::from_utf8_lossy(&buf).into_owned()
        } else {
            let path = resolve(ctx, list);
            match std::fs::read(&path) {
                Ok(bytes) => String::from_utf8_lossy(&bytes).into_owned(),
                Err(e) => {
                    let reason = crate::fsutil::reason(&e);
                    let _ = writeln!(
                        io.stderr,
                        "du: {}",
                        tr!(
                            ctx,
                            "command.du.error.cannot_open",
                            "cannot open '{file}' for reading: {reason}",
                            file = list,
                            reason = reason
                        )
                    );
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
                    "du: {}",
                    tr!(
                        ctx,
                        "command.du.error.zero_length_name",
                        "{file}:{line}: invalid zero-length file name",
                        file = list,
                        line = i + 1
                    )
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
    // Directories visited in this invocation: lexical keys under -P, the
    // followed target's (volume, index) under -L/-H (#1670).
    let mut visited: HashSet<String> = HashSet::new();
    for operand in &operands {
        let path = resolve(ctx, operand);
        let mut walk_path = path.clone();
        if std::fs::symlink_metadata(&path).is_err() {
            let _ = writeln!(
                io.stderr,
                "du: {}",
                tr!(
                    ctx,
                    "command.du.error.cannot_access_missing",
                    "cannot access '{operand}': No such file or directory",
                    operand = operand
                )
            );
            failed = true;
            continue;
        }
        // [GNU] Under -L/-H a link operand must resolve to a LIVE target
        // (#1656, coreutils du.c fts with FTS_LOGICAL): a dangling link is
        // reported without errno text and the run fails, while a plain
        // missing path keeps ": No such file or directory". Windows cannot
        // traverse WSL DrvFs LX reparse points, so a live LX target is
        // resolved here and the walk runs on the target while every printed
        // row keeps the operand spelling — exactly like GNU's dereference.
        // A native symlink/junction resolves inside the kernel
        // (std::fs::metadata follows reparse points).
        if cfg.deref_all || cfg.deref_args {
            let live = std::fs::metadata(&path).is_ok();
            if !live {
                let api = crate::fs_win::imp::api_path(ctx, operand);
                let mut resolved_target = None;
                if let Some(target) = crate::fs_win::read_lx_symlink_target(&api) {
                    let resolved = crate::fs_win::resolve_symlink_target(ctx, &api, &target);
                    if std::fs::metadata(&resolved).is_ok() {
                        resolved_target = Some(PathBuf::from(resolved));
                    }
                }
                match resolved_target {
                    Some(target) => walk_path = target,
                    None => {
                        let _ = writeln!(
                            io.stderr,
                            "du: {}",
                            tr!(
                                ctx,
                                "command.du.error.cannot_access_dangling",
                                "cannot access '{operand}'",
                                operand = operand
                            )
                        );
                        failed = true;
                        continue;
                    }
                }
            }
        }
        // [GNU] Operand dedup has two faces (#1670, du.c:1042/546-549):
        // a DIRECTORY enters the visited set keyed by the followed target's
        // identity under -L/-H and by the lexical spelling otherwise, so
        // `-L dirlink dir` prints once under the first spelling while `-P`
        // keeps the link and the real directory distinct; a FILE operand is
        // hashed by its inode, and an unfollowed link by the LINK's own
        // identity. --count-links disables both.
        let operand_is_unfollowed_link = std::fs::symlink_metadata(&path)
            .map(|m| m.file_type().is_symlink())
            .unwrap_or(false)
            && !cfg.deref_all
            && !cfg.deref_args;
        let operand_is_dir = !operand_is_unfollowed_link
            && (std::fs::metadata(&walk_path)
                .map(|m| m.is_dir())
                .unwrap_or(false)
                || ((cfg.deref_all || cfg.deref_args)
                    && followed_lx_dir_target(ctx, &path).is_some()));
        if !cfg.count_links {
            if operand_is_dir {
                let key = visited_dir_key(ctx, &walk_path, cfg.deref_all || cfg.deref_args);
                if !visited.insert(key) {
                    continue;
                }
            } else {
                let key = if operand_is_unfollowed_link {
                    link_handle(&path)
                } else {
                    file_handle(&walk_path)
                };
                if let Some(k) = key {
                    if !seen.insert(k) {
                        continue;
                    }
                }
            }
        }
        let mut volume_root = None;
        let mut sink = |size: u64, tail: &str, nul: bool, cfg: &DuConfig| {
            let num = display_size(cfg, size);
            let _ = if nul {
                write!(io.stdout, "{num}\t{tail}\0")
            } else {
                writeln!(io.stdout, "{num}\t{tail}")
            };
        };
        let operand_display = display_operand(ctx, operand);
        let sub = walk(
            ctx,
            &cfg,
            &operand_display,
            &walk_path,
            0,
            &mut volume_root,
            &mut seen,
            &mut visited,
            &zone,
            &mut sink,
            &mut summaries,
        );
        summaries.grand += sub.size;
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

    /// WSL's Windows-volume link shape: an LX reparse point (tag
    /// 0xA000001D: u32 version 2, then the UTF-8 target). Writing it needs
    /// no SeCreateSymbolicLinkPrivilege, so the exact object `ln -s`
    /// produces is constructible here (#1670; the C++ helper
    /// du_test_create_lx_symlink is the same buffer).
    #[cfg(windows)]
    fn create_lx_symlink(link: &std::path::Path, target: &str) -> bool {
        use std::ffi::c_void;
        #[link(name = "kernel32")]
        extern "system" {
            fn CreateFileW(
                name: *const u16,
                access: u32,
                share: u32,
                security: *mut c_void,
                disposition: u32,
                flags: u32,
                template: *mut c_void,
            ) -> *mut c_void;
            fn DeviceIoControl(
                handle: *mut c_void,
                code: u32,
                in_buf: *mut c_void,
                in_size: u32,
                out_buf: *mut c_void,
                out_size: u32,
                returned: *mut u32,
                overlapped: *mut c_void,
            ) -> i32;
            fn CloseHandle(handle: *mut c_void) -> i32;
        }
        const GENERIC_WRITE: u32 = 0x4000_0000;
        const SHARE_ALL: u32 = 0x7;
        const CREATE_NEW: u32 = 1;
        const BACKUP_SEMANTICS: u32 = 0x0200_0000;
        const OPEN_REPARSE_POINT: u32 = 0x0020_0000;
        // FSCTL_SET_REPARSE_POINT: CTL_CODE(FILE_DEVICE_FILE_SYSTEM, 41,
        // METHOD_BUFFERED, FILE_ANY_ACCESS).
        const FSCTL_SET_REPARSE_POINT: u32 = 0x0009_00A4;
        const TAG: u32 = 0xA000_001D;

        let wide: Vec<u16> = link
            .to_string_lossy()
            .encode_utf16()
            .chain(std::iter::once(0))
            .collect();
        let handle = unsafe {
            CreateFileW(
                wide.as_ptr(),
                GENERIC_WRITE,
                SHARE_ALL,
                std::ptr::null_mut(),
                CREATE_NEW,
                BACKUP_SEMANTICS | OPEN_REPARSE_POINT,
                std::ptr::null_mut(),
            )
        };
        if handle as isize == -1 || handle.is_null() {
            return false;
        }
        let data_len = 4 + target.len();
        let mut buffer = vec![0u8; 8 + data_len];
        buffer[0..4].copy_from_slice(&TAG.to_le_bytes());
        buffer[4..6].copy_from_slice(&(data_len as u16).to_le_bytes());
        buffer[8..12].copy_from_slice(&2u32.to_le_bytes());
        buffer[12..].copy_from_slice(target.as_bytes());
        let mut returned: u32 = 0;
        let ok = unsafe {
            DeviceIoControl(
                handle,
                FSCTL_SET_REPARSE_POINT,
                buffer.as_mut_ptr() as *mut c_void,
                buffer.len() as u32,
                std::ptr::null_mut(),
                0,
                &mut returned,
                std::ptr::null_mut(),
            )
        };
        unsafe { CloseHandle(handle) };
        ok != 0
    }

    /// A directory junction via `cmd /c mklink /J` (no privilege needed).
    #[cfg(windows)]
    fn create_junction(link: &std::path::Path, target: &std::path::Path) -> bool {
        std::process::Command::new("cmd")
            .args(["/C", "mklink", "/J"])
            .arg(link)
            .arg(target)
            .stdout(std::process::Stdio::null())
            .stderr(std::process::Stdio::null())
            .status()
            .map(|s| s.success())
            .unwrap_or(false)
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
    fn dereference_operand_liveness_matches_gnu_1656() {
        // #1656 (old-exe parity): -L/-H probe the link's TARGET. A dangling
        // link reports "cannot access 'X'" with NO errno text and rc 1; a
        // plain missing path keeps ": No such file or directory"; default
        // du lstats the link and prints its zero-size row.
        let sc = ScratchCtx::new("du-deref");
        std::fs::write(sc.path("gone.txt"), "x").unwrap();
        #[cfg(windows)]
        let linked = std::os::windows::fs::symlink_file("gone.txt", sc.path("dangling")).is_ok();
        #[cfg(not(windows))]
        let linked = std::os::unix::fs::symlink("gone.txt", sc.path("dangling")).is_ok();
        if !linked {
            return; // host without symlink capability (same skip as ctest)
        }
        std::fs::remove_file(sc.path("gone.txt")).unwrap();

        let (r, out, err) = run(&sc, &["-L", "dangling"]);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert_eq!(err, "du: cannot access 'dangling'\n");

        let (r, _, err) = run(&sc, &["-H", "dangling"]);
        assert_eq!(r, 1);
        assert_eq!(err, "du: cannot access 'dangling'\n");

        let (r, out, err) = run(&sc, &["dangling"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.ends_with("\tdangling\n"));

        let (r, _, err) = run(&sc, &["-L", "missing"]);
        assert_eq!(r, 1);
        assert_eq!(
            err,
            "du: cannot access 'missing': No such file or directory\n"
        );
    }

    #[test]
    fn dereference_directory_link_operand_walks_the_target() {
        // -L/-H on a live directory link: rows carry the OPERAND spelling
        // while the walk follows the target (#1656).
        let sc = ScratchCtx::new("du-deref-dir");
        std::fs::create_dir_all(sc.path("dir")).unwrap();
        std::fs::write(sc.path("dir/f.txt"), "x").unwrap();
        // A junction is the unprivileged Windows analog of a directory
        // symlink (mklink /J); the CI runner is admin but developers are
        // not, so the fixture must not depend on SeCreateSymbolicLink.
        #[cfg(windows)]
        let linked = std::process::Command::new("cmd")
            .args(["/c", "mklink", "/J"])
            .arg(sc.path("dirlink"))
            .arg(sc.path("dir"))
            .output()
            .map(|o| o.status.success())
            .unwrap_or(false);
        #[cfg(not(windows))]
        let linked = std::os::unix::fs::symlink("dir", sc.path("dirlink")).is_ok();
        if !linked {
            return;
        }
        for flag in ["-L", "-H"] {
            // -a is required to see per-file rows (du -b alone prints only
            // the directory total, probed GNU 9.4).
            let (r, out, err) = run(&sc, &["-a", "-b", flag, "dirlink"]);
            assert_eq!(r, 0, "{flag}: {err}");
            assert!(err.is_empty(), "{flag}: {err}");
            assert!(out.contains("dirlink/f.txt\n"), "{flag}: {out}");
            assert!(out.ends_with("\tdirlink\n"), "{flag}: {out}");
        }
    }

    #[test]
    fn dereference_lx_directory_link_operand_walks_target_1748() {
        // #1748: an LX directory link (WSL `ln -s` on /mnt, no privilege
        // needed) as the OPERAND. Pre-fix the walk could see a directory
        // in metadata() but fail read_dir(), printing only the total row.
        let sc = ScratchCtx::new("du-1748-operand");
        std::fs::create_dir_all(sc.path("d1")).unwrap();
        std::fs::write(sc.path("d1/f"), "ab").unwrap();
        #[cfg(windows)]
        if !create_lx_symlink(&sc.path("dirlink"), "d1") {
            eprintln!("SKIP: LX reparse ioctl unavailable");
            return;
        }
        #[cfg(not(windows))]
        std::os::unix::fs::symlink("d1", sc.path("dirlink")).unwrap();
        for flag in ["-L", "-H"] {
            let (r, out, err) = run(&sc, &["-a", "-b", flag, "dirlink"]);
            assert_eq!(r, 0, "{flag}: {err}");
            assert!(err.is_empty(), "{flag}: {err}");
            assert_eq!(out, "2\tdirlink/f\n2\tdirlink\n", "{flag}: {out}");
        }
    }

    #[test]
    fn dereference_lx_child_dir_link_walks_target_1748() {
        // #1748: a directory link INSIDE a walked tree. Win32 follows
        // native links but a DrvFs LX link is opaque to `read_dir()`; the
        // pre-fix row was `0 tree/rel` and the subtree never appeared.
        let sc = ScratchCtx::new("du-1748-child");
        std::fs::create_dir_all(sc.path("d1")).unwrap();
        std::fs::write(sc.path("d1/f"), "ab").unwrap();
        std::fs::create_dir_all(sc.path("tree")).unwrap();
        #[cfg(windows)]
        if !create_lx_symlink(&sc.path("tree/rel"), "../d1") {
            eprintln!("SKIP: LX reparse ioctl unavailable");
            return;
        }
        #[cfg(not(windows))]
        std::os::unix::fs::symlink("../d1", sc.path("tree/rel")).unwrap();
        let (r, out, err) = run(&sc, &["-a", "-b", "-L", "tree"]);
        assert_eq!(r, 0, "{err}");
        assert!(err.is_empty(), "{err}");
        assert_eq!(out, "2\ttree/rel/f\n2\ttree/rel\n2\ttree\n");
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

    // ---- #1670: followed-directory identity dedup (PR #1730 parity) ----

    #[test]
    fn followed_lx_dir_operand_dedups_against_real_dir_1670() {
        let sc = ScratchCtx::new("du-1670-lx");
        std::fs::create_dir_all(sc.path("dir/sub")).unwrap();
        std::fs::write(sc.path("dir/f.txt"), "x").unwrap();
        #[cfg(windows)]
        if !create_lx_symlink(&sc.path("dirlink"), "dir") {
            eprintln!("SKIP: LX reparse ioctl unavailable");
            return;
        }
        for flag in ["-L", "-H"] {
            let (r, out, err) = run(&sc, &[flag, "-s", "dirlink", "dir"]);
            assert_eq!(r, 0, "{flag}: {err}");
            assert!(err.is_empty(), "{flag}: {err}");
            assert_eq!(out, "4\tdirlink\n", "{flag}");

            let (r, out, err) = run(&sc, &[flag, "-s", "dir", "dirlink"]);
            assert_eq!(r, 0, "{flag}: {err}");
            assert!(err.is_empty(), "{flag}: {err}");
            assert_eq!(out, "4\tdir\n", "{flag}");
        }
    }

    #[test]
    fn no_dereference_lx_link_and_dir_stay_distinct_1670() {
        let sc = ScratchCtx::new("du-1670-p");
        std::fs::create_dir_all(sc.path("dir/sub")).unwrap();
        std::fs::write(sc.path("dir/f.txt"), "x").unwrap();
        #[cfg(windows)]
        if !create_lx_symlink(&sc.path("dirlink"), "dir") {
            eprintln!("SKIP: LX reparse ioctl unavailable");
            return;
        }
        let (r, out, _) = run(&sc, &["-P", "-s", "dirlink", "dir"]);
        assert_eq!(r, 0);
        assert_eq!(out, "0\tdirlink\n4\tdir\n");

        let (r, out, _) = run(&sc, &["-P", "-s", "dir", "dirlink"]);
        assert_eq!(r, 0);
        assert_eq!(out, "4\tdir\n0\tdirlink\n");
    }

    #[test]
    fn followed_junction_dir_operand_dedups_against_real_dir_1670() {
        let sc = ScratchCtx::new("du-1670-junction");
        std::fs::create_dir_all(sc.path("dir/sub")).unwrap();
        std::fs::write(sc.path("dir/f.txt"), "x").unwrap();
        #[cfg(windows)]
        if !create_junction(&sc.path("dirlink"), &sc.path("dir")) {
            eprintln!("SKIP: mklink /J failed");
            return;
        }
        for flag in ["-L", "-H"] {
            let (r, out, err) = run(&sc, &[flag, "-s", "dirlink", "dir"]);
            assert_eq!(r, 0, "{flag}: {err}");
            assert_eq!(out, "4\tdirlink\n", "{flag}");

            let (r, out, err) = run(&sc, &[flag, "-s", "dir", "dirlink"]);
            assert_eq!(r, 0, "{flag}: {err}");
            assert_eq!(out, "4\tdir\n", "{flag}");
        }
    }

    #[test]
    fn count_links_keeps_both_followed_dir_operands_1670() {
        let sc = ScratchCtx::new("du-1670-l");
        std::fs::create_dir_all(sc.path("dir/sub")).unwrap();
        std::fs::write(sc.path("dir/f.txt"), "x").unwrap();
        #[cfg(windows)]
        if !create_lx_symlink(&sc.path("dirlink"), "dir") {
            eprintln!("SKIP: LX reparse ioctl unavailable");
            return;
        }
        let (r, out, err) = run(&sc, &["-L", "-l", "-s", "dirlink", "dir"]);
        assert_eq!(r, 0, "{err}");
        assert_eq!(out, "4\tdirlink\n4\tdir\n");
    }
}
