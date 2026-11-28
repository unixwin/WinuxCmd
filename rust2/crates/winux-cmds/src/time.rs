//! `time` — time a simple command or give resource usage (#1488).
//!
//! Port of `src/commands/time.cpp`. Explicit `Io`/`Ctx`, zero globals.

use std::ffi::OsString;
use std::fs::OpenOptions;
use std::io::Write;
use std::path::Path;
use std::time::Instant;

use crate::argopt::{self, OptSpec};
use crate::childproc::{build_child_command, resolve_child, ChildResolve};
use crate::fsutil;
use winux_core::{CmdResult, Ctx, Io};

const RC_TIME_FAIL: u32 = 125;

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('p'),
        long: "portability",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "posix",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "format",
        takes_value: true,
    },
    OptSpec {
        short: Some('o'),
        long: "output",
        takes_value: true,
    },
    OptSpec {
        short: Some('a'),
        long: "append",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: Some('q'),
        long: "quiet",
        takes_value: false,
    },
];

struct TimingData {
    real_seconds: f64,
    user_seconds: f64,
    sys_seconds: f64,
    max_rss_kb: u64,
    minor_faults: u64,
    page_size: u32,
    exit_code: u32,
    command_line: String,
}

fn format_seconds(seconds: f64, posix: bool) -> String {
    let value = seconds.max(0.0);
    if posix {
        format!("{value:.3}")
    } else {
        format!("{value:.3}s")
    }
}

fn format_elapsed_hms(seconds: f64) -> String {
    let value = seconds.max(0.0);
    let total_centis = (value * 100.0 + 0.5) as u64;
    let hours = total_centis / 360000;
    let minutes = (total_centis / 6000) % 60;
    let secs = (total_centis / 100) % 60;
    let centis = total_centis % 100;
    if hours > 0 {
        format!("{hours}:{minutes:02}:{secs:02}.{centis:02}")
    } else {
        format!("{minutes}:{secs:02}.{centis:02}")
    }
}

fn expand_format(fmt: &str, data: &TimingData) -> String {
    let mut out = String::new();
    let cpu_percent = if data.real_seconds > 0.0 {
        (data.user_seconds + data.sys_seconds) / data.real_seconds * 100.0
    } else {
        0.0
    };
    let bytes = fmt.as_bytes();
    let mut i = 0usize;
    while i < bytes.len() {
        let c = bytes[i] as char;
        if c == '\\' && i + 1 < bytes.len() {
            i += 1;
            match bytes[i] as char {
                'n' => out.push('\n'),
                't' => out.push('\t'),
                '\\' => out.push('\\'),
                other => out.push(other),
            }
            i += 1;
            continue;
        }
        if c != '%' || i + 1 >= bytes.len() {
            out.push(c);
            i += 1;
            continue;
        }
        i += 1;
        let spec = bytes[i] as char;
        match spec {
            '%' => out.push('%'),
            'e' => out.push_str(&format!("{:.2}", data.real_seconds.max(0.0))),
            'E' => out.push_str(&format_elapsed_hms(data.real_seconds)),
            'U' => out.push_str(&format!("{:.2}", data.user_seconds.max(0.0))),
            'S' => out.push_str(&format!("{:.2}", data.sys_seconds.max(0.0))),
            'P' => out.push_str(&format!("{cpu_percent:.0}%")),
            'M' => out.push_str(&data.max_rss_kb.to_string()),
            'Z' => out.push_str(&data.page_size.to_string()),
            'R' => out.push_str(&data.minor_faults.to_string()),
            'x' => out.push_str(&data.exit_code.to_string()),
            'C' => out.push_str(&data.command_line),
            'K' | 'D' | 'p' | 'X' | 'F' | 'W' | 'c' | 'w' | 'I' | 'O' | 'r' | 's' | 'k' => {
                out.push('0')
            }
            other => {
                out.push('%');
                out.push(other);
            }
        }
        i += 1;
    }
    out
}

fn verbose_report(data: &TimingData) -> String {
    let cpu_percent = if data.real_seconds > 0.0 {
        (data.user_seconds + data.sys_seconds) / data.real_seconds * 100.0
    } else {
        0.0
    };
    let mut out = String::new();
    let mut line = |label: &str, val: &str| {
        out.push('\t');
        out.push_str(label);
        out.push_str(": ");
        out.push_str(val);
        out.push('\n');
    };
    line("Command being timed", &format!("\"{}\"", data.command_line));
    line("User time (seconds)", &format!("{:.2}", data.user_seconds));
    line("System time (seconds)", &format!("{:.2}", data.sys_seconds));
    line("Percent of CPU this job got", &format!("{cpu_percent:.0}%"));
    line(
        "Elapsed (wall clock) time (h:mm:ss or m:ss)",
        &format_elapsed_hms(data.real_seconds),
    );
    line("Average shared text size (kbytes)", "0");
    line("Average unshared data size (kbytes)", "0");
    line("Average stack size (kbytes)", "0");
    line("Average total size (kbytes)", "0");
    line(
        "Maximum resident set size (kbytes)",
        &data.max_rss_kb.to_string(),
    );
    line("Average resident set size (kbytes)", "0");
    line("Major (requiring I/O) page faults", "0");
    line(
        "Minor (reclaiming a frame) page faults",
        &data.minor_faults.to_string(),
    );
    line("Voluntary context switches", "0");
    line("Involuntary context switches", "0");
    line("Swaps", "0");
    line("File system inputs", "0");
    line("File system outputs", "0");
    line("Socket messages sent", "0");
    line("Socket messages received", "0");
    line("Signals delivered", "0");
    line("Page size (bytes)", &data.page_size.to_string());
    line("Exit status", &data.exit_code.to_string());
    out
}

fn default_report(data: &TimingData, posix: bool) -> String {
    format!(
        "real {}\nuser {}\nsys {}\n",
        format_seconds(data.real_seconds, posix),
        format_seconds(data.user_seconds, posix),
        format_seconds(data.sys_seconds, posix)
    )
}

#[cfg(windows)]
mod sys {
    use std::ffi::c_void;
    use std::os::windows::io::AsRawHandle;
    use std::process::Child;

    #[repr(C)]
    #[derive(Default)]
    struct Filetime {
        dw_low_date_time: u32,
        dw_high_date_time: u32,
    }

    #[repr(C)]
    struct SystemInfo {
        w_processor_architecture: u16,
        w_reserved: u16,
        dw_page_size: u32,
        lp_min: *mut c_void,
        lp_max: *mut c_void,
        dw_active_processor_mask: usize,
        dw_number_of_processors: u32,
        dw_processor_type: u32,
        dw_allocation_granularity: u32,
        w_processor_level: u16,
        w_processor_revision: u16,
    }

    #[repr(C)]
    #[derive(Default)]
    struct ProcessMemoryCounters {
        cb: u32,
        page_fault_count: u32,
        peak_working_set_size: usize,
        working_set_size: usize,
        quota_peak_paged_pool_usage: usize,
        quota_paged_pool_usage: usize,
        quota_peak_non_paged_pool_usage: usize,
        quota_non_paged_pool_usage: usize,
        pagefile_usage: usize,
        peak_pagefile_usage: usize,
    }

    extern "system" {
        fn GetProcessTimes(
            h_process: *mut c_void,
            lp_creation_time: *mut Filetime,
            lp_exit_time: *mut Filetime,
            lp_kernel_time: *mut Filetime,
            lp_user_time: *mut Filetime,
        ) -> i32;
        fn GetSystemInfo(lp_system_info: *mut SystemInfo);
        fn K32GetProcessMemoryInfo(
            process: *mut c_void,
            ppsmem_counters: *mut ProcessMemoryCounters,
            cb: u32,
        ) -> i32;
    }

    fn ft_to_secs(ft: &Filetime) -> f64 {
        let ticks = ((ft.dw_high_date_time as u64) << 32) | (ft.dw_low_date_time as u64);
        (ticks as f64) / 10_000_000.0
    }

    pub fn collect_child_metrics(child: &Child) -> (f64, f64, u64, u64, u32) {
        let handle = child.as_raw_handle() as *mut c_void;
        let mut creation = Filetime::default();
        let mut exit = Filetime::default();
        let mut kernel = Filetime::default();
        let mut user = Filetime::default();
        let (u_sec, s_sec) =
            if unsafe { GetProcessTimes(handle, &mut creation, &mut exit, &mut kernel, &mut user) }
                != 0
            {
                (ft_to_secs(&user), ft_to_secs(&kernel))
            } else {
                (0.0, 0.0)
            };

        let mut si: SystemInfo = unsafe { std::mem::zeroed() };
        unsafe { GetSystemInfo(&mut si) };
        let page_size = si.dw_page_size;

        let mut mem = ProcessMemoryCounters::default();
        mem.cb = std::mem::size_of::<ProcessMemoryCounters>() as u32;
        let (max_rss_kb, minor_faults) =
            if unsafe { K32GetProcessMemoryInfo(handle, &mut mem, mem.cb) } != 0 {
                (
                    (mem.peak_working_set_size / 1024) as u64,
                    mem.page_fault_count as u64,
                )
            } else {
                (0, 0)
            };

        (u_sec, s_sec, max_rss_kb, minor_faults, page_size)
    }
}

#[cfg(not(windows))]
mod sys {
    use std::process::Child;

    pub fn collect_child_metrics(_child: &Child) -> (f64, f64, u64, u64, u32) {
        (0.0, 0.0, 0, 0, 4096)
    }
}

fn split_at_first_positional(args: &[OsString]) -> usize {
    let mut i = 0usize;
    while i < args.len() {
        let s = args[i].to_string_lossy();
        if s == "--" {
            return args.len();
        }
        if s.starts_with("--") {
            let takes_sep_val =
                !s.contains('=') && matches!(s.as_ref(), "--format" | "--output");
            i += if takes_sep_val { 2 } else { 1 };
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let mut consumes_next = false;
            let bytes = s.as_bytes();
            for idx in 1..bytes.len() {
                if matches!(bytes[idx], b'f' | b'o') {
                    if idx + 1 == bytes.len() {
                        consumes_next = true;
                    }
                    break;
                }
            }
            i += if consumes_next { 2 } else { 1 };
            continue;
        }
        return i;
    }
    args.len()
}

pub fn time_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let split = split_at_first_positional(args).min(args.len());
    let mut p = match argopt::parse("time", &args[..split], OPTS, io, ctx, RC_TIME_FAIL) {
        Ok(p) => p,
        Err(rc) => return Ok(rc),
    };
    p.positionals.extend_from_slice(&args[split..]);

    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        if which == argopt::HELP {
            writeln!(
                io.stdout,
                "Usage: time [OPTION]... COMMAND [ARG]...\nRun COMMAND, then print system resource usage."
            )?;
        } else {
            writeln!(io.stdout, "GNU time 1.9 (WinuxCmd)")?;
        }
        return Ok(0);
    }

    let posix = p.has("portability") || p.has("posix");
    let verbose = p.has("verbose");
    let quiet = p.has("quiet");
    let append = p.has("append");
    let format = p.value_of("format");
    let output_path = p.value_of("output");

    let Some(command) = p.positionals.first().map(|a| a.to_string_lossy().into_owned()) else {
        writeln!(io.stderr, "time: missing operand")?;
        writeln!(io.stderr, "Try 'time --help' for more information.")?;
        return Ok(RC_TIME_FAIL);
    };
    let cmd_args: Vec<String> = p.positionals[1..]
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    let mut out_file = if let Some(path) = output_path {
        let resolved = fsutil::resolve(ctx, Path::new(path));
        match OpenOptions::new()
            .write(true)
            .create(true)
            .append(append)
            .truncate(!append)
            .open(&resolved)
        {
            Ok(f) => Some(f),
            Err(e) => {
                writeln!(
                    io.stderr,
                    "time: cannot open '{path}' for writing: {}",
                    fsutil::reason(&e)
                )?;
                return Ok(RC_TIME_FAIL);
            }
        }
    } else {
        None
    };

    let mut cmd = match resolve_child(&command, ctx) {
        ChildResolve::Found(launch) => build_child_command(&launch, &cmd_args, ctx),
        ChildResolve::NotFound => {
            writeln!(
                io.stderr,
                "time: failed to run command '{command}': No such file or directory"
            )?;
            return Ok(127);
        }
        ChildResolve::BadFormat => {
            writeln!(
                io.stderr,
                "time: failed to run command '{command}': Exec format error"
            )?;
            return Ok(126);
        }
        ChildResolve::NotExecutable | ChildResolve::NotADirectory => {
            writeln!(
                io.stderr,
                "time: failed to run command '{command}': Permission denied"
            )?;
            return Ok(126);
        }
    };

    let start = Instant::now();
    let mut child = match cmd.spawn() {
        Ok(c) => c,
        Err(e) => {
            writeln!(
                io.stderr,
                "time: failed to run command '{command}': {}",
                fsutil::reason(&e)
            )?;
            return Ok(126);
        }
    };
    let status = match child.wait() {
        Ok(s) => s,
        Err(e) => {
            writeln!(io.stderr, "time: failed while waiting for command: {e}")?;
            return Ok(RC_TIME_FAIL);
        }
    };
    let real_seconds = start.elapsed().as_secs_f64();
    let (user_seconds, sys_seconds, max_rss_kb, minor_faults, page_size) =
        sys::collect_child_metrics(&child);
    let exit_code = status.code().unwrap_or(1) as u32;

    let mut command_line = command;
    for a in &cmd_args {
        command_line.push(' ');
        command_line.push_str(a);
    }

    let data = TimingData {
        real_seconds,
        user_seconds,
        sys_seconds,
        max_rss_kb,
        minor_faults,
        page_size,
        exit_code,
        command_line,
    };

    let mut report = if let Some(fmt) = format {
        let mut s = expand_format(fmt, &data);
        s.push('\n');
        s
    } else if verbose {
        verbose_report(&data)
    } else {
        default_report(&data, posix)
    };

    if !quiet && (exit_code & 0xF000_0000) == 0xC000_0000 {
        report.push_str(&format!(
            "Command terminated abnormally (status 0x{exit_code:08X})\n"
        ));
    }

    if let Some(ref mut f) = out_file {
        if let Err(e) = f.write_all(report.as_bytes()) {
            writeln!(io.stderr, "time: failed to write the report file: {e}")?;
            return Ok(RC_TIME_FAIL);
        }
    } else {
        write!(io.stderr, "{report}")?;
    }

    Ok(exit_code)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn missing_operand_and_format_expansion() {
        let sc = ScratchCtx::new("time-err");
        let mut io = MemIo::new("");
        let rc = time_cmd(&[], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 125);
        assert!(io.err().contains("missing operand"));

        let data = TimingData {
            real_seconds: 1.25,
            user_seconds: 0.5,
            sys_seconds: 0.25,
            max_rss_kb: 1024,
            minor_faults: 42,
            page_size: 4096,
            exit_code: 7,
            command_line: "echo hi".to_owned(),
        };
        assert_eq!(
            expand_format("elapsed %e status %x cmd %C %%", &data),
            "elapsed 1.25 status 7 cmd echo hi %"
        );
        assert_eq!(format_elapsed_hms(3661.5), "1:01:01.50");
    }
}
