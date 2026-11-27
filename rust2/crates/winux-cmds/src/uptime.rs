//! `uptime` — procps-ng uptime semantics on Windows, ported for WinuxCmd 2.0 (#1488).
//!
//! Uses `GetTickCount64`, `GetLocalTime`, `GetSystemTimeAsFileTime`, and
//! `WTSEnumerateSessionsW` on Windows (with `/proc/uptime` / `SystemTime`
//! fallback on non-Windows hosts), explicit `Io`/`Ctx`, and zero globals.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::gnuhelp;
use crate::messages;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const UPTIME_HELP: &str = "\
Usage:
 uptime [options]

Options:
 -p, --pretty   show uptime in pretty format
 -h, --help     display this help and exit
 -s, --since    system up since
 -V, --version  output version information and exit
";

fn format_pretty_uptime(uptime_secs: u64) -> String {
    let mut rem = uptime_secs as i64;
    let original = rem;

    let mut take = |unit: i64| -> i64 {
        if rem > unit {
            let value = rem / unit;
            rem -= value * unit;
            value
        } else {
            0
        }
    };

    let decades = take(60 * 60 * 24 * 365 * 10);
    let years = take(60 * 60 * 24 * 365);
    let weeks = take(60 * 60 * 24 * 7);
    let days = take(60 * 60 * 24);
    let hours = take(60 * 60);
    let minutes = take(60);

    let mut result = String::from("up ");
    let mut first = true;
    let mut append = |value: i64, singular: &str, plural: &str| {
        if value == 0 {
            return;
        }
        if !first {
            result.push_str(", ");
        }
        result.push_str(&value.to_string());
        result.push(' ');
        result.push_str(if value > 1 { plural } else { singular });
        first = false;
    };

    append(decades, "decade", "decades");
    append(years, "year", "years");
    append(weeks, "week", "weeks");
    append(days, "day", "days");
    append(hours, "hour", "hours");
    if minutes > 0 || original < 60 {
        if !first {
            result.push_str(", ");
        }
        result.push_str(&minutes.to_string());
        result.push_str(if minutes != 1 { " minutes" } else { " minute" });
    }
    result
}

fn format_default_line(uptime_secs: u64, users: usize, now_hms: &str) -> String {
    let days = uptime_secs / (60 * 60 * 24);
    let hours = (uptime_secs / (60 * 60)) % 24;
    let minutes = (uptime_secs / 60) % 60;

    let mut line = format!(" {now_hms} up ");
    if days > 0 {
        line.push_str(&format!(
            "{days} {}, ",
            if days > 1 { "days" } else { "day" }
        ));
    }
    if hours > 0 {
        line.push_str(&format!("{hours:2}:{minutes:02}, "));
    } else {
        line.push_str(&format!("{minutes} min, "));
    }
    let user_word = if users == 1 { "user" } else { "users" };
    line.push_str(&format!(
        "{users:2} {user_word},  load average: N/A, N/A, N/A"
    ));
    line
}

#[cfg(windows)]
mod sys {
    #[repr(C)]
    struct SystemTimeWin {
        year: u16,
        month: u16,
        day_of_week: u16,
        day: u16,
        hour: u16,
        minute: u16,
        second: u16,
        milliseconds: u16,
    }

    #[repr(C)]
    struct FileTimeWin {
        low: u32,
        high: u32,
    }

    #[repr(C)]
    struct WtsSessionInfoW {
        session_id: u32,
        winstation_name: *mut u16,
        state: i32,
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn GetTickCount64() -> u64;
        fn GetLocalTime(lpSystemTime: *mut SystemTimeWin);
        fn GetSystemTimeAsFileTime(lpSystemTimeAsFileTime: *mut FileTimeWin);
        fn FileTimeToLocalFileTime(
            lpFileTime: *const FileTimeWin,
            lpLocalFileTime: *mut FileTimeWin,
        ) -> i32;
        fn FileTimeToSystemTime(
            lpFileTime: *const FileTimeWin,
            lpSystemTime: *mut SystemTimeWin,
        ) -> i32;
    }

    #[link(name = "wtsapi32")]
    extern "system" {
        fn WTSEnumerateSessionsW(
            server: isize,
            reserved: u32,
            version: u32,
            sessions: *mut *mut WtsSessionInfoW,
            count: *mut u32,
        ) -> i32;
        fn WTSQuerySessionInformationW(
            server: isize,
            session_id: u32,
            class: u32,
            buffer: *mut *mut u16,
            bytes: *mut u32,
        ) -> i32;
        fn WTSFreeMemory(memory: *mut u8);
    }

    pub fn uptime_seconds() -> u64 {
        unsafe { GetTickCount64() / 1000 }
    }

    pub fn now_hms() -> String {
        let mut st = SystemTimeWin {
            year: 1970,
            month: 1,
            day_of_week: 0,
            day: 1,
            hour: 0,
            minute: 0,
            second: 0,
            milliseconds: 0,
        };
        unsafe {
            GetLocalTime(&mut st);
        }
        format!("{:02}:{:02}:{:02}", st.hour, st.minute, st.second)
    }

    pub fn boot_since_ymd_hms(uptime_secs: u64) -> String {
        let mut ft = FileTimeWin { low: 0, high: 0 };
        unsafe {
            GetSystemTimeAsFileTime(&mut ft);
        }
        let now_100ns = ((ft.high as u64) << 32) | (ft.low as u64);
        let boot_100ns = now_100ns.saturating_sub(uptime_secs.saturating_mul(10_000_000));
        let boot_ft = FileTimeWin {
            low: (boot_100ns & 0xFFFF_FFFF) as u32,
            high: (boot_100ns >> 32) as u32,
        };
        let mut local_ft = FileTimeWin { low: 0, high: 0 };
        let mut st = SystemTimeWin {
            year: 1970,
            month: 1,
            day_of_week: 0,
            day: 1,
            hour: 0,
            minute: 0,
            second: 0,
            milliseconds: 0,
        };
        unsafe {
            if FileTimeToLocalFileTime(&boot_ft, &mut local_ft) != 0 {
                FileTimeToSystemTime(&local_ft, &mut st);
            }
        }
        format!(
            "{:04}-{:02}-{:02} {:02}:{:02}:{:02}",
            st.year, st.month, st.day, st.hour, st.minute, st.second
        )
    }

    pub fn logged_in_users() -> usize {
        const WTS_ACTIVE: i32 = 0;
        const WTS_USER_NAME: u32 = 5;
        let mut sessions: *mut WtsSessionInfoW = std::ptr::null_mut();
        let mut count: u32 = 0;
        let ok = unsafe { WTSEnumerateSessionsW(0isize, 0, 1, &mut sessions, &mut count) };
        if ok == 0 || sessions.is_null() {
            return 1;
        }
        let mut users = 0usize;
        for idx in 0..(count as usize) {
            let sess = unsafe { &*sessions.add(idx) };
            if sess.state != WTS_ACTIVE {
                continue;
            }
            let mut buf: *mut u16 = std::ptr::null_mut();
            let mut bytes: u32 = 0;
            let qok = unsafe {
                WTSQuerySessionInformationW(0isize, sess.session_id, WTS_USER_NAME, &mut buf, &mut bytes)
            };
            if qok != 0 && !buf.is_null() && bytes > 2 && unsafe { *buf } != 0 {
                users += 1;
            }
            if !buf.is_null() {
                unsafe {
                    WTSFreeMemory(buf as *mut u8);
                }
            }
        }
        unsafe {
            WTSFreeMemory(sessions as *mut u8);
        }
        users.max(1)
    }
}

#[cfg(not(windows))]
mod sys {
    pub fn uptime_seconds() -> u64 {
        std::fs::read_to_string("/proc/uptime")
            .ok()
            .and_then(|s| s.split_whitespace().next().map(str::to_owned))
            .and_then(|v| v.split('.').next().and_then(|i| i.parse::<u64>().ok()))
            .unwrap_or(0)
    }

    fn civil_from_unix(secs: u64) -> (i32, u32, u32, u32, u32, u32) {
        let day_secs = (secs % 86_400) as u32;
        let hour = day_secs / 3600;
        let min = (day_secs % 3600) / 60;
        let sec = day_secs % 60;
        let z = (secs / 86_400) as i64 + 719_468;
        let era = if z >= 0 { z } else { z - 146_096 } / 146_097;
        let doe = z - era * 146_097;
        let yoe = (doe - doe / 1460 + doe / 36524 - doe / 146_096) / 365;
        let y = yoe + era * 400;
        let doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        let mp = (5 * doy + 2) / 153;
        let d = doy - (153 * mp + 2) / 5 + 1;
        let m = if mp < 10 { mp + 3 } else { mp - 9 };
        let year = (y + if m <= 2 { 1 } else { 0 }) as i32;
        (year, m as u32, d as u32, hour, min, sec)
    }

    pub fn now_hms() -> String {
        let secs = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0);
        let (_, _, _, h, m, s) = civil_from_unix(secs);
        format!("{h:02}:{m:02}:{s:02}")
    }

    pub fn boot_since_ymd_hms(uptime_secs: u64) -> String {
        let secs = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .map(|d| d.as_secs())
            .unwrap_or(0)
            .saturating_sub(uptime_secs);
        let (y, mo, d, h, m, s) = civil_from_unix(secs);
        format!("{y:04}-{mo:02}-{d:02} {h:02}:{m:02}:{s:02}")
    }

    pub fn logged_in_users() -> usize {
        1
    }
}

pub fn uptime_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut pretty = false;
    let mut since = false;
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if no_more_opts {
            positionals.push(s.into_owned());
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            continue;
        }
        if s == "-h" || s == "--help" {
            io.stdout.write_all(UPTIME_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "-V" || s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("uptime", "WinuxCmd")
            )?;
            return Ok(0);
        }
        if s == "--pretty" {
            pretty = true;
            continue;
        }
        if s == "--since" {
            since = true;
            continue;
        }
        if s.starts_with("--") {
            let _ = usage_error(ctx, io, "uptime", &argopt::unrecognized_option(ctx, &s));
            return Ok(1);
        }
        if s.starts_with('-') && s.len() > 1 {
            for ch in s[1..].chars() {
                match ch {
                    'p' => pretty = true,
                    's' => since = true,
                    'h' => {
                        io.stdout.write_all(UPTIME_HELP.as_bytes())?;
                        return Ok(0);
                    }
                    'V' => {
                        write!(
                            io.stdout,
                            "{}",
                            gnuhelp::coreutils_version("uptime", "WinuxCmd")
                        )?;
                        return Ok(0);
                    }
                    other => {
                        let _ = usage_error(
                            ctx,
                            io,
                            "uptime",
                            &argopt::invalid_option(ctx, &other.to_string()),
                        );
                        return Ok(1);
                    }
                }
            }
            continue;
        }
        positionals.push(s.into_owned());
    }

    if let Some(first) = positionals.first() {
        let _ = usage_error(ctx, io, "uptime", &messages::extra_operand(ctx, first));
        return Ok(1);
    }

    let uptime_secs = sys::uptime_seconds();
    if since {
        writeln!(io.stdout, "{}", sys::boot_since_ymd_hms(uptime_secs))?;
    } else if pretty {
        writeln!(io.stdout, "{}", format_pretty_uptime(uptime_secs))?;
    } else {
        let users = sys::logged_in_users();
        let now_hms = sys::now_hms();
        writeln!(io.stdout, "{}", format_default_line(uptime_secs, users, &now_hms))?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn pretty_and_default_formatters() {
        assert_eq!(format_pretty_uptime(30), "up 0 minutes");
        assert_eq!(format_pretty_uptime(60), "up 0 minutes");
        assert_eq!(format_pretty_uptime(61), "up 1 minute");
        assert_eq!(
            format_pretty_uptime(2 * 86_400 + 3 * 3600 + 5 * 60),
            "up 2 days, 3 hours, 5 minutes"
        );
        assert_eq!(
            format_default_line(2 * 86_400 + 30 * 60, 1, "12:34:56"),
            " 12:34:56 up 2 days, 30 min,  1 user,  load average: N/A, N/A, N/A"
        );
        assert_eq!(
            format_default_line(2 * 3600 + 5 * 60, 3, "01:02:03"),
            " 01:02:03 up  2:05,  3 users,  load average: N/A, N/A, N/A"
        );
    }

    #[test]
    fn extra_operand_and_modes() {
        let sc = ScratchCtx::new("uptime-test");
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let rc = uptime_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert!(io.out().contains("load average: N/A, N/A, N/A"));

        let mut io_p = MemIo::new("");
        let rc = uptime_cmd(&[OsString::from("-p")], &mut io_p.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert!(io_p.out().starts_with("up "));

        let mut io_s = MemIo::new("");
        let rc = uptime_cmd(&[OsString::from("-s")], &mut io_s.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert_eq!(io_s.out().trim().len(), 19);

        let mut io_err = MemIo::new("");
        let rc = uptime_cmd(&[OsString::from("extra")], &mut io_err.split(), &ctx).unwrap();
        assert_eq!(rc, 1);
        assert!(io_err.err().contains("extra operand 'extra'"));
    }
}
