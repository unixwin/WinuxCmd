//! One visited file-system entry with lazily fetched metadata (GNU's
//! "need_type / need_stat" split: the enumeration record answers the
//! common predicates, a handle open is paid only when a predicate needs
//! what the record lacks).

use std::cell::OnceCell;

use super::winfs::{self, FindData, HandleInfo};

/// A timestamp: seconds since the Unix epoch plus nanoseconds.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, PartialOrd, Ord)]
pub struct Ts {
    pub sec: i64,
    pub nsec: u32,
}

impl Ts {
    /// FILETIME ticks (100 ns since 1601) -> epoch time.
    pub fn from_ticks(t: i64) -> Ts {
        let d = t - 116_444_736_000_000_000;
        Ts {
            sec: d.div_euclid(10_000_000),
            nsec: (d.rem_euclid(10_000_000) * 100) as u32,
        }
    }

    /// Total nanoseconds (comparison domain of the time predicates).
    pub fn ns(self) -> i128 {
        self.sec as i128 * 1_000_000_000 + self.nsec as i128
    }
}

/// POSIX file type of an entry (GNU `S_IFMT` classes the platform has).
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    Reg,
    Dir,
    Lnk,
    Chr,
    Fifo,
}

impl Kind {
    /// `%y` / `-type` letter.
    pub fn letter(self) -> u8 {
        match self {
            Kind::Reg => b'f',
            Kind::Dir => b'd',
            Kind::Lnk => b'l',
            Kind::Chr => b'c',
            Kind::Fifo => b'p',
        }
    }
}

/// Executable-extension heuristic of the Windows mode model (1.x C++
/// find.cpp permission_mode / win32_executable, plus `.com` which the C++
/// -executable test already honored).
pub fn exec_ext(name: &[u8]) -> bool {
    let dot = match name.iter().rposition(|&b| b == b'.') {
        Some(d) => d,
        None => return false,
    };
    let ext = &name[dot + 1..];
    ["exe", "com", "bat", "cmd", "ps1"]
        .iter()
        .any(|e| ext.eq_ignore_ascii_case(e.as_bytes()))
}

/// Classify a record: name-surrogate reparse points are symlinks (the
/// C++ find treats junctions the same, `is_directory_reparse_point`),
/// device attributes are character devices, a SYSTEM 7-byte file holding
/// the WinuxCmd fifo marker is a fifo.
pub fn classify(attrs: u32, tag: u32, size: u64, native: &[u16]) -> Kind {
    if attrs & winfs::FILE_ATTRIBUTE_REPARSE_POINT != 0 && winfs::is_link_tag(tag) {
        Kind::Lnk
    } else if attrs & winfs::FILE_ATTRIBUTE_DIRECTORY != 0 {
        Kind::Dir
    } else if attrs & winfs::FILE_ATTRIBUTE_DEVICE != 0 {
        Kind::Chr
    } else if attrs & winfs::FILE_ATTRIBUTE_SYSTEM != 0 && size == 7 {
        let s = String::from_utf16_lossy(native);
        if crate::fs_win::is_winux_fifo_path(&s) {
            Kind::Fifo
        } else {
            Kind::Reg
        }
    } else {
        Kind::Reg
    }
}

/// The record-level (cheap) stat view.
#[derive(Clone, Debug)]
pub struct Basic {
    pub kind: Kind,
    pub attrs: u32,
    pub size: u64,
    pub atime: Ts,
    pub mtime: Ts,
    pub btime: Ts,
}

impl Basic {
    pub fn from_find(d: &FindData, native: &[u16]) -> Basic {
        Basic {
            kind: classify(d.attrs, d.tag, d.size, native),
            attrs: d.attrs,
            size: d.size,
            atime: Ts::from_ticks(d.atime),
            mtime: Ts::from_ticks(d.mtime),
            btime: Ts::from_ticks(d.btime),
        }
    }

    pub fn from_handle(h: &HandleInfo, native: &[u16]) -> Basic {
        Basic {
            kind: classify(h.attrs, h.tag, h.size, native),
            attrs: h.attrs,
            size: h.size,
            atime: Ts::from_ticks(h.atime),
            mtime: Ts::from_ticks(h.mtime),
            btime: Ts::from_ticks(h.btime),
        }
    }

    /// The synthesized record of a DOS device operand (`NUL`, `/dev/null`).
    pub fn device() -> Basic {
        Basic {
            kind: Kind::Chr,
            attrs: winfs::FILE_ATTRIBUTE_DEVICE,
            size: 0,
            atime: Ts::default(),
            mtime: Ts::default(),
            btime: Ts::default(),
        }
    }
}

/// One entry of the walk.
pub struct Entry {
    /// Display path (GNU `pathname`): the starting point as given (with
    /// `\` shown as `/`), children joined with `/`.
    pub path: String,
    /// Native path for API calls (`\\?\` extended form, no NUL).
    pub native: Vec<u16>,
    /// Depth below the starting point (0 = the starting point).
    pub depth: usize,
    /// Length of the starting point prefix of `path` (`%H`, `%P`).
    pub root_len: usize,
    /// The effective stat follows links (-L everywhere, -H at depth 0).
    pub follow: bool,
    /// lstat-level record.
    pub lbasic: Basic,
    /// DOS device operand (no handle-level metadata).
    pub device: bool,
    lfull: OnceCell<Option<HandleInfo>>,
    followed: OnceCell<Result<(Basic, HandleInfo), u32>>,
    link: OnceCell<Option<String>>,
    sids: OnceCell<Option<(Vec<u8>, Vec<u8>)>>,
}

impl Entry {
    pub fn new(
        path: String,
        native: Vec<u16>,
        depth: usize,
        root_len: usize,
        follow: bool,
        lbasic: Basic,
    ) -> Entry {
        Entry {
            path,
            native,
            depth,
            root_len,
            follow,
            lbasic,
            device: false,
            lfull: OnceCell::new(),
            followed: OnceCell::new(),
            link: OnceCell::new(),
            sids: OnceCell::new(),
        }
    }

    pub fn device(path: String, native: Vec<u16>, root_len: usize) -> Entry {
        let mut e = Entry::new(path, native, 0, root_len, false, Basic::device());
        e.device = true;
        e
    }

    /// Prime the lstat-level handle snapshot (a root probed via its handle).
    pub fn set_lfull(&self, h: HandleInfo) {
        let _ = self.lfull.set(Some(h));
    }

    /// The followed (stat) view of a link; `Err(win32)` for a dangling or
    /// looping link.
    pub fn followed(&self) -> Result<&(Basic, HandleInfo), u32> {
        self.followed
            .get_or_init(|| {
                winfs::handle_info(&self.native, true)
                    .map(|h| (Basic::from_handle(&h, &self.native), h))
            })
            .as_ref()
            .map_err(|e| *e)
    }

    /// Does the effective stat use the followed view?
    fn use_followed(&self) -> bool {
        self.follow && self.lbasic.kind == Kind::Lnk && self.followed().is_ok()
    }

    /// The effective stat record (GNU `stat_buf` under -P/-H/-L; a broken
    /// link under -L falls back to the link itself like optionl_stat).
    pub fn st(&self) -> &Basic {
        if self.use_followed() {
            &self.followed().unwrap().0
        } else {
            &self.lbasic
        }
    }

    pub fn kind(&self) -> Kind {
        self.st().kind
    }

    /// The effective handle-level snapshot.
    pub fn full(&self) -> Option<&HandleInfo> {
        if self.device {
            return None;
        }
        if self.use_followed() {
            return Some(&self.followed().unwrap().1);
        }
        self.lfull
            .get_or_init(|| winfs::handle_info(&self.native, false).ok())
            .as_ref()
    }

    /// Raw symlink target text (lstat-level), `None` for non-links.
    pub fn link_target(&self) -> Option<&str> {
        if self.lbasic.kind != Kind::Lnk {
            return None;
        }
        self.link
            .get_or_init(|| {
                let os: std::ffi::OsString =
                    std::os::windows::ffi::OsStringExt::from_wide(&self.native);
                std::fs::read_link(std::path::PathBuf::from(os))
                    .ok()
                    .map(|t| {
                        let s = t.to_string_lossy().into_owned();
                        let s = if let Some(r) = s.strip_prefix(r"\\?\UNC\") {
                            format!(r"\\{r}")
                        } else if let Some(r) = s.strip_prefix(r"\\?\") {
                            r.to_string()
                        } else if let Some(r) = s.strip_prefix(r"\??\") {
                            r.to_string()
                        } else {
                            s
                        };
                        s.replace('\\', "/")
                    })
            })
            .as_deref()
    }

    /// GNU st_size: a symlink's size is its target length.
    pub fn size(&self) -> u64 {
        let st = self.st();
        match st.kind {
            Kind::Lnk => self.link_target().map(|t| t.len() as u64).unwrap_or(0),
            Kind::Dir => 0,
            _ => st.size,
        }
    }

    /// Creation (birth) time. The handle snapshot carries the exact
    /// FILETIME; the enumeration record can be coarser, so prefer the
    /// handle whenever one was opened (#1488 wire: the 1.x test helper
    /// reads FileBasicInfo).
    pub fn btime(&self) -> Ts {
        match self.full() {
            Some(h) => Ts::from_ticks(h.btime),
            None => self.st().btime,
        }
    }

    /// Status-change time. On the Windows value model this is the
    /// creation time (MinGW's st_ctime and the 1.x C++ find both map C
    /// here; Windows exposes no portable change time that survives
    /// SetFileTime).
    pub fn ctime(&self) -> Ts {
        self.btime()
    }

    /// Windows mode model (1.x C++ permission_mode): links 0777,
    /// directories 0755, executables 0755, other files 0644, devices 0666;
    /// the read-only attribute clears the write bits.
    pub fn mode(&self) -> u32 {
        let st = self.st();
        let ro = st.attrs & winfs::FILE_ATTRIBUTE_READONLY != 0;
        let m = match st.kind {
            Kind::Lnk => return 0o777,
            Kind::Chr => return 0o666,
            Kind::Dir => 0o755,
            Kind::Fifo => 0o644,
            Kind::Reg => {
                if exec_ext(self.base_bytes()) {
                    0o755
                } else {
                    0o644
                }
            }
        };
        if ro {
            m & !0o222
        } else {
            m
        }
    }

    /// The last path component used for extension checks.
    fn base_bytes(&self) -> &[u8] {
        let p = self.path.trim_end_matches('/').as_bytes();
        match p.iter().rposition(|&b| b == b'/') {
            Some(i) => &p[i + 1..],
            None => p,
        }
    }

    /// Owner / group SIDs.
    pub fn sids(&self) -> Option<&(Vec<u8>, Vec<u8>)> {
        if self.device {
            return None;
        }
        self.sids
            .get_or_init(|| winfs::owner_group(&self.native))
            .as_ref()
    }

    /// Is this an existing directory with no entries?
    pub fn dir_is_empty(&self) -> bool {
        match winfs::DirIter::open(&self.native) {
            Ok(mut it) => it.next().is_none(),
            Err(_) => false,
        }
    }
}
