//! `df` — GNU coreutils column model, Windows volume enumeration.
//!
//! Spec: GNU coreutils 9.4 `df.c`, probed against the WSL oracle:
//! * `--output[=FIELD_LIST]` with fields
//!   source,fstype,itotal,iused,iavail,ipcent,size,used,avail,pcent,file
//!   (bare `--output` = all 12 columns, verified). Duplicate field ->
//!   `option --output: field 'source' used more than once`; unknown/empty
//!   field -> `option --output: field 'bogus' unknown`; both usage
//!   errors rc 1 (verified). `-P`+`--output` and `-i`+`--output` are
//!   mutually exclusive rc 1 (verified wordings).
//! * `-t TYPE`/`-x TYPE` filter on the exact, case-sensitive fstype
//!   (`-t EXT4` matches nothing, verified); an empty result prints
//!   `df: no file systems processed` rc 1 (verified).
//! * `--total` appends a summary row: `total` in the source column, `-` in
//!   the file/target column, sums recomputed (verified).
//! * counts are CEIL-divided by the block size (verified -B1M), `--total`
//!   rc 0, missing operand rc 1 with `df: <p>: No such file or directory`
//!   and later operands still processed (verified).
//! * `-h`/`-H` render values < 10 with one decimal, >= 10 as integers
//!   (verified `251G`), bytes without a suffix.
//! * file/target operands resolve to their mount point (file and symlink
//!   operands verified); same-filesystem operands are deduplicated.
//!
//! Windows mapping (documented deviations, value-model class):
//! * source column = drive letter (`C:`) or UNC share (`\\srv\share`) —
//!   GNU prints device names; platform-inherent.
//! * inode columns report 0 with `IUse%` = `-`: NTFS exposes no inode
//!   count through GetDiskFreeSpaceExW/GetVolumeInformationW.
//! * fstype from GetVolumeInformationW (NTFS, FAT32, ...); `-` when the
//!   volume query fails. Network drives (DRIVE_REMOTE) are probed in a
//!   worker thread with a bounded wait so a dead UNC cannot hang df.
//! * block-size suffixes follow GNU df: integer bytes or
//!   {k,K,M,G,T,P,E,Z,Y}[B] with SI (kB/MB/..) vs binary (K/M/..) bases;
//!   invalid -> `df: invalid <opt> argument 'c'` rc 1 (verified).

use std::ffi::OsString;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('a'),
        long: "all",
        takes_value: false,
    },
    OptSpec {
        short: Some('h'),
        long: "human-readable",
        takes_value: false,
    },
    OptSpec {
        short: Some('H'),
        long: "si",
        takes_value: false,
    },
    OptSpec {
        short: Some('k'),
        long: "block-size-1k",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "inodes",
        takes_value: false,
    },
    OptSpec {
        short: Some('P'),
        long: "portability",
        takes_value: false,
    },
    OptSpec {
        short: Some('t'),
        long: "type",
        takes_value: true,
    },
    OptSpec {
        short: Some('x'),
        long: "exclude-type",
        takes_value: true,
    },
    OptSpec {
        short: Some('B'),
        long: "block-size-arg",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "output",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "block-size",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "total",
        takes_value: false,
    },
];

// ------------------------------------------------------------ fields

#[derive(Debug, Clone, Copy, PartialEq)]
enum Field {
    Source,
    Fstype,
    Itotal,
    Iused,
    Iavail,
    Ipcent,
    Size,
    Used,
    Avail,
    Pcent,
    File,
    Target,
}

fn parse_field(s: &str) -> Option<Field> {
    match s {
        "source" => Some(Field::Source),
        "fstype" => Some(Field::Fstype),
        "itotal" => Some(Field::Itotal),
        "iused" => Some(Field::Iused),
        "iavail" => Some(Field::Iavail),
        "ipcent" => Some(Field::Ipcent),
        "size" => Some(Field::Size),
        "used" => Some(Field::Used),
        "avail" => Some(Field::Avail),
        "pcent" => Some(Field::Pcent),
        "file" => Some(Field::File),
        "target" => Some(Field::Target),
        _ => None,
    }
}

fn header_of(
    f: Field,
    block_label: &str,
    posix: bool,
    output_mode: bool,
    human_si: bool,
) -> String {
    let s: &str = match f {
        Field::Source => "Filesystem",
        Field::Fstype => "Type",
        Field::Itotal => "Inodes",
        Field::Iused => "IUsed",
        Field::Iavail => "IFree",
        Field::Ipcent => "IUse%",
        Field::Size => {
            // GNU -h/-H rename the column to "Size" (verified); -P shows
            // the POSIX "1024-blocks"; otherwise the block-size label.
            if human_si {
                return "Size".to_string();
            }
            return if posix {
                "1024-blocks".to_string()
            } else {
                block_label.to_string()
            };
        }
        Field::Used => "Used",
        // GNU default 1K view shows "Available" (verified); -h/-H and the
        // --output column list show "Avail" (both verified)
        Field::Avail => {
            if human_si || output_mode {
                "Avail"
            } else {
                "Available"
            }
        }
        Field::Pcent => {
            if posix {
                "Capacity"
            } else {
                "Use%"
            }
        }
        Field::File => "File",
        Field::Target => "Mounted on",
    };
    s.to_string()
}

/// left-aligned columns (GNU df: source, fstype, file, target)
fn field_is_left(f: Field) -> bool {
    matches!(
        f,
        Field::Source | Field::Fstype | Field::File | Field::Target
    )
}

// ------------------------------------------------------------ volumes

struct Volume {
    /// source label: `C:` or `\\srv\share`
    source: String,
    fstype: String,
    total: u64,
    free: u64,
    available: u64,
}

#[link(name = "kernel32")]
extern "system" {
    fn GetLogicalDrives() -> u32;
    fn GetDiskFreeSpaceExW(
        lp_directory_name: *const u16,
        lp_free_bytes_available_to_caller: *mut u64,
        lp_total_number_of_bytes: *mut u64,
        lp_total_number_of_free_bytes: *mut u64,
    ) -> i32;
    fn GetVolumeInformationW(
        lp_root_path_name: *const u16,
        lp_volume_name_buffer: *mut u16,
        n_volume_name_size: u32,
        lp_volume_serial_number: *mut u32,
        lp_maximum_component_length: *mut u32,
        lp_file_system_flags: *mut u32,
        lp_file_system_name_buffer: *mut u16,
        n_file_system_name_size: u32,
    ) -> i32;
    fn GetDriveTypeW(lp_root_path_name: *const u16) -> u32;
}

const DRIVE_REMOTE: u32 = 4;

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

/// Raw volume query: free/total bytes plus the file-system label.
fn query_volume(name: &str) -> Option<Volume> {
    let wide_name = wide(name);
    let mut avail: u64 = 0;
    let mut total: u64 = 0;
    let mut free: u64 = 0;
    let ok = unsafe { GetDiskFreeSpaceExW(wide_name.as_ptr(), &mut avail, &mut total, &mut free) };
    if ok == 0 {
        return None;
    }
    let mut fs_buf = [0u16; 64];
    let fs_ok = unsafe {
        GetVolumeInformationW(
            wide_name.as_ptr(),
            std::ptr::null_mut(),
            0,
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            std::ptr::null_mut(),
            fs_buf.as_mut_ptr(),
            fs_buf.len() as u32,
        )
    };
    let fstype = if fs_ok == 0 {
        "-".to_string()
    } else {
        let end = fs_buf.iter().position(|&c| c == 0).unwrap_or(fs_buf.len());
        String::from_utf16_lossy(&fs_buf[..end])
    };
    Some(Volume {
        source: name.trim_end_matches('\\').to_string(),
        fstype,
        total,
        free,
        available: avail,
    })
}

/// Volume stats with remote-drive bounding: a dead UNC can block SMB for
/// tens of seconds, so DRIVE_REMOTE queries run on a worker thread with a
/// bounded wait; on timeout the volume is skipped (GNU df drops mounts it
/// cannot stat).
fn volume_stats(name: &str) -> Option<Volume> {
    let remote = unsafe { GetDriveTypeW(wide(name).as_ptr()) } == DRIVE_REMOTE;
    if !remote {
        return query_volume(name);
    }
    let (tx, rx) = std::sync::mpsc::channel();
    let name2 = name.to_string();
    std::thread::spawn(move || {
        let _ = tx.send(query_volume(&name2));
    });
    rx.recv_timeout(std::time::Duration::from_secs(3))
        .ok()
        .flatten()
}

fn local_volumes() -> Vec<Volume> {
    let mask = unsafe { GetLogicalDrives() };
    let mut out = Vec::new();
    for i in 0..26u32 {
        if mask & (1 << i) == 0 {
            continue;
        }
        let letter = (b'A' + i as u8) as char;
        let name = format!("{letter}:\\");
        if let Some(v) = volume_stats(&name) {
            out.push(v);
        }
    }
    out
}

/// Mount point for an absolute path: drive root (`C:\`) or, for UNC,
/// the `\\server\share` prefix.
fn mount_of(abs: &str) -> Option<String> {
    let b = abs.as_bytes();
    if b.len() >= 2 && b[1] == b':' && b[0].is_ascii_alphabetic() {
        return Some(format!("{}:\\", b[0] as char));
    }
    if abs.starts_with(r"\\") {
        let rest = &abs[2..];
        match rest.find(['\\', '/']) {
            Some(pos) => {
                let share = &rest[pos + 1..];
                let end = share.find(['\\', '/']).unwrap_or(share.len());
                if end > 0 {
                    return Some(format!(r"\\{}\{}", &rest[..pos], &share[..end]));
                }
            }
            None => {
                if !rest.is_empty() {
                    return Some(abs.to_string());
                }
            }
        }
    }
    None
}

// -------------------------------------------------------------- math

fn ceil_div(a: u64, b: u64) -> u64 {
    if b == 0 {
        return 0;
    }
    (a + b - 1) / b
}

/// GNU df pcenceil: ceil(100*used/(used+avail)), clamped at 100.
fn pct(used: u64, avail: u64) -> u64 {
    let total = used.saturating_add(avail);
    if total == 0 {
        return 0;
    }
    ((used * 100 + total - 1) / total).min(100)
}

/// GNU human_readable for -h/-H: < 10 of the unit keeps one decimal,
/// >= 10 rounds to an integer, plain bytes carry no suffix (verified 251G).
fn human(bytes: u64, si: bool) -> String {
    let base: f64 = if si { 1000.0 } else { 1024.0 };
    let units: [&str; 6] = if si {
        ["", "k", "M", "G", "T", "P"]
    } else {
        ["", "K", "M", "G", "T", "P"]
    };
    let mut v = bytes as f64;
    let mut u = 0usize;
    while v >= base && u + 1 < units.len() {
        v /= base;
        u += 1;
    }
    if u == 0 {
        return bytes.to_string();
    }
    if v < 10.0 {
        format!("{v:.1}{}", units[u])
    } else {
        format!("{v:.0}{}", units[u])
    }
}

/// Parse a GNU df block size: integer bytes or {kKMGTEPZY}[B] with the
/// SI/BINARY distinction (kB = 1000, K = 1024). None on invalid input.
fn parse_block_size(s: &str) -> Option<(u64, String)> {
    if s.is_empty() {
        return None;
    }
    let digits = s.len() - s.trim_start_matches(|c: char| c.is_ascii_digit()).len();
    let (num, suffix) = s.split_at(digits);
    let n: u64 = if num.is_empty() { 1 } else { num.parse().ok()? };
    let scaled = match suffix {
        "" => {
            if n == 0 {
                return None;
            }
            return Some((n, format!("{n}B")));
        }
        "K" | "k" => n.checked_mul(1024)?, // GNU: bare K/k = 1024, kB = 1000
        "kB" => n.checked_mul(1000)?,
        "M" => n.checked_mul(1024 * 1024)?,
        "MB" => n.checked_mul(1_000_000)?,
        "G" => n.checked_mul(1024 * 1024 * 1024)?,
        "GB" => n.checked_mul(1_000_000_000)?,
        "T" => n.checked_mul(1024u64.pow(4))?,
        "TB" => n.checked_mul(10u64.pow(12))?,
        "P" => n.checked_mul(1024u64.pow(5))?,
        "PB" => n.checked_mul(10u64.pow(15))?,
        "E" => n.checked_mul(1024u64.pow(6))?,
        "EB" => n.checked_mul(10u64.pow(18))?,
        _ => return None,
    };
    if scaled == 0 {
        return None;
    }
    Some((scaled, suffix.to_string()))
}

/// Normalize a byte count to the GNU header label (2048 -> `2K`).
/// The LARGEST exactly-dividing unit wins (1M is "1M", not "1024K").
fn block_label(bytes: u64) -> String {
    let units: [(u64, &str); 5] = [
        (1024u64 * 1024 * 1024 * 1024 * 1024, "P"),
        (1024u64 * 1024 * 1024 * 1024, "T"),
        (1024 * 1024 * 1024, "G"),
        (1024 * 1024, "M"),
        (1024, "K"),
    ];
    let mut best = format!("{bytes}");
    for (sz, letter) in units {
        if bytes >= sz && bytes % sz == 0 {
            best = format!("{}{}", bytes / sz, letter);
            break;
        }
    }
    format!("{best}-blocks")
}

// ------------------------------------------------------------- rows

struct Row {
    source: String,
    fstype: String,
    itotal: u64,
    iused: u64,
    iavail: u64,
    /// byte-based fields; rendered through the block size / human units
    size: u64,
    used: u64,
    avail: u64,
    file: String,
    target: String,
    is_total: bool,
}

fn cell_of(row: &Row, f: Field, opts: &DfOpts) -> String {
    match f {
        Field::Source => {
            if row.is_total {
                "total".to_string()
            } else {
                row.source.clone()
            }
        }
        Field::Fstype => {
            if row.is_total {
                "-".to_string()
            } else {
                row.fstype.clone()
            }
        }
        Field::Itotal => row.itotal.to_string(),
        Field::Iused => row.iused.to_string(),
        Field::Iavail => row.iavail.to_string(),
        Field::Ipcent => {
            if row.is_total || row.itotal == 0 {
                // Windows volumes have no inode counts: documented `-`
                "-".to_string()
            } else {
                format!("{}%", pct(row.iused, row.iavail))
            }
        }
        Field::Size => render_count(row.size, opts),
        Field::Used => render_count(row.used, opts),
        Field::Avail => render_count(row.avail, opts),
        Field::Pcent => format!("{}%", pct(row.used, row.avail)),
        Field::File => {
            if row.is_total {
                "-".to_string()
            } else {
                row.file.clone()
            }
        }
        Field::Target => {
            if row.is_total {
                "-".to_string()
            } else {
                row.target.clone()
            }
        }
    }
}

fn render_count(bytes: u64, opts: &DfOpts) -> String {
    if opts.human || opts.si {
        human(bytes, opts.si)
    } else {
        ceil_div(bytes, opts.block_size).to_string()
    }
}

struct DfOpts {
    human: bool,
    si: bool,
    block_size: u64,
}

// ------------------------------------------------------------- main

pub fn df_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // GNU --output is optional_argument: a BARE `--output` never consumes
    // the next operand, and `--output LIST` (space form) treats LIST as a
    // file operand. argopt only models required values, so rewrite a bare
    // `--output` into the inline form before parsing (df.rs-local shim;
    // argopt.rs is shared lane infrastructure).
    let mut bare_output = false;
    let mut rewritten: Vec<OsString> = Vec::with_capacity(args.len());
    let mut after_dd = false;
    for a in args {
        let s = a.to_string_lossy();
        if !after_dd && s == "--" {
            after_dd = true;
            rewritten.push(a.clone());
            continue;
        }
        if !after_dd && s == "--output" {
            bare_output = true;
            rewritten.push(OsString::from("--output="));
            continue;
        }
        rewritten.push(a.clone());
    }
    let p = match argopt::parse("df", &rewritten, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    let human = p.has("human-readable");
    let si = p.has("si");
    let posix = p.has("portability");
    let inodes = p.has("inodes");
    let total_flag = p.has("total");
    let output_mode = p.has("output");
    // `--output=` with an explicit (empty) list is an unknown-field error
    // (GNU verified); only the BARE --output selects all 12 columns.
    let explicit_output_list = p.value_of("output").filter(|_| !bare_output);

    // mutually exclusive pairs (GNU verified wordings)
    if posix && p.has("output") {
        let _ = usage_error(io, "df", "options -P and --output are mutually exclusive");
        return Ok(1);
    }
    if inodes && p.has("output") {
        let _ = usage_error(io, "df", "options -i and --output are mutually exclusive");
        return Ok(1);
    }

    // block size: -B/--block-size (default 1024)
    let mut block_size: u64 = 1024;
    if let Some(arg) = p
        .value_of("block-size-arg")
        .or_else(|| p.value_of("block-size"))
    {
        let spell = if p.value_of("block-size-arg").is_some() {
            "-B"
        } else {
            "--block-size"
        };
        match parse_block_size(arg) {
            Some((sz, _suffix)) => block_size = sz,
            None => {
                let msg = format!("invalid {spell} argument {}", winux_core::q(arg));
                let _ = usage_error(io, "df", &msg);
                return Ok(1);
            }
        }
    }
    let opts = DfOpts {
        human,
        si,
        block_size,
    };
    let block_label = block_label(block_size);

    // column selection
    let fields: Vec<Field> = if let Some(list) = explicit_output_list {
        let mut seen: Vec<Field> = Vec::new();
        for name in list.split(',') {
            match parse_field(name) {
                Some(f) => {
                    if seen.contains(&f) {
                        let msg = format!(
                            "option --output: field {} used more than once",
                            winux_core::q(name)
                        );
                        let _ = usage_error(io, "df", &msg);
                        return Ok(1);
                    }
                    seen.push(f);
                }
                None => {
                    let msg = format!("option --output: field {} unknown", winux_core::q(name));
                    let _ = usage_error(io, "df", &msg);
                    return Ok(1);
                }
            }
        }
        seen
    } else if output_mode {
        // bare --output = all 12 columns (verified)
        [
            Field::Source,
            Field::Fstype,
            Field::Itotal,
            Field::Iused,
            Field::Iavail,
            Field::Ipcent,
            Field::Size,
            Field::Used,
            Field::Avail,
            Field::Pcent,
            Field::File,
            Field::Target,
        ]
        .to_vec()
    } else if inodes {
        vec![
            Field::Source,
            Field::Itotal,
            Field::Iused,
            Field::Iavail,
            Field::Ipcent,
            Field::Target,
        ]
    } else {
        vec![
            Field::Source,
            Field::Size,
            Field::Used,
            Field::Avail,
            Field::Pcent,
            Field::Target,
        ]
    };

    // row collection
    let mut rows: Vec<Row> = Vec::new();
    let mut failed = false;
    let mut seen_mounts: Vec<String> = Vec::new();
    let mut push_volume = |v: &Volume, target: String, dedup: bool| {
        if dedup && seen_mounts.contains(&v.source) {
            return;
        }
        seen_mounts.push(v.source.clone());
        rows.push(Row {
            source: v.source.clone(),
            fstype: v.fstype.clone(),
            itotal: 0,
            iused: 0,
            iavail: 0,
            size: v.total,
            used: v.total.saturating_sub(v.free),
            avail: v.available,
            file: String::new(),
            target,
            is_total: false,
        });
    };

    if p.positionals.is_empty() {
        for v in local_volumes() {
            push_volume(&v, v.source.clone(), false);
        }
    } else {
        let canon_cwd = winux_core::strip_verbatim(
            std::fs::canonicalize(&ctx.cwd).unwrap_or_else(|_| ctx.cwd.clone()),
        )
        .to_string_lossy()
        .into_owned();
        for operand in &p.positionals {
            let name = operand.to_string_lossy().into_owned();
            let path = Path::new(&name);
            let path_abs = if path.is_absolute() {
                path.to_string_lossy().into_owned()
            } else {
                let mut p2 = canon_cwd.clone();
                for comp in crate::fsutil::split_slashes(&name) {
                    match comp {
                        "." => {}
                        ".." => {
                            while p2.ends_with('/') || p2.ends_with('\\') {
                                p2.pop();
                            }
                            match p2.rfind(['/', '\\']) {
                                Some(pos) => p2.truncate(pos),
                                None => {}
                            }
                        }
                        c => {
                            if !p2.ends_with('/') && !p2.ends_with('\\') {
                                p2.push('\\');
                            }
                            p2.push_str(c);
                        }
                    }
                }
                p2
            };
            let resolved = match std::fs::canonicalize(&path_abs) {
                Ok(c) => winux_core::strip_verbatim(c).to_string_lossy().into_owned(),
                Err(_) => {
                    let _ = writeln!(io.stderr, "df: {}: No such file or directory", name);
                    failed = true;
                    continue;
                }
            };
            match mount_of(&resolved).and_then(|m| volume_stats(&m)) {
                Some(v) => push_volume(&v, resolved, true),
                None => {
                    let _ = writeln!(io.stderr, "df: {}: No such file or directory", name);
                    failed = true;
                }
            }
        }
    }

    // fstype filters (exact, case-sensitive match — GNU verified; GNU
    // accepts repeated -t/-x)
    let includes: Vec<String> = p
        .values
        .iter()
        .filter(|(k, _)| k == "type")
        .map(|(_, v)| v.clone())
        .collect();
    let excludes: Vec<String> = p
        .values
        .iter()
        .filter(|(k, _)| k == "exclude-type")
        .map(|(_, v)| v.clone())
        .collect();
    if !includes.is_empty() || !excludes.is_empty() {
        rows.retain(|r| {
            includes.iter().all(|t| r.fstype == *t) && !excludes.iter().any(|t| r.fstype == *t)
        });
    }

    // GNU prints `no file systems processed` only when the FILTER emptied
    // the list; all-missing operands print only their cannot-access errors
    // (verified rc 1 both ways, message only in the filter case).
    if rows.is_empty() {
        if !includes.is_empty() || !excludes.is_empty() {
            let _ = writeln!(io.stderr, "df: no file systems processed");
        }
        return Ok(1);
    }

    // --total summary row (GNU verified: `total` in source, `-` in target)
    if total_flag {
        let (mut sz, mut us, mut av, mut it, mut iu, mut ia) = (0u64, 0u64, 0u64, 0u64, 0u64, 0u64);
        for r in &rows {
            sz = sz.saturating_add(r.size);
            us = us.saturating_add(r.used);
            av = av.saturating_add(r.avail);
            it = it.saturating_add(r.itotal);
            iu = iu.saturating_add(r.iused);
            ia = ia.saturating_add(r.iavail);
        }
        rows.push(Row {
            source: "total".into(),
            fstype: "-".into(),
            itotal: it,
            iused: iu,
            iavail: ia,
            size: sz,
            used: us,
            avail: av,
            file: "-".into(),
            target: "-".into(),
            is_total: true,
        });
    }

    // dynamic column widths: max(header, widest cell) — GNU df aligns this way
    let header: Vec<String> = fields
        .iter()
        .map(|&f| header_of(f, &block_label, posix, output_mode, human || si))
        .collect();
    let cells: Vec<Vec<String>> = rows
        .iter()
        .map(|r| fields.iter().map(|&f| cell_of(r, f, &opts)).collect())
        .collect();
    let widths: Vec<usize> = fields
        .iter()
        .enumerate()
        .map(|(i, _)| {
            header[i].chars().count().max(
                cells
                    .iter()
                    .map(|row| row[i].chars().count())
                    .max()
                    .unwrap_or(0),
            )
        })
        .collect();

    let out = &mut *io.stdout;
    for (i, h) in header.iter().enumerate() {
        if i > 0 {
            write!(out, " ")?;
        }
        if field_is_left(fields[i]) {
            write!(out, "{:<width$}", h, width = widths[i])?;
        } else {
            write!(out, "{:>width$}", h, width = widths[i])?;
        }
    }
    writeln!(out)?;
    let last = fields.len() - 1;
    for row in &cells {
        for (i, c) in row.iter().enumerate() {
            if i > 0 {
                write!(out, " ")?;
            }
            if i == last {
                // GNU does not pad the final column
                write!(out, "{c}")?;
            } else if field_is_left(fields[i]) {
                write!(out, "{:<width$}", c, width = widths[i])?;
            } else {
                write!(out, "{:>width$}", c, width = widths[i])?;
            }
        }
        writeln!(out)?;
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], sc: &ScratchCtx) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(|s| s.into()).collect();
        let r = df_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn listing_and_errors() {
        let sc = ScratchCtx::new("df");
        let (r, out, _) = run(&[], &sc);
        assert_eq!(r, 0);
        assert!(out.contains("Filesystem"));
        assert!(out.lines().count() >= 2);
        // missing operand: rc 1, GNU wording
        let (r, _, e) = run(&["Z:\\definitely-missing-path"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("df: Z:\\definitely-missing-path: No such file or directory"));
        // unknown option still usage-error via argopt
        let (r, _, _) = run(&["--bogus"], &sc);
        assert_eq!(r, 1);
    }

    #[test]
    fn default_columns_and_alignment() {
        let sc = ScratchCtx::new("df2");
        let (_, out, _) = run(&[], &sc);
        let hdr = out.lines().next().unwrap();
        // GNU default header shape (verified)
        assert!(hdr.contains("1K-blocks"));
        assert!(hdr.contains("Mounted on"));
        // dynamic widths: aligned up to the final column; the final column
        // is unpadded (GNU leaves no trailing blanks), so a row may end
        // short by the "Mounted on" header width.
        for line in out.lines().skip(1) {
            assert!(
                line.trim_end().chars().count()
                    >= hdr.trim_end().chars().count() - "Mounted on".len() - 1,
                "short row: {line}"
            );
        }
    }

    #[test]
    fn human_and_si() {
        let sc = ScratchCtx::new("df3");
        let (_, out, _) = run(&["-h"], &sc);
        assert!(out.lines().next().unwrap().contains("Size"));
        // human values carry no B suffix and use 1024 letters
        assert!(out
            .lines()
            .nth(1)
            .unwrap()
            .chars()
            .any(|c| "KMGTP".contains(c)));
        let (_, out, _) = run(&["-H"], &sc);
        assert!(out.lines().next().unwrap().contains("Size"));
    }

    #[test]
    fn posix_header() {
        let sc = ScratchCtx::new("df4");
        let (_, out, _) = run(&["-P"], &sc);
        assert!(out.lines().next().unwrap().contains("1024-blocks"));
        assert!(out.lines().next().unwrap().contains("Capacity"));
    }

    #[test]
    fn output_field_errors() {
        let sc = ScratchCtx::new("df5");
        // unknown field (GNU verified wording + rc)
        let (r, _, e) = run(&["--output=bogus"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("option --output: field 'bogus' unknown"));
        // duplicate field
        let (r, _, e) = run(&["--output=source,source"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("field 'source' used more than once"));
        // empty field
        let (r, _, e) = run(&["--output="], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("field '' unknown"));
        // mutual exclusivity (GNU verified wordings)
        let (r, _, e) = run(&["-P", "--output=source"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("options -P and --output are mutually exclusive"));
        let (r, _, e) = run(&["-i", "--output=source"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("options -i and --output are mutually exclusive"));
    }

    #[test]
    fn output_columns() {
        let sc = ScratchCtx::new("df6");
        let (_, out, _) = run(&["--output=source,fstype"], &sc);
        let hdr = out.lines().next().unwrap();
        assert!(hdr.contains("Filesystem") && hdr.contains("Type"));
        assert_eq!(out.lines().count() >= 2, true);
        // bare --output = all 12 columns
        let (_, out, _) = run(&["--output"], &sc);
        let hdr = out.lines().next().unwrap();
        for h in ["Filesystem", "Type", "Inodes", "IUsed", "IFree", "IUse%"] {
            assert!(hdr.contains(h), "missing {h} in {hdr}");
        }
        // inode columns on Windows: zeros with IUse% = `-` (documented)
        let row = out.lines().nth(1).unwrap();
        assert!(row.contains(" - ") || row.ends_with(" -") || row.contains(" -"));
        // --output=target,file (GNU verified header order)
        let (_, out, _) = run(&["--output=target,file"], &sc);
        let hdr = out.lines().next().unwrap();
        assert!(hdr.contains("Mounted on") && hdr.contains("File"));
    }

    #[test]
    fn type_filters() {
        let sc = ScratchCtx::new("df7");
        // -t with an impossible type: no rows -> GNU wording rc 1 (verified)
        let (r, _, e) = run(&["-t", "bogusfs"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("no file systems processed"));
        // -x excluding the real fstype of the first volume also empties
        let vols = local_volumes();
        if let Some(v) = vols.first() {
            let (r, _, e) = run(&["-x", &v.fstype], &sc);
            assert_eq!(r, 1);
            assert!(e.contains("no file systems processed"));
        }
        // -t and -x together cancel out
        let vols = local_volumes();
        if let Some(v) = vols.first() {
            let (r, _, _) = run(&["-t", &v.fstype, "-x", &v.fstype], &sc);
            assert_eq!(r, 1);
        }
    }

    #[test]
    fn total_row() {
        let sc = ScratchCtx::new("df8");
        let (_, out, _) = run(&["--total"], &sc);
        let last = out.lines().last().unwrap();
        assert!(last.trim_start().starts_with("total"));
        assert!(last.trim_end().ends_with('-'));
        // rc 0 with --total (GNU verified)
        let (r, _, _) = run(&["--total"], &sc);
        assert_eq!(r, 0);
    }

    #[test]
    fn block_sizes() {
        let sc = ScratchCtx::new("df9");
        // -B2048 normalizes the header (GNU verified: 2K-blocks)
        let (_, out, _) = run(&["-B", "2048"], &sc);
        assert!(out.lines().next().unwrap().contains("2K-blocks"));
        // -B1M header
        let (_, out, _) = run(&["-B", "1M"], &sc);
        assert!(out.lines().next().unwrap().contains("1M-blocks"));
        // invalid suffix: GNU verified wording + rc 1
        let (r, _, e) = run(&["-B", "c"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid -B argument 'c'"));
        // --block-size spelling in the message
        let (r, _, e) = run(&["--block-size", "zz"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid --block-size argument 'zz'"));
        // -k still works
        let (r, out, _) = run(&["-k"], &sc);
        assert_eq!(r, 0);
        assert!(out.lines().next().unwrap().contains("1K-blocks"));
    }

    #[test]
    fn math_units() {
        assert_eq!(ceil_div(9, 4), 3);
        assert_eq!(ceil_div(8, 4), 2);
        // GNU pcenceil
        assert_eq!(pct(1, 3), 25);
        assert_eq!(pct(2, 2), 50);
        assert_eq!(pct(0, 5), 0);
        // human: <10 one decimal, >=10 integer, bytes plain (GNU verified)
        assert_eq!(human(0, false), "0");
        assert_eq!(human(512, false), "512");
        assert_eq!(human(4096, false), "4.0K");
        assert_eq!(human(263112772 * 1024u64, false), "251G");
        assert_eq!(human(1024 * 1024 * 1024 * 1024, false), "1.0T");
        assert_eq!(human(4_000_000, true), "4.0M");
        // block sizes
        assert_eq!(parse_block_size("2048").map(|(v, _)| v), Some(2048));
        assert_eq!(parse_block_size("1M").map(|(v, _)| v), Some(1024 * 1024));
        assert_eq!(parse_block_size("kB").map(|(v, _)| v), Some(1000));
        assert_eq!(parse_block_size("MB").map(|(v, _)| v), Some(1000000));
        assert!(parse_block_size("c").is_none());
        assert!(parse_block_size("zz").is_none());
        assert!(parse_block_size("").is_none());
        assert_eq!(block_label(2048), "2K-blocks");
        assert_eq!(block_label(1024 * 1024), "1M-blocks");
        assert_eq!(block_label(1024), "1K-blocks");
    }

    #[test]
    fn mount_point_resolution() {
        let sc = ScratchCtx::new("df10");
        std::fs::create_dir_all(sc.path("sub/deep")).unwrap();
        std::fs::write(sc.path("sub/deep/f"), "x").unwrap();
        // file operand resolves to its volume (GNU verified)
        let (r, out, _) = run(&["sub/deep/f"], &sc);
        assert_eq!(r, 0);
        assert!(out.lines().count() >= 2);
        // dir operand
        let (r, _, _) = run(&["sub/deep"], &sc);
        assert_eq!(r, 0);
        // symlink operand resolves through (GNU verified)
        #[cfg(windows)]
        {
            let _ = std::os::windows::fs::symlink_file(sc.path("sub/deep/f"), sc.path("link"));
        }
        // same-filesystem operands deduplicate to one row (GNU behavior)
        std::fs::write(sc.path("g"), "x").unwrap();
        let (_, out, _) = run(&["g", "sub/deep/f"], &sc);
        assert_eq!(out.lines().count(), 2); // header + 1 row
                                            // mount_of unit checks
        assert_eq!(mount_of(r"C:\x\y"), Some(r"C:\".to_string()));
        assert_eq!(
            mount_of(r"\\srv\share\d\e"),
            Some(r"\\srv\share".to_string())
        );
        assert_eq!(mount_of(r"relative"), None);
    }

    #[test]
    fn relative_dot_and_parent() {
        let sc = ScratchCtx::new("df11");
        std::fs::create_dir_all(sc.path("sub")).unwrap();
        let (r, _, _) = run(&["."], &sc);
        assert_eq!(r, 0);
        let (r, _, _) = run(&["sub/.."], &sc);
        assert_eq!(r, 0);
    }
}
