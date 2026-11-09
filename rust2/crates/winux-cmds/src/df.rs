//! `df` — GNU coreutils column shape, Windows volume subset.
//!
//! Pilot subset (documented deviation, per #1488 wave1 scope): `df` lists
//! ready local volumes (GetLogicalDrives + GetDiskFreeSpaceExW via raw
//! kernel32 FFI, zero crates) or the volume containing each given path.
//! `-h` (1024-based human), `-H` (SI), `-P` (POSIX header/capacity column),
//! `-a` accepted, `--output`/`-x`/`-t` NOT implemented (usage error rc 1).
//! Output content is platform-inherent: the gate compares rc strictly and
//! records stdout/stderr (GNU compares error paths byte-wise).

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
        short: Some('P'),
        long: "portability",
        takes_value: false,
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
];

#[link(name = "kernel32")]
extern "system" {
    fn GetLogicalDrives() -> u32;
    fn GetDiskFreeSpaceExW(
        lp_directory_name: *const u16,
        lp_free_bytes_available_to_caller: *mut u64,
        lp_total_number_of_bytes: *mut u64,
        lp_total_number_of_free_bytes: *mut u64,
    ) -> i32;
}

struct Volume {
    name: String, // e.g. "C:\"
    total: u64,
    free: u64,      // total free
    available: u64, // free to caller
}

fn volume_stats(name: &str) -> Option<Volume> {
    let wide: Vec<u16> = name.encode_utf16().chain(std::iter::once(0)).collect();
    let mut avail: u64 = 0;
    let mut total: u64 = 0;
    let mut free: u64 = 0;
    let ok = unsafe { GetDiskFreeSpaceExW(wide.as_ptr(), &mut avail, &mut total, &mut free) };
    if ok == 0 {
        None
    } else {
        Some(Volume {
            name: name.to_string(),
            total,
            free,
            available: avail,
        })
    }
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

fn human(bytes: u64, si: bool) -> String {
    let base: f64 = if si { 1000.0 } else { 1024.0 };
    let units = if si {
        ["B", "kB", "MB", "GB", "TB", "PB"]
    } else {
        ["B", "K", "M", "G", "T", "P"]
    };
    let mut v = bytes as f64;
    let mut u = 0usize;
    while v >= base && u + 1 < units.len() {
        v /= base;
        u += 1;
    }
    if u == 0 {
        format!("{bytes}B")
    } else {
        format!("{v:.1}{}", units[u])
    }
}

fn pct(used: u64, avail: u64) -> u64 {
    let total_used = used + avail;
    if total_used == 0 {
        return 0;
    }
    // GNU rounds up
    ((used * 100 + total_used - 1) / total_used).min(100)
}

pub fn df_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("df", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if p.has("output") {
        // documented subset: --output is not implemented in the pilot
        let _ = usage_error(
            io,
            "df",
            "option '--output' is not supported in the 2.0 pilot subset",
        );
        return Ok(1);
    }
    let human_mode = p.has("human-readable");
    let si = p.has("si");

    let mut failed = false;
    let mut rows: Vec<(String, u64, u64, u64, u64, String)> = Vec::new();
    if p.positionals.is_empty() {
        for v in local_volumes() {
            rows.push(row_of(&v, &v.name.clone()));
        }
    } else {
        // canonical cwd once; relative operands resolve against it lexically
        // (canonicalize of literal "." / ".." suffixes can fail on Windows)
        let canon_cwd = winux_core::strip_verbatim(
            std::fs::canonicalize(&ctx.cwd).unwrap_or_else(|_| ctx.cwd.clone()),
        );
        for operand in &p.positionals {
            let name = operand.to_string_lossy().into_owned();
            let path = Path::new(&name);
            let path_abs = if path.is_absolute() {
                path.to_path_buf()
            } else {
                let mut p2 = canon_cwd.clone();
                for comp in crate::fsutil::split_slashes(&name) {
                    match comp {
                        "." => {}
                        ".." => {
                            p2.pop();
                        }
                        c => p2.push(c),
                    }
                }
                p2
            };
            let mount = if let Ok(c) = std::fs::canonicalize(&path_abs) {
                winux_core::strip_verbatim(c).to_string_lossy().into_owned()
            } else {
                let _ = writeln!(io.stderr, "df: {}: No such file or directory", name);
                failed = true;
                continue;
            };
            let drive = extract_drive(&mount);
            match drive.and_then(|d| volume_stats(&d)) {
                Some(v) => rows.push(row_of(&v, &name)),
                None => {
                    let _ = writeln!(io.stderr, "df: {}: No such file or directory", name);
                    failed = true;
                }
            }
        }
    }

    // GNU prints nothing at all when every operand failed
    if rows.is_empty() {
        return Ok(if failed { 1 } else { 0 });
    }

    let out = &mut *io.stdout;
    if p.has("portability") {
        writeln!(
            out,
            "{:<12} {:>11} {:>10} {:>10} {:>8} {}",
            "Filesystem", "1024-blocks", "Used", "Available", "Capacity", "Mounted on"
        )?;
    } else if human_mode || si {
        writeln!(
            out,
            "{:<12} {:>4} {:>5} {:>5} {:>4} {}",
            "Filesystem", "Size", "Used", "Avail", "Use%", "Mounted on"
        )?;
    } else {
        writeln!(
            out,
            "{:<12} {:>10} {:>10} {:>10} {:>4} {}",
            "Filesystem", "1K-blocks", "Used", "Available", "Use%", "Mounted on"
        )?;
    }
    for (fs, blocks, used, avail, usepct, mount) in &rows {
        if human_mode || si {
            writeln!(
                out,
                "{:<12} {:>5} {:>5} {:>5} {:>4}% {}",
                fs,
                human(*blocks * 1024, si),
                human(*used * 1024, si),
                human(*avail * 1024, si),
                usepct,
                mount
            )?;
        } else {
            writeln!(
                out,
                "{:<12} {:>10} {:>10} {:>10} {:>3}% {}",
                fs, blocks, used, avail, usepct, mount
            )?;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

fn row_of(v: &Volume, mount: &str) -> (String, u64, u64, u64, u64, String) {
    let kb = v.total / 1024;
    let used_kb = (v.total - v.free) / 1024;
    let avail_kb = v.available / 1024;
    let pct = pct(used_kb, avail_kb);
    (
        v.name.clone(),
        kb,
        used_kb,
        avail_kb,
        pct,
        mount.to_string(),
    )
}

fn extract_drive(abs_path: &str) -> Option<String> {
    let bytes = abs_path.as_bytes();
    if bytes.len() >= 2 && bytes[1] == b':' && bytes[0].is_ascii_alphabetic() {
        Some(format!("{}:\\", bytes[0] as char))
    } else {
        None
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn listing_and_errors() {
        let sc = ScratchCtx::new("df");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let r = df_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().contains("Filesystem"));
        assert!(io.out().lines().count() >= 2);

        let mut io = MemIo::new("");
        let args = [OsString::from("Z:\\definitely-missing-path")];
        let r = df_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("No such file or directory"));
    }

    #[test]
    fn human_columns() {
        let sc = ScratchCtx::new("df2");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let args = ["-h".into()];
        let r = df_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().contains("Size"));
    }

    #[test]
    fn posix_header() {
        let sc = ScratchCtx::new("df3");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let args = ["-P".into()];
        let r = df_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().contains("1024-blocks"));
    }

    #[test]
    fn output_not_implemented_is_rc1() {
        let sc = ScratchCtx::new("df4");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let args = ["--output=source".into()];
        let r = df_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
    }
}
