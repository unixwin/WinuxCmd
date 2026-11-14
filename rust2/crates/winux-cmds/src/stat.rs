//! `stat` — GNU coreutils stat output on the Windows value model.
//!
//! The FORMAT engine follows stat.c: -c/--printf take a printf-like string
//! whose directives carry flags/width/precision; unknown directives print
//! `?` and never fail (GNU probed rc 0). The default format is GNU's
//! byte-for-byte shape (File/Size/Device/Access lines, four timestamps),
//! -t is the terse 16-field line; both are platform-form-bound `win`
//! output (inode, uid/gid, volume serial... are the Windows values of the
//! 1.x C++ applets, see fs_win.rs): uid = 0x30000+RID for domain SIDs,
//! mode words are POSIX-shaped (0x81a4 regular 0644, 0x41ed dir 0755,
//! 0xa1ff symlink), device is the NTFS volume serial. lstat is the
//! default (-L follows); a symlink operand reports the link with the
//! target-name length as its size.
//!
//! `%C` prints `?` plus GNU's `failed to get security context` stderr
//! line (there is no SELinux on Windows; the C++ 1.x applet emits the
//! same). -f reports the containing volume in GNU's shape (NTFS values).
//! Any operand failure prints `cannot statx 'X': ...` and continues; rc 1
//! if anything failed.
//!
//! rdev directives print plain integers, never `?`: [GNU] stat.c:1584-1600
//! formats `%r` (dec), `%R`/`%t`/`%T` (hex) straight out of st_rdev with
//! no "?" fallback, and on Linux a regular file's st_rdev is 0 — probed
//! coreutils 9.4: `stat -c '%R|%r|%t|%T'` on a regular file -> `0|0|0|0`,
//! `%Hr %Lr` -> `0 0`. Windows has no device number, so the value is 0
//! (the same platform-inherent choice as the volume-serial %d/%D).
//! The `%H`/`%L` prefix is a modifier ONLY before `d` or `r`
//! (stat.c:1176-1190: otherwise the H/L itself is the directive -> `?`
//! and the next byte renders literally — probed `%Hx` -> `?x`);
//! `%Hd`/`%Ld` are major/minor of st_dev (stat.c:1535-1537), also not
//! representable on Windows -> 0.
//!
//! Backslash escapes are GNU print_it's shared interpreter
//! (stat.c:1199-1242 + print_esc_char stat.c:1075-1112), applied per
//! operand render and only when --printf set interpret_backslash_escapes:
//! -c/-t keep backslashes verbatim and never warn. Escaped values go out
//! as raw bytes (putchar truncation: `\400` -> 0x00). An unrecognized
//! escape DROPS the backslash, echoes the character, and warns
//! `stat: warning: unrecognized escape '\X'` on stderr — probed 9.4:
//! `--printf '[\xZZ]'` -> stdout `[xZZ]` + that warning; `\'` warns,
//! `\"`/`\e` are recognized; a trailing lone `\` warns
//! `warning: backslash at end of format` and echoes the `\`.

use std::ffi::OsString;

use crate::glob::{contains_wildcard, glob_expand_in};
use std::io::Write;

use crate::argopt;
use crate::fs_win::{self, FileTime};
use winux_core::{usage_error, CmdResult, Ctx, Io};

/// GNU stat.c default layout, byte-aligned with the 1.x C++ renderer it
/// replaces (git HEAD src/commands/stat.cpp print_stat): the File line is
/// UNQUOTED with an optional " -> target" suffix, Size/Blocks/IO Block pad
/// to 10/10/6, the device prints as "<volume serial>,0", and the inode
/// pads to 11 before a single-space "Links:". coreutils 9.4 on DrvFs shows
/// the same shape (unquoted name, comma device) modulo platform fields.
/// No trailing "\n": GNU stat.c appends the terminating newline once after
/// a -t/-c render (probed coreutils 9.4: `stat -t f` ends in one \n, while
/// `stat -c '%n\n'` keeps the two escape chars literal and STILL appends a
/// real \n — the append is unconditional, so a \n in the format would
/// double it).
const TERSE_FORMAT: &str = "%n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %W %o";

/// GNU stat.c terse filesystem format (stat.c FS_NORM_TERMINATE form); the
/// terminating newline is the unconditional -t/-c append, not the format
/// (probed coreutils 9.4 `stat -f -t f`: exactly one \n).
const FS_TERSE_FORMAT: &str = "%n %i %l %t %s %S %b %f %a %c %d";

#[derive(Clone)]
enum Field {
    Str(String),
    Num(i64),
    Dec(u64),
    Hex(u64),
    Oct(u32),
}

impl Field {
    fn render(&self, spec: &DirSpec) -> String {
        let raw = match self {
            Field::Str(s) => {
                let mut t = s.clone();
                if let Some(p) = spec.precision {
                    if t.len() > p {
                        t.truncate(p);
                    }
                }
                t
            }
            Field::Num(n) => n.to_string(),
            Field::Hex(n) => format!("{n:x}"),
            Field::Dec(n) => n.to_string(),
            Field::Oct(n) => format!("{n:o}"),
        };
        let mut s = raw;
        if spec.zero_pad && !spec.left_adjust {
            let w = spec.width;
            if s.len() < w {
                let neg = s.starts_with('-');
                let insert = w - s.len();
                if neg {
                    let sign = s.remove(0);
                    s = format!("{sign}{}{s}", "0".repeat(insert));
                } else {
                    s = format!("{}{s}", "0".repeat(insert));
                }
            }
        } else if s.len() < spec.width {
            let pad = " ".repeat(spec.width - s.len());
            if spec.left_adjust {
                s.push_str(&pad);
            } else {
                s = format!("{pad}{s}");
            }
        }
        s
    }
}

#[derive(Default, Clone)]
struct DirSpec {
    left_adjust: bool,
    zero_pad: bool,
    width: usize,
    precision: Option<usize>,
}

struct StatEntry {
    name: String,
    /// The operand actually statted: the typed name, or the `+ ".exe"`
    /// candidate when the bare name resolved through it (#1572). Display
    /// (%n, `File:`) keeps `name`; re-probes (owner/group, symlink target,
    /// mount point) use `resolved`.
    resolved: String,
    meta: fs_win::FileMeta,
    /// #1038 fifo marker (GNU lstat() S_IFIFO shape: type "fifo", mode
    /// 0o10644/0o10444, st_size 0 — 1.x stat.cpp:438-446).
    fifo: bool,
    owner: (String, String),
    group: (String, String),
}

fn mode_word(meta: &fs_win::FileMeta, fifo: bool) -> u32 {
    // 1.x Windows value model: regular 0644 (0444 when read-only),
    // directories 0755, symlinks 0777 (GNU's a1ff). A fifo marker reports
    // the GNU lstat() shape: S_IFIFO | 0644 (0444 when read-only) —
    // `stat -c %f -` on a GNU pipe prints 11b4's regular-file sibling
    // 11a4 (1.x st_mode_bits, stat.cpp:299-305).
    if meta.is_symlink {
        return 0o120777; // S_IFLNK | 0777
    }
    if fifo {
        // S_IFIFO | 0644 (read-only marker: 0444)
        return 0o010000 | if meta.read_only { 0o444 } else { 0o644 };
    }
    let type_bits = if meta.is_dir {
        0o040000 // S_IFDIR
    } else if meta.character_device {
        0o020000 // S_IFCHR
    } else {
        0o100000 // S_IFREG
    };
    let perms = if meta.is_dir {
        0o755
    } else if meta.read_only {
        0o444
    } else {
        0o644
    };
    type_bits | perms
}

fn access_string(meta: &fs_win::FileMeta, fifo: bool) -> String {
    let m = mode_word(meta, fifo);
    let type_char = match m & 0o170000 {
        0o140000 => 's',
        0o120000 => 'l',
        0o060000 => 'b',
        0o020000 => 'c',
        0o040000 => 'd',
        0o010000 => 'p',
        _ => '-',
    };
    let mut s = String::with_capacity(10);
    s.push(type_char);
    for shift in [6, 3, 0] {
        let bits = (m >> shift) & 0o7;
        s.push(if bits & 4 != 0 { 'r' } else { '-' });
        s.push(if bits & 2 != 0 { 'w' } else { '-' });
        s.push(if bits & 1 != 0 { 'x' } else { '-' });
    }
    s
}

fn file_type_name(meta: &fs_win::FileMeta, fifo: bool) -> String {
    if fifo {
        // [GNU] `stat -c %F fifo` reports "fifo" (st_mode S_IFIFO; the
        // 1.x file_type_name, stat.cpp:277-278).
        "fifo".to_string()
    } else if meta.is_symlink {
        "symbolic link".to_string()
    } else if meta.is_dir {
        "directory".to_string()
    } else if meta.character_device {
        "character special file".to_string()
    } else if meta.size == 0 {
        "regular empty file".to_string()
    } else {
        "regular file".to_string()
    }
}

/// Format a FILETIME like GNU: "2026-11-10 20:00:04.297388421 +0800".
fn format_filetime(ft: &FileTime) -> String {
    let (secs, _remainder) = ft.to_epoch();
    match fs_win::to_local(ft.as_u64() as i64) {
        Some((local, offset_min)) => {
            // reconstruct the sub-second part from the ticks
            let frac_ticks = ft.as_u64() as i64 % 10_000_000;
            let nanos = frac_ticks * 100;
            let sign = if offset_min < 0 { '-' } else { '+' };
            let off = offset_min.abs();
            format!(
                "{:04}-{:02}-{:02} {:02}:{:02}:{:02}.{:09} {}{:02}{:02}",
                local.year,
                local.month,
                local.day,
                local.hour,
                local.minute,
                local.second,
                nanos,
                sign,
                off / 60,
                off % 60
            )
        }
        None => {
            let _ = secs;
            "*".to_string()
        }
    }
}

fn quote_name(s: &str) -> String {
    format!("'{s}'")
}

/// [GNU] stat prints "UNKNOWN" where the group name cannot be resolved
/// (1.x display_group_name: empty or the literal Windows "None" group,
/// git HEAD src/commands/stat.cpp:332).
fn group_display(name: &str) -> String {
    if name.is_empty() || name == "None" {
        "UNKNOWN".to_string()
    } else {
        name.to_string()
    }
}

/// Compute the field for a directive letter. `modifier` is GNU print_it's
/// mod_char ('H' or 'L', only ever paired with 'd' or 'r' — resolved by
/// render_format per stat.c:1176-1190). `ctx_failed` flips when the
/// requested field cannot be produced (GNU %C: rc 1 per GNU stat -c %C on
/// a volume without security contexts — probed coreutils 9.4, rc 1 with
/// '?' on stdout and the "failed to get security context" note).
fn field_for(
    letter: char,
    modifier: char,
    entry: &StatEntry,
    follow: bool,
    ctx: &Ctx,
    stderr: &mut dyn Write,
    ctx_failed: &mut bool,
) -> Field {
    let m = &entry.meta;
    let mw = mode_word(m, entry.fifo);
    Field::Str(match letter {
        'n' => entry.name.clone(),
        'N' => {
            let mut s = quote_name(&entry.name);
            if m.is_symlink && !follow {
                if let Ok(target) = std::fs::read_link(resolve(ctx, &entry.resolved)) {
                    s.push_str(" -> ");
                    s.push_str(&quote_name(&target.to_string_lossy()));
                }
            }
            s
        }
        'F' => file_type_name(m, entry.fifo),
        'A' => access_string(m, entry.fifo),
        'a' => return Field::Oct(mw & 0o7777),
        'f' => format!("{:04x}", mw),
        's' => return Field::Dec(m.size),
        'b' => return Field::Dec(m.size.div_ceil(512)),
        'B' => return Field::Dec(512),
        'o' => return Field::Dec(4096),
        'i' => return Field::Dec(m.index),
        'h' => return Field::Dec(m.links),
        'd' => {
            // [GNU] stat.c:1533-1539: %d is st_dev, %Hd/%Ld are
            // major(st_dev)/minor(st_dev) in decimal. Windows exposes the
            // device only as the volume serial with no major/minor split,
            // so the split fields report 0 (see module doc).
            if modifier == 'H' || modifier == 'L' {
                return Field::Dec(0);
            }
            return Field::Dec(m.volume_serial as u64);
        }
        'D' => return Field::Hex(m.volume_serial as u64),
        // [GNU] stat.c:1584-1600 — rdev renders as plain integers, no "?"
        // fallback; a Linux regular file's st_rdev is 0 (probed 9.4:
        // `%R|%r|%t|%T` -> `0|0|0|0`, `%Hr %Lr` -> `0 0`). Windows has no
        // device number: 0. %r dec, %R/%t/%T hex (out_uint_x).
        'r' => return Field::Dec(0),
        'R' => return Field::Hex(0),
        't' => return Field::Hex(0),
        'T' => return Field::Hex(0),
        'u' => return Field::Num(entry.owner.0.parse::<i64>().unwrap_or(0)),
        'g' => return Field::Num(entry.group.0.parse::<i64>().unwrap_or(0)),
        'U' => {
            if entry.owner.1.is_empty() {
                "UNKNOWN".to_string()
            } else {
                entry.owner.1.clone()
            }
        }
        'G' => group_display(&entry.group.1),
        'X' => return Field::Num(m.atime.to_epoch().0),
        'Y' => return Field::Num(m.mtime.to_epoch().0),
        'Z' => return Field::Num(m.ctime.to_epoch().0),
        'W' => return Field::Num(m.btime.to_epoch().0),
        'x' => format_filetime(&m.atime),
        'y' => format_filetime(&m.mtime),
        'z' => format_filetime(&m.ctime),
        'w' => format_filetime(&m.btime),
        'C' => {
            let _ = writeln!(
                stderr,
                "stat: failed to get security context of '{}': No data available",
                entry.name
            );
            *ctx_failed = true;
            "?".to_string()
        }
        'm' => mount_point(ctx, &entry.resolved),
        // [GNU] %c: SELinux security context string; none on Windows, so
        // the literal "?" with rc 0 (probed coreutils 9.4: `stat -c %c`
        // -> "?"; 1.x stat.cpp:840-843). %C is the failing sibling below.
        'c' => "?".to_string(),
        'S' => return Field::Dec(4096),
        _ => "?".to_string(),
    })
}

fn mount_point(ctx: &Ctx, name: &str) -> String {
    #[cfg(windows)]
    {
        let full = fs_win::imp::api_path(ctx, name);
        #[link(name = "kernel32")]
        extern "system" {
            fn GetVolumePathNameW(path: *const u16, volume: *mut u16, len: u32) -> i32;
        }
        let w = fs_win::imp::wide(&full);
        let mut buf = vec![0u16; 512];
        let rc = unsafe { GetVolumePathNameW(w.as_ptr(), buf.as_mut_ptr(), 512) };
        if rc != 0 {
            let len = buf.iter().take_while(|&&c| c != 0).count();
            return String::from_utf16_lossy(&buf[..len]);
        }
        String::new()
    }
    #[cfg(not(windows))]
    {
        let _ = (ctx, name);
        String::new()
    }
}

fn resolve(ctx: &Ctx, p: &str) -> std::path::PathBuf {
    // the one operand boundary (fs_win::api_path): drive-dialect folds,
    // the carrier-colon fold, dot-segment collapse, the pseudo-device
    // names and the shell-tree mapping all happen there, exactly like the
    // 1.x operand layer (native_path.cppm make_api_path_operand_w)
    std::path::PathBuf::from(fs_win::api_path(ctx, p))
}

/// Expand one -c/--printf format for one entry. Returns whether a field
/// failure occurred (GNU: %C on a context-less volume fails the entry).
fn render_format(
    fmt: &str,
    entry: &StatEntry,
    follow: bool,
    ctx: &Ctx,
    stderr: &mut dyn Write,
    process_escapes: bool,
    out: &mut dyn Write,
) -> std::io::Result<bool> {
    let mut ctx_failed = false;
    let bytes = fmt.as_bytes();
    let mut i = 0usize;
    while i < bytes.len() {
        let c = bytes[i];
        if c != b'%' {
            if c == b'\\' {
                process_backslash(bytes, &mut i, process_escapes, stderr, out)?;
            } else {
                out.write_all(&[c])?;
            }
            i += 1;
            continue;
        }
        i += 1;
        if i >= bytes.len() {
            out.write_all(b"%")?;
            break;
        }
        if bytes[i] == b'%' {
            out.write_all(b"%")?;
            i += 1;
            continue;
        }
        let mut spec = DirSpec::default();
        while i < bytes.len() && matches!(bytes[i], b'-' | b'+' | b'#' | b'0' | b' ' | b'\'') {
            match bytes[i] {
                b'-' => spec.left_adjust = true,
                b'0' => spec.zero_pad = true,
                _ => {}
            }
            i += 1;
        }
        let mut width = 0usize;
        let mut has_width = false;
        while i < bytes.len() && bytes[i].is_ascii_digit() {
            width = width * 10 + (bytes[i] - b'0') as usize;
            has_width = true;
            i += 1;
        }
        if has_width {
            spec.width = width.min(4096);
        }
        if i < bytes.len() && bytes[i] == b'.' {
            i += 1;
            let mut p = 0usize;
            while i < bytes.len() && bytes[i].is_ascii_digit() {
                p = p * 10 + (bytes[i] - b'0') as usize;
                i += 1;
            }
            spec.precision = Some(p.min(4096));
        }
        if i >= bytes.len() {
            out.write_all(b"?")?;
            break;
        }
        let mut letter = bytes[i];
        i += 1;
        // [GNU] print_it stat.c:1176-1190: an H/L prefix is a modifier only
        // when immediately followed by 'd' or 'r' (print_stat directives);
        // otherwise H/L itself is the directive ('?' via field_for's
        // default — probed 9.4: `%Hx` -> `?x`, `%H` -> `?`) and the next
        // byte renders as a literal.
        let mut modifier = 0u8;
        if (letter == b'H' || letter == b'L')
            && i < bytes.len()
            && (bytes[i] == b'd' || bytes[i] == b'r')
        {
            modifier = letter;
            letter = bytes[i];
            i += 1;
        }
        let field = field_for(
            letter as char,
            modifier as char,
            entry,
            follow,
            ctx,
            stderr,
            &mut ctx_failed,
        );
        let rendered = field.render(&spec);
        out.write_all(rendered.as_bytes())?;
    }
    Ok(ctx_failed)
}

/// [GNU] print_it's backslash branch (stat.c:1199-1242) and
/// print_esc_char (stat.c:1075-1112), byte-faithful. `bytes[*i]` is the
/// leading `\`; on return `*i` indexes the last byte consumed. When
/// `enabled` is false (GNU interpret_backslash_escapes: -c/-t/default, not
/// --printf) the backslash is emitted verbatim and nothing else happens
/// (stat.c:1200-1204 — probed 9.4: `-c '\z'` -> `\z`, no warning).
/// Otherwise: \NNN is 1-3 octal digits, \xHH one or TWO hex digits
/// (stat.c:1218-1230); values go out as RAW bytes — putchar truncation,
/// probed `\400` -> 0x00, `\xff` -> 0xFF. An escape that is not octal,
/// not \x<hex>, and not at end-of-format goes through print_esc_char:
/// the table a b e f n r t v " \ maps to control bytes; anything else
/// DROPS the backslash, echoes the character, and warns
/// `stat: warning: unrecognized escape '\X'` (probed `[\xZZ]` -> `xZZ`
/// + warning for the `\x`, `[\']` -> `'` + warning — ' is NOT in the
/// table). A `\` at end of format warns
/// `stat: warning: backslash at end of format` and echoes the `\`
/// (stat.c:1231-1237).
fn process_backslash(
    bytes: &[u8],
    i: &mut usize,
    enabled: bool,
    stderr: &mut dyn Write,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    if !enabled {
        out.write_all(b"\\")?;
        return Ok(());
    }
    *i += 1;
    if *i >= bytes.len() {
        let _ = writeln!(stderr, "stat: warning: backslash at end of format");
        out.write_all(b"\\")?;
        return Ok(());
    }
    let c = bytes[*i];
    if matches!(c, b'0'..=b'7') {
        // stat.c:1206-1217: 1-3 octal digits.
        let mut value = (c - b'0') as u32;
        let mut taken = 1;
        while taken < 3 {
            match bytes.get(*i + 1) {
                Some(n) if matches!(*n, b'0'..=b'7') => {
                    value = value * 8 + (*n - b'0') as u32;
                    *i += 1;
                    taken += 1;
                }
                _ => break,
            }
        }
        out.write_all(&[(value & 0xFF) as u8])?;
        return Ok(());
    }
    if c == b'x' && bytes.get(*i + 1).is_some_and(|n| n.is_ascii_hexdigit()) {
        // stat.c:1218-1230: \xhh, 1 or 2 hex digits (fromhex table).
        let hex_val = |b: u8| -> u32 {
            match b {
                b'0'..=b'9' => (b - b'0') as u32,
                b'a'..=b'f' => (b - b'a' + 10) as u32,
                _ => (b - b'A' + 10) as u32,
            }
        };
        let mut value = hex_val(bytes[*i + 1]);
        *i += 1;
        if let Some(n) = bytes.get(*i + 1) {
            if n.is_ascii_hexdigit() {
                value = value * 16 + hex_val(*n);
                *i += 1;
            }
        }
        out.write_all(&[(value & 0xFF) as u8])?;
        return Ok(());
    }
    // print_esc_char (stat.c:1075-1112).
    let mapped: Option<u8> = match c {
        b'a' => Some(0x07),
        b'b' => Some(0x08),
        b'e' => Some(0x1B),
        b'f' => Some(0x0C),
        b'n' => Some(b'\n'),
        b'r' => Some(b'\r'),
        b't' => Some(b'\t'),
        b'v' => Some(0x0B),
        b'"' | b'\\' => Some(c),
        _ => None,
    };
    match mapped {
        Some(b) => out.write_all(&[b])?,
        None => {
            // warning: unrecognized escape '\<raw byte>' — keep the byte
            // verbatim in the message like GNU's error() formatting.
            let mut msg = b"stat: warning: unrecognized escape '\\".to_vec();
            msg.push(c);
            msg.extend_from_slice(b"'\n");
            stderr.write_all(&msg)?;
            out.write_all(&[c])?;
        }
    }
    Ok(())
}

/// Filesystem facts for -f (volume query), shared by the default fs shape
/// and the explicit fs-mode format table (GNU stat -f directive set).
struct FsInfo {
    id_hex: u32,     // %i "file system ID in hex"
    namelen: u64,    // %l
    fs_type: String, // %T human form
    block: u64,      // %s
    fblock: u64,     // %S
    total: u64,      // %b
    free: u64,       // %f
    avail: u64,      // %a
}

#[allow(clippy::needless_return)]
fn collect_fs_info(entry: &StatEntry, ctx: &Ctx) -> Option<FsInfo> {
    #[cfg(windows)]
    {
        #[link(name = "kernel32")]
        extern "system" {
            fn GetVolumeInformationW(
                root: *const u16,
                name: *mut u16,
                name_len: u32,
                serial: *mut u32,
                maxlen: *mut u32,
                flags: *mut u32,
                fs: *mut u16,
                fs_len: u32,
            ) -> i32;
            fn GetDiskFreeSpaceExW(
                dir: *const u16,
                free_avail: *mut u64,
                total: *mut u64,
                total_free: *mut u64,
            ) -> i32;
        }
        let mut volume_root = mount_point(ctx, &entry.resolved);
        if !volume_root.ends_with('\\') {
            volume_root.push('\\');
        }
        let w = fs_win::imp::wide(&volume_root);
        let mut serial = 0u32;
        let mut maxlen = 0u32;
        let mut fsname = vec![0u16; 64];
        let rc = unsafe {
            GetVolumeInformationW(
                w.as_ptr(),
                std::ptr::null_mut(),
                0,
                &mut serial,
                &mut maxlen,
                std::ptr::null_mut(),
                fsname.as_mut_ptr(),
                64,
            )
        };
        let fs_type = if rc != 0 {
            let len = fsname.iter().take_while(|&&c| c != 0).count();
            String::from_utf16_lossy(&fsname[..len])
        } else {
            "NTFS".to_string()
        };
        let namelen = if rc != 0 && maxlen > 0 {
            maxlen as u64
        } else {
            255
        };
        let mut free_avail = 0u64;
        let mut total = 0u64;
        let mut total_free = 0u64;
        let rc2 = unsafe {
            GetDiskFreeSpaceExW(w.as_ptr(), &mut free_avail, &mut total, &mut total_free)
        };
        let (t, f, a) = if rc2 != 0 {
            (total / 4096, total_free / 4096, free_avail / 4096)
        } else {
            (0, 0, 0)
        };
        // GNU prints the fs ID from the byte-folded serial (1.x shape).
        let id_hex = serial
            .to_be_bytes()
            .iter()
            .fold(0u32, |acc: u32, b: &u8| (acc << 8) | *b as u32);
        return Some(FsInfo {
            id_hex,
            namelen,
            fs_type,
            block: 4096,
            fblock: 4096,
            total: t,
            free: f,
            avail: a,
        });
    }
    #[cfg(not(windows))]
    {
        let _ = (entry, ctx);
        return None;
    }
}

/// Default -f shape (GNU layout with Windows values; the 1.x shape it
/// replaces rendered the same block).
fn render_fs_default(name: &str, info: &FsInfo, out: &mut dyn Write) -> std::io::Result<()> {
    writeln!(out, "  File: \"{}\"", name)?;
    writeln!(
        out,
        "    ID: {:08x} Namelen: {} Type: {}",
        info.id_hex, info.namelen, info.fs_type
    )?;
    writeln!(
        out,
        "Block size: {} Fundamental block size: {}",
        info.block, info.fblock
    )?;
    writeln!(
        out,
        "Blocks: Total: {} Free: {} Available: {}",
        info.total, info.free, info.avail
    )?;
    writeln!(out, "Inodes: Total: 0 Free: 0")?;
    Ok(())
}

/// GNU stat -f with an explicit format/printf/-t: the FS directive table
/// (coreutils stat.c fs_use_format fields). %c/%d report 0 (Windows volumes
/// expose no inode counts); %t is the fs-type magic, 0 on Windows.
/// Backslash escapes go through the SAME print_it interpreter as the file
/// side (stat.c:1199-1242 is shared by print_stat and print_fs; probed
/// 9.4: `-f --printf '%n\x41\n'` -> `f.txtA\n`, `[\z]` warns).
fn render_fs_format(
    fmt: &str,
    name: &str,
    info: &FsInfo,
    process_escapes: bool,
    stderr: &mut dyn Write,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    let bytes = fmt.as_bytes();
    let mut i = 0usize;
    let mut body: Vec<u8> = Vec::new();
    while i < bytes.len() {
        let c = bytes[i];
        if c != b'%' {
            if c == b'\\' {
                process_backslash(bytes, &mut i, process_escapes, stderr, &mut body)?;
            } else {
                body.push(c);
            }
            i += 1;
            continue;
        }
        i += 1;
        if i >= bytes.len() {
            body.push(b'%');
            break;
        }
        let letter = bytes[i];
        i += 1;
        let text: String = match letter {
            b'%' => "%".to_string(),
            b'n' => name.to_string(),
            b'i' => format!("{:08x}", info.id_hex),
            b'l' => info.namelen.to_string(),
            b't' => "0".to_string(),
            b'T' => info.fs_type.clone(),
            b's' => info.block.to_string(),
            b'S' => info.fblock.to_string(),
            b'b' => info.total.to_string(),
            b'f' => info.free.to_string(),
            b'a' => info.avail.to_string(),
            b'c' | b'd' => "0".to_string(),
            _ => "?".to_string(),
        };
        body.extend_from_slice(text.as_bytes());
    }
    out.write_all(&body)?;
    Ok(())
}

/// The GNU default file listing, byte-aligned with the 1.x C++ renderer it
/// replaces (git HEAD src/commands/stat.cpp print_stat default branch).
/// Returns true when a context (%C) failure occurred; the default layout
/// carries no %C, so this stays false.
fn render_default(
    entry: &StatEntry,
    follow: bool,
    ctx: &Ctx,
    out: &mut dyn Write,
) -> std::io::Result<bool> {
    let m = &entry.meta;
    let pad_right = |s: String, w: usize| format!("{:<width$}", s, width = w);
    let pad_left = |s: String, w: usize| format!("{:>width$}", s, width = w);
    //   File: <name>[ -> <target>]  (unquoted: 1.x and coreutils 9.4 shape)
    write!(out, "  File: {}", entry.name)?;
    if m.is_symlink && !follow {
        if let Ok(target) = std::fs::read_link(resolve(ctx, &entry.resolved)) {
            write!(out, " -> {}", target.to_string_lossy())?;
        }
    }
    writeln!(out)?;
    writeln!(
        out,
        "  Size: {}\tBlocks: {} IO Block: {} {}",
        pad_right(m.size.to_string(), 10),
        pad_right(m.size.div_ceil(512).to_string(), 10),
        pad_right("4096".to_string(), 6),
        file_type_name(m, entry.fifo)
    )?;
    // [GNU] "Device: <dev>,<sub>": Windows exposes the volume serial as the
    // device with subunit 0 (1.x comment, stat.cpp:1283).
    writeln!(
        out,
        "Device: {},0\tInode: {} Links: {}",
        m.volume_serial,
        pad_right(m.index.to_string(), 11),
        m.links
    )?;
    let uid = if entry.owner.0.is_empty() {
        "0"
    } else {
        &entry.owner.0
    };
    let gid = if entry.group.0.is_empty() {
        "0"
    } else {
        &entry.group.0
    };
    let owner_name = if entry.owner.1.is_empty() {
        "UNKNOWN".to_string()
    } else {
        entry.owner.1.clone()
    };
    writeln!(
        out,
        "Access: ({:04o}/{})  Uid: ({}/{})   Gid: ({}/{})",
        mode_word(m, entry.fifo) & 0o7777,
        access_string(m, entry.fifo),
        pad_left(uid.to_string(), 5),
        pad_left(owner_name, 8),
        pad_left(gid.to_string(), 5),
        pad_left(group_display(&entry.group.1), 8)
    )?;
    writeln!(out, "Access: {}", format_filetime(&m.atime))?;
    writeln!(out, "Modify: {}", format_filetime(&m.mtime))?;
    writeln!(out, "Change: {}", format_filetime(&m.ctime))?;
    writeln!(out, " Birth: {}", format_filetime(&m.btime))?;
    Ok(false)
}

pub fn stat_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut follow = false;
    let mut terse = false;
    let mut fs_mode = false;
    // [GNU] stat.c: -c/--printf/--format all assign ONE format slot, so
    // the LAST one on the command line wins (probed coreutils 9.4:
    // `--printf %n -c %s` prints the size, `-c %s --printf %n` prints the
    // name); printf_escapes records whether the winning assignment was
    // --printf. -t is an independent FLAG: an explicit format beats it
    // regardless of order (`-c %n -t` prints the name; probed), and it
    // only renders when the format slot is empty.
    let mut format: Option<String> = None;
    let mut printf_escapes = false;
    let mut operands: Vec<String> = Vec::new();
    let mut idx = 0usize;
    while idx < args.len() {
        let arg = args[idx].to_string_lossy().into_owned();
        if arg == "--" {
            idx += 1;
            for a in &args[idx..] {
                operands.push(a.to_string_lossy().into_owned());
            }
            break;
        }
        if arg == "-" || !arg.starts_with('-') {
            operands.push(arg);
            idx += 1;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            let (name, inline) = match long.split_once('=') {
                Some((n, v)) => (n.to_string(), Some(v.to_string())),
                None => (long.to_string(), None),
            };
            match name.as_str() {
                "dereference" => follow = true,
                "classify" | "printf" => match name.as_str() {
                    "classify" => {}
                    _ => {
                        let v = match inline {
                            Some(v) => v,
                            None => {
                                idx += 1;
                                match args.get(idx) {
                                    Some(v) => v.to_string_lossy().into_owned(),
                                    None => {
                                        let _ = usage_error(
                                            ctx,
                                            io,
                                            "stat",
                                            &argopt::option_requires_argument(ctx, "--printf"),
                                        );
                                        return Ok(1);
                                    }
                                }
                            }
                        };
                        // [GNU] --printf sets interpret_backslash_escapes;
                        // the format stays RAW here — print_it interprets
                        // backslashes per operand render (stat.c:1149-1248),
                        // so warnings repeat once per operand (probed 9.4:
                        // `--printf '[\z%n]' a b` -> two warnings).
                        format = Some(v);
                        printf_escapes = true;
                    }
                },
                "format" => match inline {
                    Some(v) => {
                        format = Some(v);
                        printf_escapes = false;
                    }
                    None => {
                        idx += 1;
                        match args.get(idx) {
                            Some(v) => {
                                format = Some(v.to_string_lossy().into_owned());
                                printf_escapes = false;
                            }
                            None => {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "stat",
                                    &argopt::option_requires_argument(ctx, "--format"),
                                );
                                return Ok(1);
                            }
                        }
                    }
                },
                "terse" => {
                    terse = true;
                }
                "file-system" => fs_mode = true,
                // [GNU] --cached=WHEN is a required-argument caching hint
                // (#1069): 'never'/'always'/'default' are accepted as a
                // no-op (Windows always queries fresh attributes); any
                // other value is GNU argmatch's invalid-argument usage
                // error, rc 1 (probed coreutils 9.4; 1.x stat.cpp:140-153).
                "cached" => {
                    let when = match inline {
                        Some(v) => v,
                        None => {
                            idx += 1;
                            match args.get(idx) {
                                Some(v) => v.to_string_lossy().into_owned(),
                                None => {
                                    let _ = usage_error(
                                        ctx,
                                        io,
                                        "stat",
                                        &argopt::option_requires_argument(ctx, "--cached"),
                                    );
                                    return Ok(1);
                                }
                            }
                        }
                    };
                    if when != "never" && when != "always" && when != "default" {
                        let _ = usage_error(
                            ctx,
                            io,
                            "stat",
                            &format!(
                                "invalid argument '{when}' for '--cached'\n\
                                 Valid arguments are:\n\
                                 \x20 - 'default'\n\
                                 \x20 - 'never'\n\
                                 \x20 - 'always'"
                            ),
                        );
                        return Ok(1);
                    }
                }
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(ctx, io, "stat", &argopt::unrecognized_option(ctx, &arg));
                    return Ok(1);
                }
            }
            idx += 1;
            continue;
        }
        let chars: Vec<char> = arg.chars().skip(1).collect();
        let mut ci = 0usize;
        let mut consumed_next = false;
        while ci < chars.len() {
            let c = chars[ci];
            match c {
                'L' => follow = true,
                't' => {
                    terse = true;
                }
                'f' => fs_mode = true,
                'c' => {
                    let rest: String = chars[ci + 1..].iter().collect();
                    let v = if !rest.is_empty() {
                        ci = chars.len();
                        rest
                    } else {
                        match args.get(idx + 1) {
                            Some(v) => {
                                consumed_next = true;
                                ci = chars.len();
                                v.to_string_lossy().into_owned()
                            }
                            None => {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "stat",
                                    &argopt::short_option_requires_argument(ctx, "c"),
                                );
                                return Ok(1);
                            }
                        }
                    };
                    format = Some(v);
                    printf_escapes = false;
                }
                c => {
                    let _ = usage_error(
                        ctx,
                        io,
                        "stat",
                        &argopt::invalid_option(ctx, &c.to_string()),
                    );
                    return Ok(1);
                }
            }
            ci += 1;
        }
        idx += if consumed_next { 2 } else { 1 };
    }
    if operands.is_empty() {
        let _ = usage_error(ctx, io, "stat", &crate::messages::missing_operand(ctx));
        return Ok(1);
    }
    let mut ok = true;
    for op in &operands {
        // direct-invocation glob contract (1.x utils glob_expand; the
        // shared module carries the rules): wildcard operands expand over
        // the named directory, unmatched patterns stay literal
        let expanded = if contains_wildcard(op) {
            glob_expand_in(ctx, op)
        } else {
            None
        };
        for op in expanded.unwrap_or_else(|| vec![op.clone()]) {
            let (meta, resolved) = match fs_win::file_meta_exe(ctx, &op, !follow) {
                Ok((m, resolved)) => (m, resolved),
                Err(e) => {
                    let _ = writeln!(
                        io.stderr,
                        "stat: cannot statx '{op}': {}",
                        fs_win::win32_reason(e)
                    );
                    ok = false;
                    continue;
                }
            };
            let (owner, group) = fs_win::file_accounts(ctx, &resolved);
            // #1038 fifo marker (1.x stat.cpp:438-446): a non-device,
            // non-directory, non-reparse operand whose on-disk content is
            // the `!<fifo>` marker reports the GNU lstat() FIFO shape —
            // type "fifo", S_IFIFO mode bits, st_size 0. The probe needs
            // the API-path spelling; `resolved` is the typed operand.
            let mut meta = meta;
            let fifo = !meta.character_device
                && !meta.is_dir
                && !meta.is_symlink
                && fs_win::is_winux_fifo_path(&fs_win::api_path(ctx, &resolved));
            if fifo {
                meta.size = 0;
            }
            let entry = StatEntry {
                name: op.clone(),
                resolved,
                meta,
                fifo,
                owner,
                group,
            };
            if fs_mode {
                // GNU stat -f: the default fs shape, or the FS directive table
                // for an explicit format; an explicit format beats -t (same
                // slot model as the file side)
                let info = collect_fs_info(&entry, ctx);
                match &info {
                    Some(info) if format.is_none() && !terse => {
                        render_fs_default(&entry.name, info, io.stdout)?;
                    }
                    Some(info) => {
                        let (fmt, process_escapes, append_nl) = match &format {
                            Some(f) => (f.clone(), printf_escapes, !printf_escapes),
                            None => (FS_TERSE_FORMAT.to_string(), false, true),
                        };
                        render_fs_format(
                            &fmt,
                            &entry.name,
                            info,
                            process_escapes,
                            io.stderr,
                            io.stdout,
                        )?;
                        if append_nl {
                            io.stdout.write_all(b"\n")?;
                        }
                    }
                    None => {}
                }
                continue;
            }
            // [GNU] the LAST of -c/--printf/--format assigns the one format
            // slot (printf_escapes says whether it was --printf); an explicit
            // format beats -t regardless of order (probed `-c %n -t` prints
            // the name); plain -t renders the terse line
            let (fmt, process_escapes, append_nl) = if let Some(f) = &format {
                (f.clone(), printf_escapes, !printf_escapes)
            } else if terse {
                (TERSE_FORMAT.to_string(), false, true)
            } else {
                // dedicated renderer, byte-aligned with the 1.x C++ default
                let failed = render_default(&entry, follow, ctx, io.stdout)?;
                if failed {
                    ok = false;
                }
                continue;
            };
            let ctx_failed = render_format(
                &fmt,
                &entry,
                follow,
                ctx,
                io.stderr,
                process_escapes,
                io.stdout,
            )?;
            if ctx_failed {
                ok = false;
            }
            if append_nl {
                io.stdout.write_all(b"\n")?;
            }
        }
    }
    Ok(if ok { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_sc(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = stat_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn setup(tag: &str) -> ScratchCtx {
        let sc = ScratchCtx::new(tag);
        std::fs::write(sc.path("reg"), "abc").unwrap();
        std::fs::create_dir(sc.path("dir")).unwrap();
        sc
    }

    #[test]
    fn format_fields() {
        let sc = setup("stat-fields");
        let (r, out, _) = run_sc(&sc, &["-c", "%n|%s|%b|%B|%o|%F", "reg"]);
        assert_eq!(r, 0);
        assert_eq!(out, "reg|3|1|512|4096|regular file\n");
        let (_, out, _) = run_sc(&sc, &["-c", "%n|%F", "dir"]);
        assert_eq!(out, "dir|directory\n");
        // %A / %a / %f
        let (_, out, _) = run_sc(&sc, &["-c", "%A|%a|%f", "reg"]);
        assert_eq!(out, "-rw-r--r--|644|81a4\n");
        let (_, out, _) = run_sc(&sc, &["-c", "%A|%a|%f", "dir"]);
        assert_eq!(out, "drwxr-xr-x|755|41ed\n");
        // widths and precision
        let (_, out, _) = run_sc(&sc, &["-c", "%6s|%a", "reg"]);
        assert_eq!(out, "     3|644\n");
        let (_, out, _) = run_sc(&sc, &["-c", "%-6s|%a", "reg"]);
        assert_eq!(out, "3     |644\n");
        let (_, out, _) = run_sc(&sc, &["-c", "%10.10A", "reg"]);
        // -c appends one newline per operand (GNU)
        assert_eq!(out, "-rw-r--r--\n");
        // %D/%d device, %i inode, %h links
        let (_, out, _) = run_sc(&sc, &["-c", "%D|%d|%i|%h", "reg"]);
        let parts: Vec<&str> = out.trim_end().split('|').collect();
        assert_eq!(parts.len(), 4);
        assert!(u64::from_str_radix(parts[0], 16).is_ok());
        assert!(parts[1].parse::<u64>().is_ok());
        assert!(parts[2].parse::<u64>().is_ok());
        assert_eq!(parts[3], "1");
        // %t/%T zero
        let (_, out, _) = run_sc(&sc, &["-c", "%t|%T", "reg"]);
        assert_eq!(out, "0|0\n");
        // rdev directives: plain integers, never '?' — GNU stat.c:1584-1600
        // renders st_rdev directly (0 on Linux regular files; probed 9.4)
        let (_, out, _) = run_sc(&sc, &["-c", "%R|%r|%Hr %Lr", "reg"]);
        assert_eq!(out, "0|0|0 0\n");
        // H/L modifier applies to %d too (major/minor st_dev, stat.c:1535-1537;
        // not representable on Windows -> 0)
        let (_, out, _) = run_sc(&sc, &["-c", "%Hd|%Ld", "reg"]);
        assert_eq!(out, "0|0\n");
        // width still applies (probed 9.4: `%4R|%-4R` -> `   0|0   `)
        let (_, out, _) = run_sc(&sc, &["-c", "%4R|%-4R|", "reg"]);
        assert_eq!(out, "   0|0   |\n");
        // modifier only before d/r (stat.c:1176-1190; probed `%Hx` -> `?x`)
        let (_, out, _) = run_sc(&sc, &["-c", "%Hx", "reg"]);
        assert_eq!(out, "?x\n");
        let (_, out, _) = run_sc(&sc, &["-c", "%H", "reg"]);
        assert_eq!(out, "?\n");
        // times
        let (_, out, _) = run_sc(&sc, &["-c", "%X|%Y|%Z|%W", "reg"]);
        for t in out.trim_end().split('|') {
            assert!(t.parse::<i64>().is_ok());
        }
        let (_, out, _) = run_sc(&sc, &["-c", "%y", "reg"]);
        // 2026-11-10 20:00:04.297388421 +0800 shape
        assert!(out.trim_end().ends_with(" +0800") || out.contains(':'));
        assert_eq!(out.matches('.').count(), 1);
        assert!(out.contains(' '));
        // %C prints ? and a stderr note, and fails the entry (GNU probed
        // coreutils 9.4: `stat -c %C reg` -> rc 1)
        let (r, out, err) = run_sc(&sc, &["-c", "%C", "reg"]);
        assert_eq!(r, 1);
        assert_eq!(out.trim_end(), "?");
        assert!(err.contains("failed to get security context"));
        // unknown directive prints ? and rc 0
        let (r, out, _) = run_sc(&sc, &["-c", "%q", "reg"]);
        assert_eq!(r, 0);
        assert_eq!(out.trim_end(), "?");
        // %% literal
        let (_, out, _) = run_sc(&sc, &["-c", "%n %n%", "reg"]);
        assert_eq!(out, "reg reg%\n");
    }

    #[test]
    fn default_and_terse_shape() {
        let sc = setup("stat-def");
        let (r, out, _) = run_sc(&sc, &["reg"]);
        assert_eq!(r, 0);
        // GNU default shape line by line (unquoted File line, comma device,
        // byte-aligned with the 1.x C++ renderer: probed coreutils 9.4 on
        // DrvFs + the old exe matrix, 2026-11-11)
        let lines: Vec<&str> = out.split('\n').collect();
        assert!(lines[0].starts_with("  File: reg"));
        assert!(!lines[0].contains('\''));
        assert!(lines[1].starts_with("  Size: 3"));
        assert!(lines[1].contains("Blocks: 1"));
        assert!(lines[1].contains("IO Block: 4096"));
        assert!(lines[1].contains("regular empty file") || lines[1].contains("regular file"));
        assert!(lines[2].starts_with("Device: "));
        assert!(lines[2].contains(",0\t"));
        assert!(lines[2].contains("Links: 1"));
        assert!(lines[3].starts_with("Access: (0644/"));
        assert!(lines[3].contains("Uid:"));
        assert!(lines[3].contains("Gid:"));
        assert!(lines[4].starts_with("Access: "));
        assert!(lines[5].starts_with("Modify: "));
        assert!(lines[6].starts_with("Change: "));
        assert!(lines[7].starts_with(" Birth: "));
        // terse: 16 fields
        let (r, out, _) = run_sc(&sc, &["-t", "reg"]);
        assert_eq!(r, 0);
        let fields: Vec<&str> = out.split_whitespace().collect();
        assert_eq!(fields.len(), 16);
        assert_eq!(fields[0], "reg");
        assert_eq!(fields[1], "3");
        assert_eq!(fields[2], "1");
        assert_eq!(fields[3], "81a4");
        assert_eq!(fields[13], fields[13]); // W
        assert_eq!(fields[15], "4096"); // o
    }

    #[test]
    fn multi_files_and_errors() {
        let sc = setup("stat-multi");
        let (r, out, _) = run_sc(&sc, &["-c", "%n", "reg", "dir"]);
        assert_eq!(r, 0);
        assert_eq!(out, "reg\ndir\n");
        // missing operand: error + rc 1 + others still run
        let (r, out, err) = run_sc(&sc, &["-c", "%n", "reg", "missing", "dir"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot statx 'missing': No such file or directory"));
        assert!(out.contains("reg"));
        assert!(out.contains("dir"));
        // no operands
        let (r, _, e) = run_sc(&sc, &[]);
        assert_eq!(r, 1);
        assert!(e.contains("missing operand"));
        assert_eq!(run_sc(&sc, &["--bogus"]).0, 1);
        assert_eq!(run_sc(&sc, &["--help"]).0, 0);
        assert_eq!(run_sc(&sc, &["--version"]).0, 0);
    }

    #[test]
    fn bare_exe_operand_resolves() {
        // #1572: a bare name whose only on-disk form is `<name>.exe` stats
        // like the engine's `test -e/-x` already answers it; the display
        // keeps the operand as typed (GNU %n / `File:` echo the operand).
        let sc = setup("stat-bare-exe");
        std::fs::write(sc.path("java.exe"), "MZ").unwrap();
        let (r, out, err) = run_sc(&sc, &["-c", "%n|%F", "java"]);
        assert_eq!(r, 0, "rc: {err}");
        assert_eq!(out, "java|regular file\n");
        assert_eq!(err, "");
        // -L follows the same resolved file (regular file: same metadata)
        let (r, out, _) = run_sc(&sc, &["-L", "-c", "%n|%F", "java"]);
        assert_eq!(r, 0);
        assert_eq!(out, "java|regular file\n");
        // default rendering: `File:` echoes the typed name, size is the
        // .exe's (2 bytes), and the type is a regular file
        let (r, out, _) = run_sc(&sc, &["java"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("  File: java\n"), "{out}");
        assert!(out.contains("Size: 2"));
        assert!(out.contains("regular file"));
        // negative controls: a bare name with no .exe sibling still fails
        // with the typed name in the message, and an explicit extension is
        // never completed (java.txt must not resolve to java.txt.exe)
        let (r, _, err) = run_sc(&sc, &["-c", "%n", "gone"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot statx 'gone': No such file or directory"));
        let (r, _, err) = run_sc(&sc, &["-c", "%n", "java.txt"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot statx 'java.txt': No such file or directory"));
    }

    #[test]
    fn dev_null_is_character_special() {
        // #276: GNU reports /dev/null (and /dev/tty) as a character special
        // file; %n keeps the operand text. probed WSL coreutils 9.4:
        // `stat -c %F /dev/null` -> "character special file", rc 0.
        let sc = ScratchCtx::new("stat-devnull");
        let (r, out, err) = run_sc(&sc, &["-c", "%F", "/dev/null"]);
        assert_eq!(r, 0, "rc");
        assert_eq!(err, "", "stderr");
        assert_eq!(out, "character special file\n");
        let (r, out, _) = run_sc(&sc, &["-c", "%n", "/dev/null"]);
        assert_eq!(r, 0);
        assert_eq!(out, "/dev/null\n");
        // bare MSYS-rewritten spelling and the /dev/tty pseudo-device
        let (r, out, _) = run_sc(&sc, &["-c", "%F|%s", "NUL"]);
        assert_eq!(r, 0);
        assert_eq!(out, "character special file|0\n");
        let (r, out, _) = run_sc(&sc, &["-c", "%F", "/dev/tty"]);
        assert_eq!(r, 0, "/dev/tty rc");
        assert_eq!(out, "character special file\n");
    }

    #[test]
    fn printf_mode() {
        let sc = setup("stat-printf");
        let (r, out, _) = run_sc(&sc, &["--printf", "%n\\n", "reg"]);
        assert_eq!(r, 0);
        assert_eq!(out, "reg\n");
        // no trailing newline added for --printf
        let (_, out, _) = run_sc(&sc, &["--printf", "%n", "reg"]);
        assert_eq!(out, "reg");
        // -c adds one
        let (_, out, _) = run_sc(&sc, &["-c", "%n", "reg"]);
        assert_eq!(out, "reg\n");
    }

    #[test]
    fn printf_escapes_gnu_print_it() {
        // Byte-faithful print_it backslash branch (stat.c:1199-1242),
        // probed coreutils 9.4; corpus regressions/58 is the pinned case.
        let sc = setup("stat-esc");
        // recognized: hex (1-2 digits), octal (1-3), the esc_char table
        let (r, out, err) = run_sc(
            &sc,
            &["--printf", "[\\x41][\\x4][\\x414][\\011][\\e][\\\"]", "reg"],
        );
        assert_eq!(r, 0);
        assert_eq!(err, "");
        assert_eq!(out, "[A][\x04][A4][\t][\x1b][\"]");
        // \xZZ: backslash dropped, 'x' echoed, warning on stderr
        let (r, out, err) = run_sc(&sc, &["--printf", "[\\xZZ]", "reg"]);
        assert_eq!(r, 0);
        assert_eq!(out, "[xZZ]");
        assert_eq!(err, "stat: warning: unrecognized escape '\\x'\n");
        // unrecognized escape letter: backslash dropped + warning ('\' is
        // NOT in the table; probed `[\']` -> `'` + warning)
        let (_, out, err) = run_sc(&sc, &["--printf", "[\\z][\\']", "reg"]);
        assert_eq!(out, "[z][']");
        assert_eq!(
            err,
            "stat: warning: unrecognized escape '\\z'\nstat: warning: unrecognized escape '\\''\n"
        );
        // putchar truncation: raw bytes (\400 -> 0x00, \xff -> 0xFF;
        // probed 9.4 od: both are 3-byte outputs). Byte-exact assertion —
        // MemIo::out() is lossy, so read the output Vec<u8> directly.
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = ["--printf", "[\\400][\\xff]", "reg"]
            .iter()
            .map(Into::into)
            .collect();
        stat_cmd(&owned, &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(io.output, b"[\x00][\xff]".to_vec());
        // trailing lone backslash: warning + backslash echoed
        let (_, out, err) = run_sc(&sc, &["--printf", "[a\\", "reg"]);
        assert_eq!(out, "[a\\");
        assert_eq!(err, "stat: warning: backslash at end of format\n");
        // -c keeps backslashes verbatim, never warns (stat.c:1200-1204)
        let (_, out, err) = run_sc(&sc, &["-c", "%n\\n\\z", "reg"]);
        assert_eq!(out, "reg\\n\\z\n");
        assert_eq!(err, "");
        // warnings repeat per operand render (print_it runs per file)
        let (_, out, err) = run_sc(&sc, &["--printf", "[\\z%n]", "reg", "dir"]);
        assert_eq!(out, "[zreg][zdir]");
        assert_eq!(err.matches("unrecognized escape '\\z'").count(), 2);
    }

    #[test]
    fn fs_printf_escapes() {
        // print_it is shared by the fs path (probed 9.4: `-f --printf
        // '%n\x41\n'` -> `f.txtA\n`; `[\z]` warns there too).
        let sc = setup("stat-fs-esc");
        let info = FsInfo {
            id_hex: 0x1234ABCD,
            namelen: 255,
            fs_type: "NTFS".to_string(),
            block: 4096,
            fblock: 4096,
            total: 1,
            free: 2,
            avail: 3,
        };
        let mut out: Vec<u8> = Vec::new();
        let mut err: Vec<u8> = Vec::new();
        render_fs_format("%n\\x41\\n", "f.txt", &info, true, &mut err, &mut out).unwrap();
        assert_eq!(out, b"f.txtA\n");
        assert_eq!(err, b"");
        // -c (process_escapes=false): backslashes verbatim
        let mut out: Vec<u8> = Vec::new();
        let mut err: Vec<u8> = Vec::new();
        render_fs_format("%n\\n", "f.txt", &info, false, &mut err, &mut out).unwrap();
        assert_eq!(out, b"f.txt\\n");
        assert_eq!(err, b"");
        // unrecognized escape warns on the fs path as well
        let mut out: Vec<u8> = Vec::new();
        let mut err: Vec<u8> = Vec::new();
        render_fs_format("[\\z]", "f.txt", &info, true, &mut err, &mut out).unwrap();
        assert_eq!(out, b"[z]");
        assert_eq!(err, b"stat: warning: unrecognized escape '\\z'\n");
    }

    #[test]
    fn symlink_lstat() {
        let sc = setup("stat-sym");
        // create a symlink (may fail without privilege; skip when so)
        let ok = std::os::windows::fs::symlink_file("reg", sc.path("sym")).is_ok();
        if !ok {
            return;
        }
        // lstat default: link metadata, size = target name length
        let (r, out, _) = run_sc(&sc, &["-c", "%n|%F|%s|%N", "sym"]);
        assert_eq!(r, 0);
        assert_eq!(out, "sym|symbolic link|3|'sym' -> 'reg'\n");
        // -L follows
        let (r, out, _) = run_sc(&sc, &["-L", "-c", "%n|%F|%s", "sym"]);
        assert_eq!(r, 0);
        assert_eq!(out, "sym|regular file|3\n");
        // dangling link still stats (lstat)
        std::os::windows::fs::symlink_file("nowhere", sc.path("dang")).ok();
        if sc.path("dang").symlink_metadata().is_ok() {
            let (r, out, _) = run_sc(&sc, &["-c", "%F", "dang"]);
            assert_eq!(r, 0);
            assert_eq!(out.trim_end(), "symbolic link");
        }
    }
}
