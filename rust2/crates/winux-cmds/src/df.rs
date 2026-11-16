//! `df` — GNU coreutils column model, Windows volume enumeration.
//!
//! Spec: GNU coreutils 9.4 `src/df.c` + `lib/human.c`, probed against the
//! WSL oracle (clean-room: owning functions cited, not transcribed):
//!
//! * Option loop + display modes — `main` df.c:1585, getopt string
//!   `"aB:iF:hHklmPTt:vx:"` df.c:1616, long table df.c:255-273. Sequential
//!   semantics: `-i`/`-P`/`-T` vs `--output` mutual exclusion is diagnosed
//!   at the SECOND conflicting option, argv order (df.c:1635-1639,
//!   1663-1667, 1671-1675, 1697-1713 — checks run in order -i, -P, -T), so
//!   `df -i -P --output` reports "-i and --output" while `df --output -P`
//!   reports "-P and --output" (probed); `df -i --output --help` fails rc 1
//!   before help is reached (probed), `df --help -T --output` prints help
//!   rc 0 (probed). This module replays `argopt::Parsed::order`.
//! * Size options are LAST-WINS: -h/-H/-k/-m/-B/--block-size (df.c cases
//!   'h' 1641, 'H' 1646, 'k' 1650, 'm' 1657-1660, 'B' 1627-1632). Probed:
//!   `-h -B 2048` -> 2K-blocks, `-B 2048 -h` -> human, `-m` -> 1M-blocks.
//! * When no size option is given (df.c:1730-1738): `-P` mode picks
//!   POSIXLY_CORRECT ? 512 : 1024; otherwise `human_options` reads
//!   DF_BLOCK_SIZE then BLOCK_SIZE then BLOCKSIZE (gnulib humblock
//!   human.c:414-424), honoring "human-readable"/"si" keywords and bad
//!   specs falling back to POSIXLY_CORRECT ? 512 : 1024 (probed
//!   `DF_BLOCK_SIZE=bad` -> 1K; `POSIXLY_CORRECT=1` alone -> 512B-blocks
//!   even without -P).
//! * `-i` mode wins over -P and -h for COLUMN selection (header_mode kept
//!   at INODES_MODE df.c:1742-1747); `-h` beats -P (`df -P -h` -> "Size",
//!   probed). `-T`/`--print-type` inserts FSTYPE after SOURCE in every
//!   non-output mode (df.c:509/520/532/543) and is mutex with --output.
//! * `-t`/`-x` filters: a type named in BOTH is a hard error
//!   `file system type 'x' both selected and excluded` rc 1 BEFORE any
//!   operand stat (df.c:1749-1767, probed). Filtering drops rows inside
//!   get_dev (df.c:1024-1025); an all-filtered run ends at
//!   `no file systems processed` rc 1 (df.c:1846-1849) — but ONLY when no
//!   earlier operand error already failed the run (probed
//!   `df -t bogusfs /missing` prints just the stat error).
//! * `-l`/`--local` (df.c:1654) drops remote mounts (`me_remote &&
//!   show_local_fs` df.c:1018); `-F` is a -t synonym (df.c:1684); `-v` is
//!   a silent no-op (df.c:1690); `--sync` calls sync() before reading
//!   stats (df.c:1818) — a no-op on Windows where GetDiskFreeSpaceExW is
//!   always live; `--no-sync` restores the default. All probed accepted.
//! * Operands: each is stat'd first (automount_stat_err df.c:281,
//!   loop df.c:1770-1785); failures print `df: <quotef>: <err>` and
//!   continue with rc 1. Every successful operand prints its OWN row —
//!   GNU does NOT dedup (`df . .` -> two identical rows, probed). The
//!   `file` column is the operand text as typed, `target` is the mount
//!   point (`get_point` df.c:1373-1447, `FILE_FIELD` cell df.c:1227,
//!   `TARGET_FIELD` cell df.c:1231-1242). Operands the WSL oracle cannot
//!   stat — drive-letter forms (`C:\`, `C:\Windows`) are not POSIX
//!   paths — take the GNU missing-path branch: quotef'd
//!   `No such file or directory`, empty stdout, rc 1 (probed).
//! * --output fields (decode_output_arg df.c:431-498):
//!   source,fstype,itotal,iused,iavail,ipcent,size,used,avail,pcent,file,
//!   target — bare `--output` = all 12 (df.c:555-556, probed). Unknown /
//!   duplicate / empty field -> usage error rc 1 (df.c:456, 462-464).
//!   `--output=` with `=` form only; bare `--output`/abbreviations
//!   (`--out`) never consume the next arg (optional_argument, probed).
//! * --total (df.c:1837-1841): the word "total" sits in the SOURCE cell
//!   when that field is printed, otherwise it moves into the TARGET cell
//!   (probed `--output=size,target --total` -> last row target "total");
//!   file/fstype cells are "-", block columns are sums.
//! * Numbers: block counts are CEIL-divided (df_readable df.c:879-888
//!   feeding human_readable); -h/-H and the `N-blocks` header use
//!   human.c's CEILING autoscale (`human_ceiling == 0` is the default
//!   style, human.h:53; integer path human.c:284-351 — mantissa < 10
//!   keeps one decimal rounded UP, >= 10 rounds up to integer, `amt ==
//!   base` promotes an exponent). `-B` suffix grammar is gnulib
//!   xstrtoumax: digits+optional {eEgGkKmMpPtTyYzZ}[iB|B] are clean
//!   scalars, while a SUFFIX-ONLY spec (K, KiB, kB, MB, ...) sets
//!   human_SI (+ human_B for *B forms) so data cells carry the unit
//!   suffix (`-B K` -> `263112772K`, `-B KiB` -> `263112772KiB`, `-B kB`
//!   -> `269427479kB`, all probed). Invalid spec -> `df: invalid -B
//!   argument 'x'` rc 1 WITHOUT the try-help line (error() not usage()).
//! * Headers (get_header df.c:568-647): default/output-mode SIZE shows
//!   the humanized block size + "-blocks" (df.c:578-618; "more exact"
//!   base choice -> 1000 `1kB-blocks`, 2000 `2kB-blocks`, 1024000
//!   `1.1MB-blocks`, 1024 `1K-blocks`, 512 `512B-blocks` — all probed);
//!   POSIX mode shows the literal `{n}-blocks` (df.c:620-626); -h/-H and
//!   autoscale --output show "Size". Column minimum widths come from
//!   field_data df.c:178-215; print_table df.c:377-409 aligns them and
//!   sets MBA_NO_RIGHT_PAD on the LAST column (df.c:397) — the byte-shape
//!   rule of #1657: a left-aligned last column is emitted unpadded, a
//!   right-aligned last column keeps its left pad, header row included.
//!
//! Windows mapping (documented deviations, value-model class):
//! * source column = the volume's mount root (`C:\`, `\\srv\share`) —
//!   GNU prints device names; platform-inherent. Drive enumeration is
//!   GetLogicalDriveStringsW; volumes mounted at NTFS junctions are not
//!   listed without an operand, but operands resolve through
//!   GetVolumePathNameW to the true mount root.
//! * inode columns report `-` with IUse% = `-`: NTFS exposes no inode
//!   count through GetDiskFreeSpaceExW/GetVolumeInformationW (the 1.x
//!   C++ applet renders the same placeholders).
//! * fstype from GetVolumeInformationW (NTFS, FAT32, ...); `-` when the
//!   volume query fails. Network drives (DRIVE_REMOTE) are probed in a
//!   worker thread with a bounded wait so a dead UNC cannot hang df.
//! * Pseudo filesystems do not exist in the Windows mount model —
//!   nothing to list or mark; -a only affects total==0 volumes
//!   (fsu_blocks==0 skip rule, df.c:1082-1083).

use std::ffi::OsString;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::digestsum::quotef;
use winux_core::{tr, usage_error, CmdResult, Ctx, EnvSource, Io};

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
    // -m exists for BSD compatibility (df.c:1657); no long form.
    OptSpec {
        short: Some('m'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('l'),
        long: "local",
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
        short: Some('T'),
        long: "print-type",
        takes_value: false,
    },
    OptSpec {
        short: Some('t'),
        long: "type",
        takes_value: true,
    },
    // -F: Solaris -t synonym (df.c:1684); short-only, records under "".
    OptSpec {
        short: Some('F'),
        long: "",
        takes_value: true,
    },
    OptSpec {
        short: Some('x'),
        long: "exclude-type",
        takes_value: true,
    },
    // -v: SysV compatibility, ignored (df.c:1690).
    OptSpec {
        short: Some('v'),
        long: "",
        takes_value: false,
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
    OptSpec {
        short: None,
        long: "sync",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "no-sync",
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

/// field_data minimum widths (df.c:178-215): the column never shrinks
/// below this even when header and cells are all narrower.
fn field_min_width(f: Field) -> usize {
    match f {
        Field::Source => 14,
        Field::Fstype => 4,
        Field::Itotal | Field::Iused | Field::Iavail => 5,
        Field::Ipcent => 4,
        Field::Size | Field::Used | Field::Avail => 5,
        Field::Pcent => 4,
        Field::File | Field::Target => 0,
    }
}

/// left-aligned columns (df.c field_data MBS_ALIGN_LEFT).
fn field_is_left(f: Field) -> bool {
    matches!(
        f,
        Field::Source | Field::Fstype | Field::File | Field::Target
    )
}

/// Display modes matching GNU header_mode (df.c:131-137).
#[derive(Debug, Clone, Copy, PartialEq)]
enum Mode {
    Default,
    Inodes,
    Human,
    Posix,
    Output,
}

// ------------------------------------------------------------ volumes

struct Volume {
    /// mount root: `C:\` or `\\srv\share`
    root: String,
    fstype: String,
    total: u64,
    free: u64,
    available: u64,
    remote: bool,
}

#[link(name = "kernel32")]
extern "system" {
    fn GetLogicalDriveStringsW(n_buffer_length: u32, lp_buffer: *mut u16) -> u32;
    fn GetDriveTypeW(lp_root_path_name: *const u16) -> u32;
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
    fn GetVolumePathNameW(
        lpsz_file_name: *const u16,
        lpsz_volume_path_name: *mut u16,
        cch_buffer_length: u32,
    ) -> i32;
}

const DRIVE_REMOTE: u32 = 4;

fn wide(s: &str) -> Vec<u16> {
    s.encode_utf16().chain(std::iter::once(0)).collect()
}

fn utf16_str(buf: &[u16]) -> String {
    let end = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..end])
}

/// Raw volume query: free/total bytes plus the file-system label.
fn query_volume(root: &str) -> Option<Volume> {
    let wide_name = wide(root);
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
        utf16_str(&fs_buf)
    };
    Some(Volume {
        root: root.to_string(),
        fstype,
        total,
        free,
        available: avail,
        remote: unsafe { GetDriveTypeW(wide_name.as_ptr()) } == DRIVE_REMOTE,
    })
}

/// Volume stats with remote-drive bounding: a dead UNC can block SMB for
/// tens of seconds, so DRIVE_REMOTE queries run on a worker thread with a
/// bounded wait; on timeout the volume is skipped (GNU df drops mounts it
/// cannot stat).
fn volume_stats(root: &str) -> Option<Volume> {
    let remote = unsafe { GetDriveTypeW(wide(root).as_ptr()) } == DRIVE_REMOTE;
    if !remote {
        return query_volume(root);
    }
    let (tx, rx) = std::sync::mpsc::channel();
    let root2 = root.to_string();
    std::thread::spawn(move || {
        let _ = tx.send(query_volume(&root2));
    });
    rx.recv_timeout(std::time::Duration::from_secs(3))
        .ok()
        .flatten()
}

/// Enumerate drive roots via GetLogicalDriveStringsW ("C:\\\0D:\\\0\0").
fn local_volumes() -> Vec<Volume> {
    let mut buf = [0u16; 1024];
    let n = unsafe { GetLogicalDriveStringsW(buf.len() as u32, buf.as_mut_ptr()) } as usize;
    let mut out = Vec::new();
    let mut i = 0;
    while i < n.min(buf.len()) {
        let start = i;
        while i < n.min(buf.len()) && buf[i] != 0 {
            i += 1;
        }
        if i == start {
            break;
        }
        let root = utf16_str(&buf[start..i + 1]);
        if let Some(v) = volume_stats(&root) {
            out.push(v);
        }
        i += 1;
    }
    out
}

/// True for `X:` / `X:\` / `X:\...` drive-letter forms (the operand
/// class the WSL oracle cannot stat as a POSIX path).
fn is_drive_letter_form(name: &str) -> bool {
    let b = name.as_bytes();
    b.len() >= 2 && b[0].is_ascii_alphabetic() && b[1] == b':'
}

/// Mount root for an absolute path: GetVolumePathNameW first (handles
/// NTFS junction mounts), then the drive root / `\\server\share` prefix.
fn mount_of(abs: &str) -> Option<String> {
    let mut buf = [0u16; 1024];
    let ok = unsafe { GetVolumePathNameW(wide(abs).as_ptr(), buf.as_mut_ptr(), buf.len() as u32) };
    if ok != 0 {
        let s = utf16_str(&buf);
        if !s.is_empty() {
            return Some(s);
        }
    }
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

/// GNU df pcenceil: ceil(100*used/(used+avail)), clamped at 100
/// (df.c:1170-1188 integer branch).
fn pct(used: u64, avail: u64) -> u64 {
    let total = used.saturating_add(avail);
    if total == 0 {
        return 0;
    }
    ((used * 100 + total - 1) / total).min(100)
}

// ---------------------------------------------- human_readable (ceil)

const POWER_LETTER: [char; 11] = ['\0', 'K', 'M', 'G', 'T', 'P', 'E', 'Z', 'Y', 'R', 'Q'];

/// GNU human.c autoscale integer path (human.c:284-351, from==to==1 so
/// amt=n, tenths=rounding=0 on entry): divide out `base`, then render
/// with CEILING rounding — df passes no rounding flag, and
/// `human_ceiling` is style 0 (human.h:53). Returns
/// (integer, optional tenths digit, exponent).
fn autoscale_ceil(n: u64, base1024: bool, suppress0: bool) -> (u64, Option<u8>, usize) {
    let base: u128 = if base1024 { 1024 } else { 1000 };
    let mut amt: u128 = n as u128;
    let mut tenths: u128 = 0;
    let mut rounding: u128 = 0;
    let mut e = 0usize;
    if amt >= base {
        while amt >= base && e < POWER_LETTER.len() - 1 {
            let r10 = (amt % base) * 10 + tenths;
            let r2 = (r10 % base) * 2 + (rounding >> 1);
            amt /= base;
            tenths = r10 / base;
            rounding = if r2 < base {
                u128::from(r2 + rounding != 0)
            } else {
                2 + u128::from(base < r2 + rounding)
            };
            e += 1;
        }
        if amt < 10 {
            if rounding > 0 {
                // ceiling: bump the tenths digit on any residue
                tenths += 1;
                if tenths == 10 {
                    amt += 1;
                    tenths = 0;
                }
            }
            if amt < 10 && (tenths > 0 || !suppress0) {
                return (amt as u64, Some(tenths as u8), e);
            }
            tenths = 0;
            rounding = 0;
        }
    }
    if tenths + rounding > 0 {
        amt += 1;
        if amt == base && e < POWER_LETTER.len() - 1 {
            // promoted: 1023.9K -> 1.0M ("1.0" kept unless suppressed)
            e += 1;
            return (1, if suppress0 { None } else { Some(0) }, e);
        }
    }
    (amt as u64, None, e)
}

/// SI suffix for an exponent: 'k' is the decimal-kilo special case
/// (human.c:374-384); human_B appends "iB" (base 1024) or "B".
fn suffix_of(exp: usize, base1024: bool, b_flag: bool) -> String {
    let mut s = String::new();
    if exp > 0 {
        s.push(if !base1024 && exp == 1 {
            'k'
        } else {
            POWER_LETTER[exp.min(POWER_LETTER.len() - 1)]
        });
    }
    if b_flag {
        if base1024 && exp > 0 {
            s.push('i');
        }
        s.push('B');
    }
    s
}

/// -h/-H data cell: autoscale, ceiling, ".0" kept (no suppress flag).
fn human(n: u64, si1000: bool) -> String {
    let base1024 = !si1000;
    let (m, t, e) = autoscale_ceil(n, base1024, false);
    match t {
        Some(d) => format!("{m}.{d}{}", suffix_of(e, base1024, false)),
        None => format!("{m}{}", suffix_of(e, base1024, false)),
    }
}

/// `N-blocks` header label (get_header df.c:588-613): the base is the
/// one that makes the value more exact — count consecutive divisibility
/// of output_block_size by 1000 vs 1024; the higher count wins (ties go
/// decimal). Decimal base forces human_B ("1kB"/"512B"); binary keeps
/// the spec's own flags ("1KiB" for `-B KiB`).
fn size_label(size: u64, spec_base1024: bool, spec_b: bool) -> String {
    let mut d1000: u32;
    let mut d1024: u32;
    let (mut q1000, mut q1024) = (size, size);
    loop {
        d1000 = u32::from(q1000 % 1000 == 0);
        q1000 /= 1000;
        d1024 = u32::from(q1024 % 1024 == 0);
        q1024 /= 1024;
        if !(d1000 == 1 && d1024 == 1) {
            break;
        }
    }
    let mut base1024 = spec_base1024;
    let mut b = spec_b;
    if d1000 < d1024 {
        base1024 = true;
    }
    if d1024 < d1000 {
        base1024 = false;
    }
    if !base1024 {
        b = true;
    }
    let (m, t, e) = autoscale_ceil(size, base1024, true);
    let num = match t {
        Some(d) => format!("{m}.{d}"),
        None => m.to_string(),
    };
    format!("{num}{}", suffix_of(e, base1024, b))
}

/// The cell suffix a SUFFIX-ONLY -B spec leaves on scaled numbers
/// (`df -B K` -> `263112772K`, `-B KiB` -> `263112772KiB`, `-B kB` ->
/// `269427479kB`): SI letter for the spec's exponent in its base, then
/// the iB/B tail when human_B was set. `to_units` is the conversion
/// denominator (output_block_size for block cells, 1 for inode cells).
fn cell_suffix(to_units: u64, si: bool, b: bool, base1024: bool) -> String {
    if !si {
        return String::new();
    }
    let base: u64 = if base1024 { 1024 } else { 1000 };
    let mut e = 0usize;
    let mut power: u128 = 1;
    while power < to_units as u128 && e < POWER_LETTER.len() - 1 {
        power *= base as u128;
        e += 1;
    }
    suffix_of(e, base1024, b)
}

/// A parsed SIZE argument: gnulib humblock grammar (human.c:410-449).
#[derive(Debug, Clone, Copy)]
enum SizeSpec {
    /// autoscale (-h semantics, or the "human-readable"/"si" keywords /
    /// suffix-led spec flag combination).
    Human { si1000: bool },
    Blocks {
        size: u64,
        si: bool,
        b: bool,
        base1024: bool,
    },
}

/// SIZE-spec failure classes (xstrtoumax LONGINT_*): a spec with no
/// usable number AND no leading unit letter is `invalid ... argument`;
/// a spec whose unit suffix is malformed is `invalid suffix in ...
/// argument` (probed: -B iB / -B 0 / -B c vs -B 1X / -B Ki / -B 1kb).
enum SizeParse {
    Ok(SizeSpec),
    InvalidArg,
    InvalidSuffix,
}

const UNIT_EXP: &[char] = &['k', 'm', 'g', 't', 'p', 'e', 'z', 'y'];

fn unit_exp(c: char) -> Option<usize> {
    UNIT_EXP
        .iter()
        .position(|&u| u == c.to_ascii_lowercase())
        .map(|i| i + 1)
}

/// Parse the strtoumax suffix of a digit-led spec, or a suffix-only spec
/// (num implied 1). Returns (multiplier, exponent) or None.
fn suffix_multiplier(suffix: &str) -> Option<(u64, usize)> {
    let (letter, tail) = suffix.split_at(1);
    let c = letter.chars().next()?;
    let e = unit_exp(c)?;
    match tail {
        "" => Some((1024u64.checked_pow(e as u32)?, e)),
        "B" => Some((1000u64.checked_pow(e as u32)?, e)),
        "iB" => Some((1024u64.checked_pow(e as u32)?, e)),
        _ => None,
    }
}

/// gnulib human_options for a SIZE spec: "human-readable"/"si" keywords,
/// digit-led `N[suffix]` scalars (no SI flags), suffix-only specs that
/// set human_SI (+ human_B for *B forms), else the two GNU error
/// classes.
fn parse_block_size(s: &str) -> SizeParse {
    if s.is_empty() {
        return SizeParse::InvalidArg;
    }
    // leading ' requests digit grouping (human.c:421-424); ignored here —
    // Windows builds never group.
    let t = s.strip_prefix('\'').unwrap_or(s);
    match t {
        "human-readable" => return SizeParse::Ok(SizeSpec::Human { si1000: false }),
        "si" => return SizeParse::Ok(SizeSpec::Human { si1000: true }),
        _ => {}
    }
    if t.is_empty() {
        return SizeParse::InvalidArg;
    }
    if t.as_bytes()[0].is_ascii_digit() {
        // digit-led: strtoumax base-0 number (0x hex, 0 octal), then the
        // optional multiplier suffix — flags stay 0 (cells are clean).
        let hex = t
            .strip_prefix("0x")
            .or_else(|| t.strip_prefix("0X"))
            .filter(|h| h.chars().next().is_some_and(|c| c.is_ascii_hexdigit()));
        let (num, rest) = if let Some(hex) = hex {
            let digits: String = hex.chars().take_while(|c| c.is_ascii_hexdigit()).collect();
            match u64::from_str_radix(&digits, 16) {
                Ok(n) => (n, &hex[digits.len()..]),
                Err(_) => return SizeParse::InvalidArg,
            }
        } else {
            let digits: String = t.chars().take_while(|c| c.is_ascii_digit()).collect();
            let rest = &t[digits.len()..];
            let num = if digits.starts_with('0') && digits.len() > 1 {
                match u64::from_str_radix(&digits, 8) {
                    Ok(n) => n,
                    // "09": strtoumax consumes "0", leaves "9" -> suffix err
                    Err(_) => return SizeParse::InvalidSuffix,
                }
            } else {
                match digits.parse::<u64>() {
                    Ok(n) => n,
                    Err(_) => return SizeParse::InvalidArg,
                }
            };
            (num, rest)
        };
        let mult = if rest.is_empty() {
            1
        } else {
            match suffix_multiplier(rest) {
                Some((m, _)) => m,
                None => return SizeParse::InvalidSuffix,
            }
        };
        return match num.checked_mul(mult) {
            Some(size) if size > 0 => SizeParse::Ok(SizeSpec::Blocks {
                size,
                si: false,
                b: false,
                base1024: false,
            }),
            // 0 and overflows report as argument-invalid (probed -B 0)
            _ => SizeParse::InvalidArg,
        };
    }
    // suffix-only (num implied 1): sets human_SI, human_B when the spec
    // ends in 'B', human_base_1024 when it does not or ends 'iB'
    // (human.c:437-447). A leading unit letter with a bad tail is the
    // invalid-suffix class (probed -B Ki); anything else invalid-arg.
    if unit_exp(t.chars().next().unwrap()).is_none() {
        return SizeParse::InvalidArg;
    }
    match suffix_multiplier(t) {
        Some((mult, _)) => {
            let ends_b = t.ends_with('B');
            let ends_ib = t.ends_with("iB");
            let base1024 = !ends_b || ends_ib;
            SizeParse::Ok(SizeSpec::Blocks {
                size: mult,
                si: true,
                b: ends_b,
                base1024,
            })
        }
        None => SizeParse::InvalidSuffix,
    }
}

// ------------------------------------------------------------- rows

struct Row {
    source: String,
    fstype: String,
    /// byte-based fields; rendered through the size mode
    size: u64,
    used: u64,
    avail: u64,
    file: String,
    target: String,
    is_total: bool,
}

/// How block counts render: -h/-H autoscale, otherwise N scaled by the
/// block size plus any SI suffix the spec carried (`-B K` -> `...K`).
#[derive(Debug, Clone, Copy)]
enum Render {
    Human {
        si1000: bool,
    },
    Blocks {
        size: u64,
        si: bool,
        b: bool,
        base1024: bool,
    },
}

fn render_count(bytes: u64, r: Render) -> String {
    match r {
        Render::Human { si1000 } => human(bytes, si1000),
        Render::Blocks {
            size,
            si,
            b,
            base1024,
        } => format!(
            "{}{}",
            ceil_div(bytes, size),
            cell_suffix(size, si, b, base1024)
        ),
    }
}

fn cell_of(row: &Row, f: Field, r: Render, source_used: bool) -> String {
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
        // Windows volumes expose no inode counts — GNU's unknown-value
        // placeholder (1.x C++ renders the same).
        Field::Itotal | Field::Iused | Field::Iavail | Field::Ipcent => "-".to_string(),
        Field::Size => render_count(row.size, r),
        Field::Used => render_count(row.used, r),
        Field::Avail => render_count(row.avail, r),
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
                // df.c:1839 — "total" moves into the mount cell when the
                // source field is absent from the output.
                if source_used {
                    "-".to_string()
                } else {
                    "total".to_string()
                }
            } else {
                row.target.clone()
            }
        }
    }
}

fn header_of(f: Field, mode: Mode, size_label: &str, posix_block_size: u64) -> String {
    match f {
        Field::Source => "Filesystem".to_string(),
        Field::Fstype => "Type".to_string(),
        Field::Itotal => "Inodes".to_string(),
        Field::Iused => "IUsed".to_string(),
        Field::Iavail => "IFree".to_string(),
        Field::Ipcent => "IUse%".to_string(),
        Field::Size => match mode {
            Mode::Human => "Size".to_string(),
            Mode::Posix => format!("{posix_block_size}-blocks"),
            _ => size_label.to_string(),
        },
        Field::Used => "Used".to_string(),
        Field::Avail => match mode {
            Mode::Human | Mode::Output => "Avail".to_string(),
            _ => "Available".to_string(),
        },
        Field::Pcent => {
            if mode == Mode::Posix {
                "Capacity".to_string()
            } else {
                "Use%".to_string()
            }
        }
        Field::File => "File".to_string(),
        Field::Target => "Mounted on".to_string(),
    }
}

// ------------------------------------------------------------- main

fn env_set(env: &dyn EnvSource, key: &str) -> bool {
    env.get(key).is_some()
}

pub fn df_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // GNU --output is optional_argument: a BARE `--output` (or any bare
    // unambiguous abbreviation --o/--ou/--out/...) never consumes the
    // next operand (probed `df --output f` treats f as a path). argopt
    // only models required values, so rewrite bare `--<prefix-of-output>`
    // tokens into the inline `=` form before parsing, remembering which
    // occurrences were bare — a bare --output contributes NO fields to
    // the accumulated list while an explicit `--output=` errors on the
    // empty field (both probed). df.rs-local shim; argopt.rs is shared
    // lane infrastructure.
    let mut output_bare: Vec<bool> = Vec::new();
    let mut rewritten: Vec<OsString> = Vec::with_capacity(args.len());
    let mut after_dd = false;
    for a in args {
        let s = a.to_string_lossy();
        if !after_dd && s == "--" {
            after_dd = true;
            rewritten.push(a.clone());
            continue;
        }
        if !after_dd && s.starts_with("--") && s.len() > 2 {
            let name = s[2..].split('=').next().unwrap_or("");
            if !name.is_empty() && "output".starts_with(name) {
                let bare = !s.contains('=');
                output_bare.push(bare);
                if bare {
                    rewritten.push(OsString::from("--output="));
                    continue;
                }
            }
        }
        rewritten.push(a.clone());
    }
    let p = match argopt::parse("df", &rewritten, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    // --output values in occurrence order (repeated --output= lists
    // ACCUMULATE fields — probed `--output=source --output=target`
    // prints both columns; `--output --output=target` prints only
    // "Mounted on"; an accumulated-empty list expands to all 12
    // df.c:555-556).
    let output_vals: Vec<String> = p
        .values
        .iter()
        .filter(|(k, _)| k == "output")
        .map(|(_, v)| v.clone())
        .collect();

    // Sequential option semantics (df.c:1614-1741): -i/-P/-T vs --output
    // mutual exclusion fires at the SECOND conflicting option in argv
    // order, with the pair named by that option — GNU exits via usage()
    // there, so a conflict before --help still fails (probed
    // `df -i --output --help` -> rc 1). --output's field list is decoded
    // at its own argv position AFTER the mutex check (probed
    // `-i --output=bogus` -> mutually exclusive, `--output=bogus -i`
    // -> field 'bogus' unknown).
    let mut seen_i = false;
    let mut seen_p = false;
    let mut seen_t = false;
    let mut seen_out = false;
    let mut out_fields: Vec<Field> = Vec::new();
    let mut out_i = 0usize;
    // Size options are last-wins and -B/--block-size validate AT their
    // argv position (xstrtol_fatal inside the getopt loop df.c:1626-1633)
    // — so every occurrence is parsed here, in order.
    let mut pending_size: Option<SizeSpec> = None;
    let mut b_vals = p
        .values
        .iter()
        .filter(|(k, _)| k == "block-size-arg")
        .map(|(_, v)| v.as_str());
    let mut bs_vals = p
        .values
        .iter()
        .filter(|(k, _)| k == "block-size")
        .map(|(_, v)| v.as_str());
    for key in &p.order {
        let excl = match key.as_str() {
            "inodes" => {
                if seen_out {
                    Some("-i")
                } else {
                    seen_i = true;
                    None
                }
            }
            "portability" => {
                if seen_out {
                    Some("-P")
                } else {
                    seen_p = true;
                    None
                }
            }
            "print-type" => {
                if seen_out {
                    Some("-T")
                } else {
                    seen_t = true;
                    None
                }
            }
            "output" => {
                // df.c:1697-1713 checks in order: -i, then -P (only when
                // header is still DEFAULT, i.e. no -i), then -T.
                let bad = if seen_i {
                    Some("-i")
                } else if seen_p {
                    Some("-P")
                } else if seen_t {
                    Some("-T")
                } else {
                    None
                };
                if bad.is_none() {
                    // decode_output_arg at this occurrence (df.c:431-498)
                    let bare = output_bare.get(out_i).copied().unwrap_or(false);
                    let val = output_vals.get(out_i).cloned().unwrap_or_default();
                    if !bare {
                        for name in val.split(',') {
                            match parse_field(name) {
                                Some(f) if out_fields.contains(&f) => {
                                    let _ = usage_error(
                                        ctx,
                                        io,
                                        "df",
                                        &tr!(
                                            ctx,
                                            "command.df.error.output_field_duplicate",
                                            "option --output: field '{field}' used more than once",
                                            field = name
                                        ),
                                    );
                                    return Ok(1);
                                }
                                Some(f) => out_fields.push(f),
                                None => {
                                    let _ = usage_error(
                                        ctx,
                                        io,
                                        "df",
                                        &tr!(
                                            ctx,
                                            "command.df.error.output_field_unknown",
                                            "option --output: field '{field}' unknown",
                                            field = name
                                        ),
                                    );
                                    return Ok(1);
                                }
                            }
                        }
                    }
                    out_i += 1;
                }
                seen_out = true;
                bad
            }
            _ => None,
        };
        if let Some(first) = excl {
            let _ = usage_error(
                ctx,
                io,
                "df",
                &tr!(
                    ctx,
                    "command.df.error.mutually_exclusive",
                    "options {first} and --output are mutually exclusive",
                    first = first
                ),
            );
            return Ok(1);
        }
        match key.as_str() {
            "human-readable" => pending_size = Some(SizeSpec::Human { si1000: false }),
            "si" => pending_size = Some(SizeSpec::Human { si1000: true }),
            "block-size-1k" => {
                pending_size = Some(SizeSpec::Blocks {
                    size: 1024,
                    si: false,
                    b: false,
                    base1024: false,
                })
            }
            "m" => {
                pending_size = Some(SizeSpec::Blocks {
                    size: 1024 * 1024,
                    si: false,
                    b: false,
                    base1024: false,
                })
            }
            key @ ("block-size-arg" | "block-size") => {
                let (spell, val) = if key == "block-size-arg" {
                    ("-B", b_vals.next().unwrap_or_default())
                } else {
                    ("--block-size", bs_vals.next().unwrap_or_default())
                };
                match parse_block_size(val) {
                    SizeParse::Ok(spec) => pending_size = Some(spec),
                    // error(), not usage(): no try-help line (probed).
                    bad => {
                        let msg = match bad {
                            SizeParse::InvalidSuffix => tr!(
                                ctx,
                                "command.df.error.invalid_size_suffix",
                                "invalid suffix in {option} argument '{value}'",
                                option = spell,
                                value = val
                            ),
                            _ => tr!(
                                ctx,
                                "command.df.error.invalid_size_argument",
                                "invalid {option} argument '{value}'",
                                option = spell,
                                value = val
                            ),
                        };
                        let _ = writeln!(io.stderr, "df: {msg}");
                        return Ok(1);
                    }
                }
            }
            _ => {}
        }
    }

    if p.has("help") || p.has("version") {
        return Ok(0);
    }

    let all_flag = p.has("all");
    let local_only = p.has("local");
    let total_flag = p.has("total");
    let output_mode = seen_out;

    // -t/-x lists; -F is the short-only -t synonym (records under "").
    let includes: Vec<String> = p
        .values
        .iter()
        .filter(|(k, _)| k == "type" || k.is_empty())
        .map(|(_, v)| v.clone())
        .collect();
    let excludes: Vec<String> = p
        .values
        .iter()
        .filter(|(k, _)| k == "exclude-type")
        .map(|(_, v)| v.clone())
        .collect();

    // Same type both selected and excluded: GNU reports BEFORE statting
    // operands (df.c:1749-1767), once per matching -t occurrence — probed
    // `df -t ext4 -t ext4 -x ext4` prints the error twice. No try-help.
    let mut conflict = false;
    for t in &includes {
        if excludes.iter().any(|x| x == t) {
            let _ = writeln!(
                io.stderr,
                "df: {}",
                tr!(
                    ctx,
                    "command.df.error.type_conflict",
                    "file system type '{fstype}' both selected and excluded",
                    fstype = t
                )
            );
            conflict = true;
        }
    }
    if conflict {
        return Ok(1);
    }

    // Size resolution: the last size option (validated in argv order
    // above) wins; with none, -P mode uses POSIXLY_CORRECT ? 512 : 1024
    // (df.c:1733-1736), otherwise humblock's env chain — DF_BLOCK_SIZE,
    // BLOCK_SIZE, BLOCKSIZE, then the same default — with
    // "human-readable"/"si" keywords and invalid specs falling back to
    // the default silently (probed).
    let render: Render = match pending_size {
        Some(SizeSpec::Human { si1000 }) => Render::Human { si1000 },
        Some(SizeSpec::Blocks {
            size,
            si,
            b,
            base1024,
        }) => Render::Blocks {
            size,
            si,
            b,
            base1024,
        },
        None => {
            let default_size: u64 = if env_set(ctx.env, "POSIXLY_CORRECT") {
                512
            } else {
                1024
            };
            if seen_p {
                Render::Blocks {
                    size: default_size,
                    si: false,
                    b: false,
                    base1024: false,
                }
            } else {
                let spec = ["DF_BLOCK_SIZE", "BLOCK_SIZE", "BLOCKSIZE"]
                    .iter()
                    .find_map(|k| ctx.env.get(k))
                    .map(|v| v.to_string_lossy().into_owned());
                match spec.as_deref().map(parse_block_size) {
                    Some(SizeParse::Ok(SizeSpec::Human { si1000 })) => Render::Human { si1000 },
                    Some(SizeParse::Ok(SizeSpec::Blocks {
                        size,
                        si,
                        b,
                        base1024,
                    })) => Render::Blocks {
                        size,
                        si,
                        b,
                        base1024,
                    },
                    _ => Render::Blocks {
                        size: default_size,
                        si: false,
                        b: false,
                        base1024: false,
                    },
                }
            }
        }
    };

    // Mode selection (df.c:1742-1747): -i and --output already excluded;
    // inodes beats posix, human beats posix.
    let autoscale = matches!(render, Render::Human { .. });
    let mode = if output_mode {
        Mode::Output
    } else if seen_i {
        Mode::Inodes
    } else if autoscale {
        Mode::Human
    } else if seen_p {
        Mode::Posix
    } else {
        Mode::Default
    };

    // Column selection (get_field_list df.c:502-562; -T inserts FSTYPE
    // after SOURCE in every non-output mode). An accumulated-empty
    // --output list expands to all 12 fields (df.c:555-556).
    let fields: Vec<Field> = if mode == Mode::Output {
        if out_fields.is_empty() {
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
        } else {
            out_fields
        }
    } else {
        let mut v = vec![Field::Source];
        if seen_t {
            v.push(Field::Fstype);
        }
        match mode {
            Mode::Inodes => v.extend([
                Field::Itotal,
                Field::Iused,
                Field::Iavail,
                Field::Ipcent,
                Field::Target,
            ]),
            _ => v.extend([
                Field::Size,
                Field::Used,
                Field::Avail,
                Field::Pcent,
                Field::Target,
            ]),
        }
        v
    };

    let fstype_selected = |fstype: &str| {
        (includes.is_empty() || includes.iter().any(|t| t == fstype))
            && !excludes.iter().any(|t| t == fstype)
    };

    // Row collection. Operands stat first (automount_stat_err path); each
    // successful operand prints its own row — GNU does NOT dedup
    // (`df . .` -> two rows, probed).
    let mut rows: Vec<Row> = Vec::new();
    let mut failed = false;
    let mut push_volume = |v: &Volume, file: String| {
        // fsu_blocks==0 mounts are skipped unless -a or explicitly
        // listed (df.c:1082-1083); listed operands always show.
        if v.total == 0 && !all_flag && file == "-" {
            return;
        }
        if v.remote && local_only {
            return;
        }
        if !fstype_selected(&v.fstype) {
            return;
        }
        rows.push(Row {
            source: v.root.clone(),
            fstype: v.fstype.clone(),
            size: v.total,
            used: v.total.saturating_sub(v.free),
            avail: v.available,
            file,
            target: v.root.clone(),
            is_total: false,
        });
    };

    if p.positionals.is_empty() {
        for v in local_volumes() {
            push_volume(&v, "-".to_string());
        }
    } else {
        let canon_cwd = winux_core::strip_verbatim(
            std::fs::canonicalize(&ctx.cwd).unwrap_or_else(|_| ctx.cwd.clone()),
        )
        .to_string_lossy()
        .into_owned();
        for operand in &p.positionals {
            let name = operand.to_string_lossy().into_owned();
            // Windows drive-letter forms (`C:\`, `C:\Windows`) are the
            // operands the WSL oracle cannot stat — not POSIX paths — so
            // they take the GNU missing-path branch (probed; the 1.x C++
            // applet keeps its drive model, r=c divergence recorded).
            if is_drive_letter_form(&name) {
                let _ = writeln!(
                    io.stderr,
                    "df: {}",
                    tr!(
                        ctx,
                        "command.df.error.no_such_file",
                        "{path}: No such file or directory",
                        path = quotef(name.as_bytes())
                    )
                );
                failed = true;
                continue;
            }
            // the shell-namespace boundary (#1488): a POSIX spelling
            // (/tmp/x, /mnt/d/x, ...) names its native path; the file
            // column keeps the operand text like GNU's get_point.
            let name = match winux_core::path::normalize_operand(ctx.env, &ctx.cwd, &name) {
                m if m != name
                    && (Path::new(&m).is_absolute()
                        || winux_core::path::resolve_pseudo_device(&m).is_some()) =>
                {
                    m
                }
                _ => name,
            };
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
                            if let Some(pos) = p2.rfind(['/', '\\']) {
                                p2.truncate(pos);
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
                Err(e) => {
                    // GNU reports the real stat errno (error(0, err, ...)
                    // df.c:1782); a trailing slash on a non-directory is
                    // ENOTDIR, anything unresolvable is ENOENT.
                    let why = if e.kind() == std::io::ErrorKind::NotADirectory
                        || (name.ends_with(['/', '\\'])
                            && std::fs::metadata(name.trim_end_matches(['/', '\\']))
                                .map(|m| m.is_file())
                                .unwrap_or(false))
                    {
                        "Not a directory"
                    } else {
                        "No such file or directory"
                    };
                    let _ = writeln!(
                        io.stderr,
                        "df: {}",
                        tr!(
                            ctx,
                            "command.df.error.cannot_stat",
                            "{path}: {reason}",
                            path = quotef(name.as_bytes()),
                            reason = why
                        )
                    );
                    failed = true;
                    continue;
                }
            };
            match mount_of(&resolved).and_then(|m| volume_stats(&m)) {
                Some(v) => push_volume(&v, name.clone()),
                None => {
                    let _ = writeln!(
                        io.stderr,
                        "df: {}",
                        tr!(
                            ctx,
                            "command.df.error.no_such_file",
                            "{path}: No such file or directory",
                            path = quotef(name.as_bytes())
                        )
                    );
                    failed = true;
                }
            }
        }
    }

    // `no file systems processed` only when the run did not already fail
    // on an operand (df.c:1844-1849, probed `df -t bogusfs /missing`
    // prints just the stat error).
    if rows.is_empty() {
        if !failed {
            let _ = writeln!(
                io.stderr,
                "df: {}",
                tr!(
                    ctx,
                    "command.df.error.no_file_systems",
                    "no file systems processed"
                )
            );
        }
        return Ok(1);
    }

    // --total summary row (df.c:1837-1841).
    if total_flag {
        let (mut sz, mut us, mut av) = (0u64, 0u64, 0u64);
        for r in &rows {
            sz = sz.saturating_add(r.size);
            us = us.saturating_add(r.used);
            av = av.saturating_add(r.avail);
        }
        rows.push(Row {
            source: "total".into(),
            fstype: "-".into(),
            size: sz,
            used: us,
            avail: av,
            file: "-".into(),
            target: String::new(), // computed from source_used in cell_of
            is_total: true,
        });
    }

    let source_used = fields.contains(&Field::Source);

    // Headers.
    let block_label = match render {
        Render::Blocks {
            size,
            si: _,
            b,
            base1024,
        } => format!("{}-blocks", size_label(size, base1024, b)),
        _ => String::new(),
    };
    let posix_block_size = match render {
        Render::Blocks { size, .. } => size,
        _ => 1024,
    };
    let header: Vec<String> = fields
        .iter()
        .map(|&f| header_of(f, mode, &block_label, posix_block_size))
        .collect();
    let cells: Vec<Vec<String>> = rows
        .iter()
        .map(|r| {
            fields
                .iter()
                .map(|&f| cell_of(r, f, render, source_used))
                .collect()
        })
        .collect();
    let widths: Vec<usize> = fields
        .iter()
        .enumerate()
        .map(|(i, &f)| {
            field_min_width(f).max(header[i].chars().count()).max(
                cells
                    .iter()
                    .map(|row| row[i].chars().count())
                    .max()
                    .unwrap_or(0),
            )
        })
        .collect();

    // print_table (df.c:377-409): single-space separators, columns
    // aligned to their width, and MBA_NO_RIGHT_PAD on the last column —
    // left-aligned last columns are emitted unpadded while right-aligned
    // ones keep their left pad, header row included (#1657).
    let out = &mut *io.stdout;
    let last = fields.len() - 1;
    for (i, h) in header.iter().enumerate() {
        if i > 0 {
            write!(out, " ")?;
        }
        if i == last && field_is_left(fields[i]) {
            write!(out, "{h}")?;
        } else if field_is_left(fields[i]) {
            write!(out, "{:<width$}", h, width = widths[i])?;
        } else {
            write!(out, "{:>width$}", h, width = widths[i])?;
        }
    }
    writeln!(out)?;
    for row in &cells {
        for (i, c) in row.iter().enumerate() {
            if i > 0 {
                write!(out, " ")?;
            }
            if i == last && field_is_left(fields[i]) {
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
        // missing operand: rc 1, GNU wording (quotef'd — backslash names
        // are quoted, plain POSIX paths are not, probed 9.4)
        let (r, _, e) = run(&["Z:\\definitely-missing-path"], &sc);
        assert_eq!(r, 1);
        assert!(
            e.contains("df: 'Z:\\definitely-missing-path': No such file or directory"),
            "got {e}"
        );
        // Windows drive-letter operands are the oracle's missing-path
        // class: rc 1, empty stdout, quotef'd stderr
        for name in ["C:\\", "C:\\Windows"] {
            let (r, out, e) = run(&[name], &sc);
            assert_eq!(r, 1, "{name}");
            assert_eq!(out, "", "{name} prints nothing");
            assert!(
                e.contains(&format!("df: '{}': No such file or directory", name)),
                "{name}: {e}"
            );
        }
        // --output=source C:\ is rc 1 + empty
        let (r, out, e) = run(&["--output=source", "C:\\"], &sc);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(
            e.contains("df: 'C:\\': No such file or directory"),
            "got {e}"
        );
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
        // #1657 byte shape: NO line — header or data — may carry trailing
        // whitespace (last column MBA_NO_RIGHT_PAD, df.c:397).
        for line in out.lines() {
            assert_eq!(line, line.trim_end(), "trailing blanks: {line:?}");
        }
    }

    #[test]
    fn last_column_shape_all_views() {
        // #1657: every mode's last column is unpadded — left-aligned
        // (target/file/source/fstype) and right-aligned alike.
        let sc = ScratchCtx::new("df2b");
        for args in [
            &[][..],
            &["-h"][..],
            &["-P"][..],
            &["-i"][..],
            &["-T"][..],
            &["--output"][..],
            &["--output=source"][..],
            &["--output=size"][..],
            &["--output=pcent"][..],
            &["--output=file"][..],
            &["--output=fstype"][..],
            &["--output=source,fstype"][..],
            &["--output=target,file"][..],
            &["--output=source,target"][..],
            &["--total"][..],
            &["--output=size,target", "--total"][..],
        ] {
            let (_, out, _) = run(args, &sc);
            for line in out.lines() {
                assert_eq!(line, line.trim_end(), "{args:?} trailing blanks: {line:?}");
            }
        }
        // min widths: a lone right-aligned column is left-padded to the
        // field's minimum (GNU pads " Size" to 5 for -h single column);
        // a lone left column emits bare.
        let (_, out, _) = run(&["--output=source"], &sc);
        assert_eq!(out.lines().next().unwrap(), "Filesystem");
        let (_, out, _) = run(&["--output=fstype"], &sc);
        assert_eq!(out.lines().next().unwrap(), "Type");
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
    fn print_type_column() {
        let sc = ScratchCtx::new("df4b");
        // -T inserts Type after Filesystem in every non-output mode.
        let (_, out, _) = run(&["-T"], &sc);
        let hdr = out.lines().next().unwrap();
        assert!(hdr.starts_with("Filesystem") && hdr.contains(" Type "));
        assert!(hdr.contains("1K-blocks"));
        let (_, out, _) = run(&["-T", "-i"], &sc);
        let hdr = out.lines().next().unwrap();
        assert!(hdr.contains(" Type ") && hdr.contains("Inodes"));
        let (_, out, _) = run(&["-T", "-P"], &sc);
        let hdr = out.lines().next().unwrap();
        assert!(hdr.contains(" Type ") && hdr.contains("1024-blocks") && hdr.contains("Capacity"));
        // -T with --output is mutually exclusive, either order
        for args in [
            &["-T", "--output=source"][..],
            &["--output=source", "-T"][..],
        ] {
            let (r, _, e) = run(args, &sc);
            assert_eq!(r, 1, "{args:?}");
            assert!(
                e.contains("options -T and --output are mutually exclusive"),
                "{args:?}: {e}"
            );
        }
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
        // mutual exclusivity, sequential order: the second option names
        // the pair (probed `df -i -P --output` -> "-i and --output").
        let (r, _, e) = run(&["-P", "--output=source"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("options -P and --output are mutually exclusive"));
        let (r, _, e) = run(&["-i", "--output=source"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("options -i and --output are mutually exclusive"));
        // the error precedes --help (probed `df -i --output --help` rc 1)
        let (r, _, e) = run(&["-i", "--output", "--help"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("mutually exclusive"));
        // ...but help first wins cleanly
        let (r, _, _) = run(&["--help", "-T", "--output"], &sc);
        assert_eq!(r, 0);
        // -i then -P then --output names -i (INODES_MODE is checked first)
        let (r, _, e) = run(&["-i", "-P", "--output"], &sc);
        assert_eq!(r, 1);
        assert!(
            e.contains("options -i and --output are mutually exclusive"),
            "{e}"
        );
        // a -T before -P still names -P (header stays DEFAULT; probed
        // `df -T -P --output` -> "-P and --output")
        let (r, _, e) = run(&["-T", "-P", "--output"], &sc);
        assert_eq!(r, 1);
        assert!(
            e.contains("options -P and --output are mutually exclusive"),
            "{e}"
        );
        // field decode happens at the --output argv position AFTER the
        // mutex check (probed both directions)
        let (r, _, e) = run(&["--output=bogus", "-i"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("field 'bogus' unknown"), "{e}");
        let (r, _, e) = run(&["-i", "--output=bogus"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("mutually exclusive"), "{e}");
        let (r, _, e) = run(&["-i", "--output=source,source"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("mutually exclusive"), "{e}");
    }

    #[test]
    fn output_columns() {
        let sc = ScratchCtx::new("df6");
        let (_, out, _) = run(&["--output=source,fstype"], &sc);
        let hdr = out.lines().next().unwrap();
        assert!(hdr.contains("Filesystem") && hdr.contains("Type"));
        assert!(out.lines().count() >= 2);
        // bare --output = all 12 columns
        let (_, out, _) = run(&["--output"], &sc);
        let hdr = out.lines().next().unwrap();
        for h in ["Filesystem", "Type", "Inodes", "IUsed", "IFree", "IUse%"] {
            assert!(hdr.contains(h), "missing {h} in {hdr}");
        }
        // inode columns on Windows: `-` placeholders (documented)
        let row = out.lines().nth(1).unwrap();
        assert!(row.contains(" - "));
        // --output=target,file (GNU verified header order)
        let (_, out, _) = run(&["--output=target,file"], &sc);
        let hdr = out.lines().next().unwrap();
        assert!(hdr.contains("Mounted on") && hdr.contains("File"));
        // abbreviations of --output are the optional_argument form too
        let (_, out, _) = run(&["--out=source"], &sc);
        assert!(out.lines().next().unwrap().starts_with("Filesystem"));
        let (r, _, _) = run(&["--out"], &sc);
        assert_eq!(r, 0);
        // repeated --output= ACCUMULATES fields in argv order (probed):
        // `--output=source --output=target` prints both columns, and a
        // bare --output contributes nothing rather than all-12.
        let (_, out, _) = run(&["--output=source", "--output=target"], &sc);
        let hdr = out.lines().next().unwrap();
        assert!(
            hdr.contains("Filesystem") && hdr.contains("Mounted on"),
            "{hdr}"
        );
        let (_, out, _) = run(&["--output=target", "--output=source"], &sc);
        let hdr = out.lines().next().unwrap();
        assert!(hdr.starts_with("Mounted on"), "{hdr}");
        let (_, out, _) = run(&["--output", "--output=target"], &sc);
        assert_eq!(out.lines().next().unwrap(), "Mounted on");
        let (_, out, _) = run(&["--output=target", "--output"], &sc);
        assert_eq!(out.lines().next().unwrap(), "Mounted on");
        // duplicates across occurrences still error (probed)
        let (r, _, e) = run(&["--output=source", "--output=source"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("field 'source' used more than once"), "{e}");
    }

    #[test]
    fn type_filters() {
        let sc = ScratchCtx::new("df7");
        // -t with an impossible type: no rows -> GNU wording rc 1
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
        // -t and -x on the same type: hard error before statting
        // (df.c:1749-1767, probed `df -t ext4 -x ext4`)
        let vols = local_volumes();
        if let Some(v) = vols.first() {
            for args in [
                &["-t", v.fstype.as_str(), "-x", v.fstype.as_str()][..],
                &["-x", v.fstype.as_str(), "-t", v.fstype.as_str()][..],
            ] {
                let (r, _, e) = run(args, &sc);
                assert_eq!(r, 1, "{args:?}");
                assert!(e.contains("both selected and excluded"), "{args:?}: {e}");
            }
        }
    }

    #[test]
    fn total_row() {
        let sc = ScratchCtx::new("df8");
        let (_, out, _) = run(&["--total"], &sc);
        let last = out.lines().last().unwrap();
        assert!(last.trim_start().starts_with("total"));
        assert!(last.trim_end().ends_with('-'));
        // rc 0 with --total
        let (r, _, _) = run(&["--total"], &sc);
        assert_eq!(r, 0);
        // the word moves into the target cell when source is absent
        // (probed `--output=size,target --total` -> last row "total").
        let (_, out, _) = run(&["--output=size,target", "--total"], &sc);
        let last = out.lines().last().unwrap();
        assert!(last.ends_with("total"), "got {last:?}");
        let (_, out, _) = run(&["--output=size", "--total"], &sc);
        let last = out.lines().last().unwrap();
        assert!(
            !last.contains("total"),
            "no marker without source/target: {last:?}"
        );
    }

    #[test]
    fn block_sizes() {
        let sc = ScratchCtx::new("df9");
        // -B2048 normalizes the header (probed: 2K-blocks)
        let (_, out, _) = run(&["-B", "2048"], &sc);
        assert!(out.lines().next().unwrap().contains("2K-blocks"));
        // -B1M header
        let (_, out, _) = run(&["-B", "1M"], &sc);
        assert!(out.lines().next().unwrap().contains("1M-blocks"));
        // -m is the BSD-compat 1M alias (df.c:1657)
        let (_, out, _) = run(&["-m"], &sc);
        assert!(out.lines().next().unwrap().contains("1M-blocks"));
        // decimal specs pick the "more exact" base + B suffix (probed)
        let (_, out, _) = run(&["-B", "1000"], &sc);
        assert!(out.lines().next().unwrap().contains("1kB-blocks"));
        let (_, out, _) = run(&["-B", "2000"], &sc);
        assert!(out.lines().next().unwrap().contains("2kB-blocks"));
        let (_, out, _) = run(&["-B", "1000000"], &sc);
        assert!(out.lines().next().unwrap().contains("1MB-blocks"));
        let (_, out, _) = run(&["-B", "1024000"], &sc);
        assert!(out.lines().next().unwrap().contains("1.1MB-blocks"));
        let (_, out, _) = run(&["-B", "1536"], &sc);
        assert!(out.lines().next().unwrap().contains("1.6kB-blocks"));
        // -B 1 / -B 512 byte-size labels
        let (_, out, _) = run(&["-B", "1"], &sc);
        assert!(out.lines().next().unwrap().contains("1B-blocks"));
        let (_, out, _) = run(&["-B", "512"], &sc);
        assert!(out.lines().next().unwrap().contains("512B-blocks"));
        // suffix-only specs suffix the data cells (probed)
        let (_, out, _) = run(&["-B", "K"], &sc);
        let row = out.lines().nth(1).unwrap();
        assert!(row.contains('K'), "cells carry K suffix: {row}");
        let (_, out, _) = run(&["-B", "KiB"], &sc);
        assert!(out.lines().nth(1).unwrap().contains("KiB"));
        assert!(out.lines().next().unwrap().contains("1KiB-blocks"));
        // digit-led KiB stays clean
        let (_, out, _) = run(&["-B", "1KiB"], &sc);
        assert!(out.lines().next().unwrap().contains("1K-blocks"));
        assert!(!out.lines().nth(1).unwrap().contains("KiB"));
        // invalid spec: GNU wording, rc 1, NO try-help line — and GNU
        // splits the failure classes (probed): no usable number ->
        // "invalid -B argument", bad unit tail -> "invalid suffix in".
        let (r, _, e) = run(&["-B", "c"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid -B argument 'c'"));
        assert!(!e.contains("--help"), "invalid -B has no try line: {e}");
        let (r, _, e) = run(&["-B", "1X"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid suffix in -B argument '1X'"), "{e}");
        let (r, _, e) = run(&["-B", "Ki"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid suffix in -B argument 'Ki'"), "{e}");
        let (r, _, e) = run(&["-B", "iB"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid -B argument 'iB'"), "{e}");
        // --block-size spelling in the message
        let (r, _, e) = run(&["--block-size", "zz"], &sc);
        assert_eq!(r, 1);
        assert!(
            e.contains("invalid suffix in --block-size argument 'zz'"),
            "{e}"
        );
        let (r, _, e) = run(&["--block-size", "iB"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid --block-size argument 'iB'"), "{e}");
        // -B keywords accepted (probed -B human-readable / -B si)
        let (r, out, _) = run(&["-B", "human-readable"], &sc);
        assert_eq!(r, 0);
        assert!(out.lines().next().unwrap().contains("Size"));
        // zero is invalid
        let (r, _, _) = run(&["-B", "0"], &sc);
        assert_eq!(r, 1);
        // -k still works
        let (r, out, _) = run(&["-k"], &sc);
        assert_eq!(r, 0);
        assert!(out.lines().next().unwrap().contains("1K-blocks"));
    }

    #[test]
    fn size_option_ordering() {
        let sc = ScratchCtx::new("df9b");
        // last size option wins (probed each direction on WSL 9.4)
        let (_, out, _) = run(&["-h", "-B", "2048"], &sc);
        assert!(out.lines().next().unwrap().contains("2K-blocks"), "{out}");
        let (_, out, _) = run(&["-B", "2048", "-h"], &sc);
        assert!(out.lines().next().unwrap().contains("Size"), "{out}");
        let (_, out, _) = run(&["-h", "-k"], &sc);
        assert!(out.lines().next().unwrap().contains("1K-blocks"), "{out}");
        let (_, out, _) = run(&["-k", "-h"], &sc);
        assert!(out.lines().next().unwrap().contains("Size"), "{out}");
        let (_, out, _) = run(&["-B", "1024", "-m"], &sc);
        assert!(out.lines().next().unwrap().contains("1M-blocks"), "{out}");
        let (_, out, _) = run(&["-m", "-B", "2048"], &sc);
        assert!(out.lines().next().unwrap().contains("2K-blocks"), "{out}");
        // -P -B: literal decimal label, env-independent
        let (_, out, _) = run(&["-P", "-B", "2048"], &sc);
        assert!(out.lines().next().unwrap().contains("2048-blocks"), "{out}");
    }

    #[test]
    fn env_block_size_defaults() {
        let sc = ScratchCtx::new("df9c");
        // POSIXLY_CORRECT defaults to 512 even without -P (probed)
        {
            let mut sc = ScratchCtx::new("df9d");
            sc.env.set("POSIXLY_CORRECT", "1");
            let (_, out, _) = run(&[], &sc);
            assert!(out.lines().next().unwrap().contains("512B-blocks"), "{out}");
            let (_, out, _) = run(&["-P"], &sc);
            assert!(out.lines().next().unwrap().contains("512-blocks"), "{out}");
        }
        {
            let mut sc = ScratchCtx::new("df9e");
            sc.env.set("DF_BLOCK_SIZE", "1M");
            let (_, out, _) = run(&[], &sc);
            assert!(out.lines().next().unwrap().contains("1M-blocks"), "{out}");
            // invalid env falls back silently (probed DF_BLOCK_SIZE=bad)
            let mut sc = ScratchCtx::new("df9f");
            sc.env.set("DF_BLOCK_SIZE", "bad");
            let (_, out, _) = run(&[], &sc);
            assert!(out.lines().next().unwrap().contains("1K-blocks"), "{out}");
            // human-readable keyword
            let mut sc = ScratchCtx::new("df9g");
            sc.env.set("DF_BLOCK_SIZE", "human-readable");
            let (_, out, _) = run(&[], &sc);
            assert!(out.lines().next().unwrap().contains("Size"), "{out}");
            // -P ignores DF_BLOCK_SIZE (probed)
            let mut sc = ScratchCtx::new("df9h");
            sc.env.set("DF_BLOCK_SIZE", "1M");
            let (_, out, _) = run(&["-P"], &sc);
            assert!(out.lines().next().unwrap().contains("1024-blocks"), "{out}");
        }
        let _ = &sc;
    }

    #[test]
    fn math_units() {
        assert_eq!(ceil_div(9, 4), 3);
        assert_eq!(ceil_div(8, 4), 2);
        // GNU pcenceil
        assert_eq!(pct(1, 3), 25);
        assert_eq!(pct(2, 2), 50);
        assert_eq!(pct(0, 5), 0);
        // human: ceiling autoscale — <10 one decimal, >=10 integer,
        // bytes plain (probed 251G/122G/117G on real sizes)
        assert_eq!(human(0, false), "0");
        assert_eq!(human(512, false), "512");
        assert_eq!(human(4096, false), "4.0K");
        assert_eq!(human(263112772 * 1024u64, false), "251G");
        assert_eq!(human(127292808 * 1024u64, false), "122G"); // 121.4 -> 122 ceil
        assert_eq!(human(1024 * 1024 * 1024 * 1024, false), "1.0T");
        assert_eq!(human(4_000_000, true), "4.0M");
        assert_eq!(human(1023, false), "1023");
        assert_eq!(human(1024 * 1024 - 1, false), "1.0M"); // promotes
                                                           // block sizes
        let blocks = |s: &str| match parse_block_size(s) {
            SizeParse::Ok(SizeSpec::Blocks { size, .. }) => Some(size),
            _ => None,
        };
        assert_eq!(blocks("2048"), Some(2048));
        assert_eq!(blocks("1M"), Some(1024 * 1024));
        assert_eq!(blocks("kB"), Some(1000));
        assert_eq!(blocks("MB"), Some(1000000));
        assert_eq!(blocks("KiB"), Some(1024));
        assert_eq!(blocks("0x10"), Some(16));
        // the two GNU failure classes (probed)
        assert!(matches!(parse_block_size("c"), SizeParse::InvalidArg));
        assert!(matches!(parse_block_size(""), SizeParse::InvalidArg));
        assert!(matches!(parse_block_size("0"), SizeParse::InvalidArg));
        assert!(matches!(parse_block_size("B"), SizeParse::InvalidArg));
        assert!(matches!(parse_block_size("iB"), SizeParse::InvalidArg));
        assert!(matches!(parse_block_size("1X"), SizeParse::InvalidSuffix));
        assert!(matches!(parse_block_size("1kb"), SizeParse::InvalidSuffix));
        assert!(matches!(parse_block_size("Ki"), SizeParse::InvalidSuffix));
        assert!(matches!(parse_block_size("zz"), SizeParse::InvalidSuffix));
        assert!(matches!(
            parse_block_size("human-readable"),
            SizeParse::Ok(SizeSpec::Human { si1000: false })
        ));
        assert!(matches!(
            parse_block_size("si"),
            SizeParse::Ok(SizeSpec::Human { si1000: true })
        ));
        assert_eq!(size_label(2048, false, false), "2K");
        assert_eq!(size_label(1024 * 1024, false, false), "1M");
        assert_eq!(size_label(1024, false, false), "1K");
        assert_eq!(size_label(512, false, false), "512B");
        assert_eq!(size_label(1000, false, false), "1kB");
        assert_eq!(size_label(1000000, false, false), "1MB");
        assert_eq!(size_label(1024000, false, false), "1.1MB");
        assert_eq!(size_label(1536, false, false), "1.6kB");
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
        // GNU does NOT dedup operands: two same-fs operands -> two rows
        // (probed `df . .` and `df f .`).
        std::fs::write(sc.path("g"), "x").unwrap();
        let (_, out, _) = run(&["g", "sub/deep/f"], &sc);
        assert_eq!(out.lines().count(), 3); // header + 2 rows
        let (_, out, _) = run(&[".", "."], &sc);
        assert_eq!(out.lines().count(), 3);
        // file column is the operand text, target is the mount root
        // (probed `df --output=file,target f` -> "f    /").
        let (_, out, _) = run(&["--output=file,target", "g"], &sc);
        let row = out.lines().nth(1).unwrap();
        assert!(row.starts_with("g"), "file cell is operand text: {row}");
        assert!(
            row.contains(":\\") || row.contains(r"\\"),
            "target is mount root: {row}"
        );
        // mount_of unit checks
        assert_eq!(mount_of(r"C:\x\y"), Some(r"C:\".to_string()));
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
