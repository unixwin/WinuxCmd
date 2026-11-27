//! `free` — procps-ng free semantics on Windows, ported for WinuxCmd 2.0 (#1488).
//!
//! Uses `GlobalMemoryStatusEx` and `GetPerformanceInfo` on Windows (with
//! `/proc/meminfo` on non-Windows hosts), explicit `Io`/`Ctx`, and zero globals.

use std::ffi::OsString;
use std::io::Write;
use std::thread;
use std::time::Duration;

use crate::argopt;
use crate::gnuhelp;
use crate::messages;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const FREE_HELP: &str = "\
Usage:
 free [options]

Options:
 -b, --bytes         show output in bytes
     --kilo          show output in kilobytes
     --mega          show output in megabytes
     --giga          show output in gigabytes
     --tera          show output in terabytes
     --peta          show output in petabytes
 -k, --kibi          show output in kibibytes
 -m, --mebi          show output in mebibytes
 -g, --gibi          show output in gibibytes
     --tebi          show output in tebibytes
     --pebi          show output in pebibytes
 -h, --human         show human-readable output
     --si            use powers of 1000 not 1024
 -l, --lohi          show detailed low and high memory statistics
 -t, --total         show total for RAM + swap
 -v, --committed     show committed memory and commit limit
 -s N, --seconds N   repeat printing every N seconds
 -c N, --count N     repeat printing N times, then exit
 -w, --wide          wide output
 -L, --line          show output on a single line
 -V, --version       output version information and exit
     --help          display this help and exit
";

#[derive(Default)]
struct Config {
    exponent: u32,
    si: bool,
    human: bool,
    lohi: bool,
    total: bool,
    wide: bool,
    line: bool,
    committed: bool,
    interval: i64,
    count: i64,
}

#[derive(Default, Clone, Copy)]
struct MemSnapshot {
    total: u64,        // KiB
    used: u64,         // KiB
    free: u64,         // KiB
    available: u64,    // KiB
    swap_total: u64,   // KiB
    swap_used: u64,    // KiB
    swap_free: u64,    // KiB
    commit_limit: u64, // KiB
    committed: u64,    // KiB
}

#[cfg(windows)]
fn take_snapshot() -> MemSnapshot {
    #[repr(C)]
    struct MemoryStatusEx {
        dw_length: u32,
        dw_memory_load: u32,
        ull_total_phys: u64,
        ull_avail_phys: u64,
        ull_total_page_file: u64,
        ull_avail_page_file: u64,
        ull_total_virtual: u64,
        ull_avail_virtual: u64,
        ull_avail_extended_virtual: u64,
    }

    #[repr(C)]
    struct PerformanceInformation {
        cb: u32,
        commit_total: usize,
        commit_limit: usize,
        commit_peak: usize,
        physical_total: usize,
        physical_available: usize,
        system_cache: usize,
        kernel_total: usize,
        kernel_paged: usize,
        kernel_nonpaged: usize,
        page_size: usize,
        handle_count: u32,
        process_count: u32,
        thread_count: u32,
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn GlobalMemoryStatusEx(lpBuffer: *mut MemoryStatusEx) -> i32;
        fn K32GetPerformanceInfo(
            pPerformanceInformation: *mut PerformanceInformation,
            cb: u32,
        ) -> i32;
    }

    let mut snap = MemSnapshot::default();
    let mut mem = MemoryStatusEx {
        dw_length: std::mem::size_of::<MemoryStatusEx>() as u32,
        dw_memory_load: 0,
        ull_total_phys: 0,
        ull_avail_phys: 0,
        ull_total_page_file: 0,
        ull_avail_page_file: 0,
        ull_total_virtual: 0,
        ull_avail_virtual: 0,
        ull_avail_extended_virtual: 0,
    };
    if unsafe { GlobalMemoryStatusEx(&mut mem) } == 0 {
        return snap;
    }

    snap.total = mem.ull_total_phys / 1024;
    snap.available = mem.ull_avail_phys / 1024;
    snap.free = snap.available;
    snap.used = snap.total.saturating_sub(snap.available);
    snap.swap_total = mem.ull_total_page_file / 1024;
    snap.swap_free = mem.ull_avail_page_file / 1024;
    snap.swap_used = snap.swap_total.saturating_sub(snap.swap_free);

    let mut perf = unsafe { std::mem::zeroed::<PerformanceInformation>() };
    perf.cb = std::mem::size_of::<PerformanceInformation>() as u32;
    if unsafe { K32GetPerformanceInfo(&mut perf, perf.cb) } != 0 {
        snap.commit_limit = (perf.commit_limit as u64).saturating_mul(perf.page_size as u64) / 1024;
        snap.committed = (perf.commit_total as u64).saturating_mul(perf.page_size as u64) / 1024;
    }
    snap
}

#[cfg(not(windows))]
fn take_snapshot() -> MemSnapshot {
    let mut snap = MemSnapshot::default();
    if let Ok(text) = std::fs::read_to_string("/proc/meminfo") {
        let mut mem_free = 0u64;
        let mut mem_avail = None;
        for line in text.lines() {
            let mut parts = line.split_whitespace();
            let key = parts.next().unwrap_or("");
            let val = parts.next().and_then(|v| v.parse::<u64>().ok()).unwrap_or(0);
            match key {
                "MemTotal:" => snap.total = val,
                "MemFree:" => mem_free = val,
                "MemAvailable:" => mem_avail = Some(val),
                "SwapTotal:" => snap.swap_total = val,
                "SwapFree:" => snap.swap_free = val,
                "CommitLimit:" => snap.commit_limit = val,
                "Committed_AS:" => snap.committed = val,
                _ => {}
            }
        }
        snap.free = mem_free;
        snap.available = mem_avail.unwrap_or(mem_free);
        snap.used = snap.total.saturating_sub(snap.available);
        snap.swap_used = snap.swap_total.saturating_sub(snap.swap_free);
    }
    snap
}

fn scale_size(kib: u64, cfg: &Config) -> String {
    let base: f64 = if cfg.si { 1000.0 } else { 1024.0 };
    let bytes: f64 = (kib as f64) * 1024.0;

    if !cfg.human {
        let value: u64 = if cfg.exponent == 1 {
            bytes as u64
        } else if cfg.exponent == 0 {
            (bytes / base) as u64
        } else {
            let mut scaled = bytes;
            for _ in 1..cfg.exponent {
                scaled /= base;
            }
            scaled as u64
        };
        return value.to_string();
    }

    const SUFFIXES: [char; 6] = ['B', 'K', 'M', 'G', 'T', 'P'];
    if bytes < 10000.0 {
        let s = format!("{}B", bytes as u64);
        if s.len() <= 4 {
            return s;
        }
    }
    let mut scaled = bytes;
    let mut last = String::new();
    for &suffix in &SUFFIXES[1..] {
        scaled /= base;
        if cfg.si {
            let s1 = format!("{scaled:.1}{suffix}");
            if s1.len() <= 4 {
                return s1;
            }
            let s2 = format!("{}{suffix}", scaled as u64);
            if s2.len() <= 4 {
                return s2;
            }
            last = s2;
        } else {
            let s1 = format!("{scaled:.1}{suffix}i");
            if s1.len() <= 5 {
                return s1;
            }
            let s2 = format!("{}{suffix}i", scaled as u64);
            if s2.len() <= 5 {
                return s2;
            }
            last = s2;
        }
    }
    last
}

fn pad_label(label: &str) -> String {
    if label.len() < 9 {
        format!("{label}{}", " ".repeat(9 - label.len()))
    } else {
        label.to_owned()
    }
}

fn pad_value(value: &str) -> String {
    if value.len() < 11 {
        format!("{}{value}", " ".repeat(11 - value.len()))
    } else {
        value.to_owned()
    }
}

fn print_row(io: &mut Io, label: &str, values: &[u64], cfg: &Config) -> std::io::Result<()> {
    let mut line = pad_label(label);
    for (idx, &v) in values.iter().enumerate() {
        if idx > 0 {
            line.push(' ');
        }
        line.push_str(&pad_value(&scale_size(v, cfg)));
    }
    writeln!(io.stdout, "{line}")
}

fn print_once(io: &mut Io, cfg: &Config) -> std::io::Result<()> {
    let snap = take_snapshot();
    if cfg.line {
        let line = format!(
            "SwapUse {} CachUse {}  MemUse {} MemFree {} ",
            pad_value(&scale_size(snap.swap_used, cfg)),
            pad_value(&scale_size(0, cfg)),
            pad_value(&scale_size(snap.used, cfg)),
            pad_value(&scale_size(snap.free, cfg)),
        );
        return writeln!(io.stdout, "{line}");
    }

    if cfg.wide {
        writeln!(
            io.stdout,
            "               total        used        free      shared     buffers       cache   available"
        )?;
        print_row(
            io,
            "Mem:",
            &[snap.total, snap.used, snap.free, 0, 0, 0, snap.available],
            cfg,
        )?;
    } else {
        writeln!(
            io.stdout,
            "               total        used        free      shared  buff/cache   available"
        )?;
        print_row(
            io,
            "Mem:",
            &[snap.total, snap.used, snap.free, 0, 0, snap.available],
            cfg,
        )?;
    }

    if cfg.lohi {
        print_row(io, "Low:", &[snap.total, snap.used, snap.free], cfg)?;
        print_row(io, "High:", &[0, 0, 0], cfg)?;
    }

    print_row(
        io,
        "Swap:",
        &[snap.swap_total, snap.swap_used, snap.swap_free],
        cfg,
    )?;

    if cfg.total {
        print_row(
            io,
            "Total:",
            &[
                snap.total + snap.swap_total,
                snap.used + snap.swap_used,
                snap.free + snap.swap_free,
            ],
            cfg,
        )?;
    }

    if cfg.committed {
        let headroom = snap.commit_limit.saturating_sub(snap.committed);
        print_row(
            io,
            "Comm:",
            &[snap.commit_limit, snap.committed, headroom],
            cfg,
        )?;
    }
    Ok(())
}

pub fn free_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut cfg = Config::default();
    let mut unit_count = 0usize;
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;
    let mut i = 0usize;

    while i < args.len() {
        let s = args[i].to_string_lossy();
        if no_more_opts {
            positionals.push(s.into_owned());
            i += 1;
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            i += 1;
            continue;
        }
        if s.starts_with("--") {
            match s.as_ref() {
                "--help" => {
                    io.stdout.write_all(FREE_HELP.as_bytes())?;
                    return Ok(0);
                }
                "--version" => {
                    write!(
                        io.stdout,
                        "{}",
                        gnuhelp::coreutils_version("free", "WinuxCmd")
                    )?;
                    return Ok(0);
                }
                "--bytes" => {
                    unit_count += 1;
                    cfg.exponent = 1;
                }
                "--kibi" => {
                    unit_count += 1;
                    cfg.exponent = 2;
                }
                "--mebi" => {
                    unit_count += 1;
                    cfg.exponent = 3;
                }
                "--gibi" => {
                    unit_count += 1;
                    cfg.exponent = 4;
                }
                "--tebi" => {
                    unit_count += 1;
                    cfg.exponent = 5;
                }
                "--pebi" => {
                    unit_count += 1;
                    cfg.exponent = 6;
                }
                "--kilo" => {
                    unit_count += 1;
                    cfg.exponent = 2;
                    cfg.si = true;
                }
                "--mega" => {
                    unit_count += 1;
                    cfg.exponent = 3;
                    cfg.si = true;
                }
                "--giga" => {
                    unit_count += 1;
                    cfg.exponent = 4;
                    cfg.si = true;
                }
                "--tera" => {
                    unit_count += 1;
                    cfg.exponent = 5;
                    cfg.si = true;
                }
                "--peta" => {
                    unit_count += 1;
                    cfg.exponent = 6;
                    cfg.si = true;
                }
                "--si" => cfg.si = true,
                "--human" => cfg.human = true,
                "--lohi" => cfg.lohi = true,
                "--total" => cfg.total = true,
                "--wide" => cfg.wide = true,
                "--line" => cfg.line = true,
                "--committed" => cfg.committed = true,
                "--seconds" => {
                    i += 1;
                    if i >= args.len() {
                        let _ = usage_error(
                            ctx,
                            io,
                            "free",
                            &argopt::option_requires_argument(ctx, "--seconds"),
                        );
                        return Ok(1);
                    }
                    let val = args[i].to_string_lossy();
                    cfg.interval = match val.parse::<i64>() {
                        Ok(v) if v >= 0 => v,
                        _ => {
                            writeln!(io.stderr, "free: invalid interval")?;
                            return Ok(1);
                        }
                    };
                }
                "--count" => {
                    i += 1;
                    if i >= args.len() {
                        let _ = usage_error(
                            ctx,
                            io,
                            "free",
                            &argopt::option_requires_argument(ctx, "--count"),
                        );
                        return Ok(1);
                    }
                    let val = args[i].to_string_lossy();
                    cfg.count = match val.parse::<i64>() {
                        Ok(v) if v >= 0 => v,
                        _ => {
                            writeln!(io.stderr, "free: invalid count")?;
                            return Ok(1);
                        }
                    };
                }
                other => {
                    if let Some(val) = other.strip_prefix("--seconds=") {
                        cfg.interval = match val.parse::<i64>() {
                            Ok(v) if v >= 0 => v,
                            _ => {
                                writeln!(io.stderr, "free: invalid interval")?;
                                return Ok(1);
                            }
                        };
                    } else if let Some(val) = other.strip_prefix("--count=") {
                        cfg.count = match val.parse::<i64>() {
                            Ok(v) if v >= 0 => v,
                            _ => {
                                writeln!(io.stderr, "free: invalid count")?;
                                return Ok(1);
                            }
                        };
                    } else {
                        let _ = usage_error(ctx, io, "free", &argopt::unrecognized_option(ctx, other));
                        return Ok(1);
                    }
                }
            }
            i += 1;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let bytes = s.as_bytes();
            let mut j = 1usize;
            while j < bytes.len() {
                match bytes[j] {
                    b'V' => {
                        write!(
                            io.stdout,
                            "{}",
                            gnuhelp::coreutils_version("free", "WinuxCmd")
                        )?;
                        return Ok(0);
                    }
                    b'b' => {
                        unit_count += 1;
                        cfg.exponent = 1;
                    }
                    b'k' => {
                        unit_count += 1;
                        cfg.exponent = 2;
                    }
                    b'm' => {
                        unit_count += 1;
                        cfg.exponent = 3;
                    }
                    b'g' => {
                        unit_count += 1;
                        cfg.exponent = 4;
                    }
                    b'h' => cfg.human = true,
                    b'l' => cfg.lohi = true,
                    b't' => cfg.total = true,
                    b'w' => cfg.wide = true,
                    b'L' => cfg.line = true,
                    b'v' => cfg.committed = true,
                    b's' => {
                        let val = if j + 1 < bytes.len() {
                            s[j + 1..].to_owned()
                        } else {
                            i += 1;
                            if i >= args.len() {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "free",
                                    &argopt::short_option_requires_argument(ctx, "s"),
                                );
                                return Ok(1);
                            }
                            args[i].to_string_lossy().into_owned()
                        };
                        cfg.interval = match val.parse::<i64>() {
                            Ok(v) if v >= 0 => v,
                            _ => {
                                writeln!(io.stderr, "free: invalid interval")?;
                                return Ok(1);
                            }
                        };
                        break;
                    }
                    b'c' => {
                        let val = if j + 1 < bytes.len() {
                            s[j + 1..].to_owned()
                        } else {
                            i += 1;
                            if i >= args.len() {
                                let _ = usage_error(
                                    ctx,
                                    io,
                                    "free",
                                    &argopt::short_option_requires_argument(ctx, "c"),
                                );
                                return Ok(1);
                            }
                            args[i].to_string_lossy().into_owned()
                        };
                        cfg.count = match val.parse::<i64>() {
                            Ok(v) if v >= 0 => v,
                            _ => {
                                writeln!(io.stderr, "free: invalid count")?;
                                return Ok(1);
                            }
                        };
                        break;
                    }
                    other => {
                        let _ = usage_error(
                            ctx,
                            io,
                            "free",
                            &argopt::invalid_option(ctx, &(other as char).to_string()),
                        );
                        return Ok(1);
                    }
                }
                j += 1;
            }
            i += 1;
            continue;
        }
        positionals.push(s.into_owned());
        i += 1;
    }

    if let Some(first) = positionals.first() {
        let _ = usage_error(ctx, io, "free", &messages::extra_operand(ctx, first));
        return Ok(1);
    }
    if unit_count > 1 {
        writeln!(io.stderr, "free: Multiple unit options don't make sense.")?;
        return Ok(1);
    }

    let looping = cfg.interval > 0 || cfg.count > 0;
    let interval = if cfg.interval > 0 { cfg.interval } else { 1 };
    let max_count = if looping {
        if cfg.count > 0 {
            cfg.count
        } else {
            -1
        }
    } else {
        1
    };

    let mut iter = 0i64;
    while max_count < 0 || iter < max_count {
        if iter > 0 {
            thread::sleep(Duration::from_secs(interval as u64));
        }
        print_once(io, &cfg)?;
        iter += 1;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_free(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("free-test");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = free_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn default_wide_lohi_total_committed_and_line() {
        let (rc, out, err) = run_free(&[]);
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert!(out.contains("Mem:"));
        assert!(out.contains("Swap:"));

        let (rc, out, _) = run_free(&["-w", "-l", "-t", "-v"]);
        assert_eq!(rc, 0);
        assert!(out.contains("buffers"));
        assert!(out.contains("Low:"));
        assert!(out.contains("High:"));
        assert!(out.contains("Total:"));
        assert!(out.contains("Comm:"));

        let (rc, out, _) = run_free(&["-L"]);
        assert_eq!(rc, 0);
        assert!(out.contains("SwapUse"));
        assert!(out.contains("MemFree"));
    }

    #[test]
    fn unit_conflict_and_extra_operand_rejected() {
        let (rc, _, err) = run_free(&["-b", "-m"]);
        assert_eq!(rc, 1);
        assert!(err.contains("Multiple unit options don't make sense."));

        let (rc, _, err) = run_free(&["extra"]);
        assert_eq!(rc, 1);
        assert!(err.contains("extra operand 'extra'"));

        let (rc, out, _) = run_free(&["--seconds", "0", "--count", "1"]);
        assert_eq!(rc, 0);
        assert!(out.contains("Mem:"));
    }
}
