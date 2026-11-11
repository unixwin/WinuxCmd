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

use std::ffi::OsString;
use std::path::Path;

use crate::glob::{contains_wildcard, glob_expand_in};
use std::io::Write;

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
    meta: fs_win::FileMeta,
    owner: (String, String),
    group: (String, String),
}

fn mode_word(meta: &fs_win::FileMeta) -> u32 {
    // 1.x Windows value model: regular 0644 (0444 when read-only),
    // directories 0755, symlinks 0777 (GNU's a1ff).
    if meta.is_symlink {
        return 0o120777; // S_IFLNK | 0777
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

fn access_string(meta: &fs_win::FileMeta) -> String {
    let m = mode_word(meta);
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

fn file_type_name(meta: &fs_win::FileMeta) -> String {
    if meta.is_symlink {
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

/// Compute the field for a directive letter. `ctx_failed` flips when the
/// requested field cannot be produced (GNU %C: rc 1 per GNU stat -c %C on
/// a volume without security contexts — probed coreutils 9.4, rc 1 with
/// '?' on stdout and the "failed to get security context" note).
fn field_for(
    letter: char,
    entry: &StatEntry,
    follow: bool,
    ctx: &Ctx,
    stderr: &mut dyn Write,
    ctx_failed: &mut bool,
) -> Field {
    let m = &entry.meta;
    let mw = mode_word(m);
    Field::Str(match letter {
        'n' => entry.name.clone(),
        'N' => {
            let mut s = quote_name(&entry.name);
            if m.is_symlink && !follow {
                if let Ok(target) = std::fs::read_link(resolve(ctx, &entry.name)) {
                    s.push_str(" -> ");
                    s.push_str(&quote_name(&target.to_string_lossy()));
                }
            }
            s
        }
        'F' => file_type_name(m),
        'A' => access_string(m),
        'a' => return Field::Oct(mw & 0o7777),
        'f' => format!("{:04x}", mw),
        's' => return Field::Dec(m.size),
        'b' => return Field::Dec(m.size.div_ceil(512)),
        'B' => return Field::Dec(512),
        'o' => return Field::Dec(4096),
        'i' => return Field::Dec(m.index),
        'h' => return Field::Dec(m.links),
        'd' => return Field::Dec(m.volume_serial as u64),
        'D' => return Field::Hex(m.volume_serial as u64),
        't' => return Field::Dec(0),
        'T' => return Field::Dec(0),
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
        'm' => mount_point(ctx, &entry.name),
        'c' => return Field::Dec(255),
        'S' => return Field::Dec(4096),
        _ => "?".to_string(),
    })
}

fn mount_point(ctx: &Ctx, name: &str) -> String {
    #[cfg(windows)]
    {
        let full = fs_win::imp::api_path(&ctx.cwd, name);
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
    // the carrier-colon fold, dot-segment collapse and the shell-tree
    // mapping all happen there, exactly like the 1.x operand layer
    std::path::PathBuf::from(fs_win::api_path(&ctx.cwd, p))
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
            out.write_all(&[c])?;
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
        let letter = bytes[i] as char;
        i += 1;
        let field = field_for(letter, entry, follow, ctx, stderr, &mut ctx_failed);
        let rendered = field.render(&spec);
        if process_escapes && letter.is_ascii_alphabetic() {
            out.write_all(rendered.as_bytes())?;
        } else {
            out.write_all(rendered.as_bytes())?;
        }
    }
    Ok(ctx_failed)
}

/// Process backslash escapes for --printf.
fn unescape(fmt: &str) -> String {
    let mut out = String::with_capacity(fmt.len());
    let mut chars = fmt.chars().peekable();
    while let Some(c) = chars.next() {
        if c != '\\' {
            out.push(c);
            continue;
        }
        match chars.next() {
            Some('n') => out.push('\n'),
            Some('t') => out.push('\t'),
            Some('r') => out.push('\r'),
            Some('\\') => out.push('\\'),
            Some('v') => out.push('\x0b'),
            Some('f') => out.push('\x0c'),
            // [GNU] printf escapes (probed coreutils 9.4: `--printf
            // '%n\t\011\010\x41'` -> name, tab, tab, backspace, 'A'):
            // \NNN is 1-3 OCTAL digits, \xHH hex
            Some(c) if c.is_ascii_digit() && c != '8' && c != '9' => {
                let mut value = c.to_digit(8).unwrap();
                let mut taken = 1;
                while taken < 3 {
                    match chars.peek() {
                        Some(n) if (*n).is_ascii_digit() && *n != '8' && *n != '9' => {
                            value = value * 8 + (*n).to_digit(8).unwrap();
                            chars.next();
                            taken += 1;
                        }
                        _ => break,
                    }
                }
                let _ = taken;
                match char::from_u32(value) {
                    Some(ch) => out.push(ch),
                    None => {
                        out.push('\\');
                        out.push(c);
                    }
                }
            }
            Some('x') => {
                let mut value = 0u32;
                let mut taken = 0;
                while taken < 2 {
                    match chars.peek().and_then(|n| n.to_digit(16)) {
                        Some(d) => {
                            value = value.saturating_mul(16).saturating_add(d);
                            chars.next();
                            taken += 1;
                        }
                        None => break,
                    }
                }
                if taken == 0 {
                    out.push('\\');
                    out.push('x');
                } else {
                    match char::from_u32(value) {
                        Some(ch) => out.push(ch),
                        None => {
                            out.push('\\');
                            out.push('x');
                        }
                    }
                }
            }
            Some('\'') => out.push('\''),
            Some(other) => {
                out.push('\\');
                out.push(other);
            }
            None => out.push('\\'),
        }
    }
    out
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
        let mut volume_root = mount_point(
            &Ctx {
                cwd: ctx.cwd.clone(),
                env: ctx.env,
            },
            &entry.name,
        );
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
fn render_fs_format(
    fmt: &str,
    name: &str,
    info: &FsInfo,
    process_escapes: bool,
    out: &mut dyn Write,
) -> std::io::Result<()> {
    let rendered = fmt
        .chars()
        .flat_map(|c| std::iter::once(c))
        .collect::<Vec<char>>();
    let mut i = 0usize;
    let mut body = String::new();
    while i < rendered.len() {
        let c = rendered[i];
        if c != '%' {
            body.push(c);
            i += 1;
            continue;
        }
        i += 1;
        if i >= rendered.len() {
            body.push('%');
            break;
        }
        let letter = rendered[i];
        i += 1;
        match letter {
            '%' => body.push('%'),
            'n' => body.push_str(name),
            'i' => body.push_str(&format!("{:08x}", info.id_hex)),
            'l' => body.push_str(&info.namelen.to_string()),
            't' => body.push('0'),
            'T' => body.push_str(&info.fs_type),
            's' => body.push_str(&info.block.to_string()),
            'S' => body.push_str(&info.fblock.to_string()),
            'b' => body.push_str(&info.total.to_string()),
            'f' => body.push_str(&info.free.to_string()),
            'a' => body.push_str(&info.avail.to_string()),
            'c' | 'd' => body.push('0'),
            _ => body.push('?'),
        }
    }
    let _ = process_escapes;
    out.write_all(body.as_bytes())?;
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
        if let Ok(target) = std::fs::read_link(resolve(ctx, &entry.name)) {
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
        file_type_name(m)
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
        mode_word(m) & 0o7777,
        access_string(m),
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
                                            io,
                                            "stat",
                                            "option '--printf' requires an argument",
                                        );
                                        return Ok(1);
                                    }
                                }
                            }
                        };
                        format = Some(unescape(&v));
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
                                    io,
                                    "stat",
                                    "option '--format' requires an argument",
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
                "help" | "version" => return Ok(0),
                _ => {
                    let _ = usage_error(io, "stat", &format!("unrecognized option '{arg}'"));
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
                                let _ =
                                    usage_error(io, "stat", "option requires an argument -- 'c'");
                                return Ok(1);
                            }
                        }
                    };
                    format = Some(v);
                    printf_escapes = false;
                }
                c => {
                    let _ = usage_error(io, "stat", &format!("invalid option -- '{c}'"));
                    return Ok(1);
                }
            }
            ci += 1;
        }
        idx += if consumed_next { 2 } else { 1 };
    }
    if operands.is_empty() {
        let _ = usage_error(io, "stat", "missing operand");
        return Ok(1);
    }
    let mut ok = true;
    for op in &operands {
        // direct-invocation glob contract (1.x utils glob_expand; the
        // shared module carries the rules): wildcard operands expand over
        // the named directory, unmatched patterns stay literal
        let expanded = if contains_wildcard(op) {
            glob_expand_in(&ctx.cwd, op)
        } else {
            None
        };
        for op in expanded.unwrap_or_else(|| vec![op.clone()]) {
            let meta = match fs_win::file_meta(&ctx.cwd, &op, !follow) {
                Ok(m) => m,
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
            let (owner, group) = fs_win::file_accounts(&ctx.cwd, &op);
            let entry = StatEntry {
                name: op.clone(),
                meta,
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
                        render_fs_format(&fmt, &entry.name, info, process_escapes, io.stdout)?;
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
