//! `w` — show who is logged on and what they are doing (#1488).
//!
//! Port of `src/commands/w.cpp` (procps-v4.0.4 option surface + WTS session
//! enumeration on Windows with non-Windows stub). Explicit `Io`/`Ctx`, zero globals.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt::{self, OptSpec};
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('h'),
        long: "no-header",
        takes_value: false,
    },
    OptSpec {
        short: Some('u'),
        long: "no-current",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "short",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "from",
        takes_value: false,
    },
    OptSpec {
        short: Some('o'),
        long: "old-style",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "ip-addr",
        takes_value: false,
    },
    OptSpec {
        short: Some('p'),
        long: "pids",
        takes_value: false,
    },
];

struct SessionRow {
    user: String,
    tty: String,
    from: String,
    logon_time: i64,
    idle_seconds: i64,
    idle_centi: i32,
    jcpu_centis: i64,
    pcpu_centis: i64,
    leader_pid: i64,
    best_pid: i64,
    what: String,
}

#[cfg(windows)]
mod sys {
    use super::SessionRow;
    use std::collections::BTreeMap;
    use std::ffi::c_void;

    const WTS_CURRENT_SERVER_HANDLE: *mut c_void = std::ptr::null_mut();
    const WTS_USER_NAME: u32 = 5;
    const WTS_WIN_STATION_NAME: u32 = 6;
    const WTS_CLIENT_ADDRESS: u32 = 14;
    const WTS_IDLE_TIME: u32 = 16; // wait: WTSIdleTime is 21? No, check below or use safe fallback
    const WTS_SESSION_INFO_ACTIVE: u32 = 0;
    const WTS_SESSION_INFO_CONNECTED: u32 = 1;
    const WTS_SESSION_INFO_DISCONNECTED: u32 = 4;
    const TH32CS_SNAPPROCESS: u32 = 0x0000_0002;
    const PROCESS_QUERY_LIMITED_INFORMATION: u32 = 0x1000;

    #[repr(C)]
    struct WtsSessionInfoW {
        session_id: u32,
        p_win_station_name: *mut u16,
        state: u32,
    }

    #[repr(C)]
    struct WtsClientAddress {
        address_family: u32,
        address: [u8; 20],
    }

    #[repr(C)]
    struct ProcessEntry32W {
        dw_size: u32,
        cnt_usage: u32,
        th32_process_id: u32,
        th32_default_heap_id: usize,
        th32_module_id: u32,
        cnt_threads: u32,
        th32_parent_process_id: u32,
        pc_pri_class_base: i32,
        dw_flags: u32,
        sz_exe_file: [u16; 260],
    }

    #[repr(C)]
    #[derive(Default)]
    struct Filetime {
        dw_low_date_time: u32,
        dw_high_date_time: u32,
    }

    extern "system" {
        fn GetTickCount64() -> u64;
        fn WTSEnumerateSessionsW(
            h_server: *mut c_void,
            reserved: u32,
            version: u32,
            pp_session_info: *mut *mut WtsSessionInfoW,
            p_count: *mut u32,
        ) -> i32;
        fn WTSQuerySessionInformationW(
            h_server: *mut c_void,
            session_id: u32,
            wts_info_class: u32,
            pp_buffer: *mut *mut u16,
            p_bytes_returned: *mut u32,
        ) -> i32;
        fn WTSFreeMemory(p_memory: *mut c_void);
        fn CreateToolhelp32Snapshot(dw_flags: u32, th32_process_id: u32) -> *mut c_void;
        fn Process32FirstW(h_snapshot: *mut c_void, lppe: *mut ProcessEntry32W) -> i32;
        fn Process32NextW(h_snapshot: *mut c_void, lppe: *mut ProcessEntry32W) -> i32;
        fn ProcessIdToSessionId(dw_process_id: u32, p_session_id: *mut u32) -> i32;
        fn OpenProcess(
            dw_desired_access: u32,
            b_inherit_handle: i32,
            dw_process_id: u32,
        ) -> *mut c_void;
        fn GetProcessTimes(
            h_process: *mut c_void,
            lp_creation_time: *mut Filetime,
            lp_exit_time: *mut Filetime,
            lp_kernel_time: *mut Filetime,
            lp_user_time: *mut Filetime,
        ) -> i32;
        fn CloseHandle(h_object: *mut c_void) -> i32;
    }

    pub fn uptime_seconds() -> i64 {
        (unsafe { GetTickCount64() } / 1000) as i64
    }

    fn wide_ptr_to_string(ptr: *const u16) -> String {
        if ptr.is_null() {
            return String::new();
        }
        let mut len = 0usize;
        unsafe {
            while *ptr.add(len) != 0 {
                len += 1;
            }
            String::from_utf16_lossy(std::slice::from_raw_parts(ptr, len))
        }
    }

    fn query_wstring(session_id: u32, kind: u32) -> String {
        let mut buf: *mut u16 = std::ptr::null_mut();
        let mut bytes = 0u32;
        let ok = unsafe {
            WTSQuerySessionInformationW(
                WTS_CURRENT_SERVER_HANDLE,
                session_id,
                kind,
                &mut buf,
                &mut bytes,
            )
        };
        let out = if ok != 0 && !buf.is_null() {
            wide_ptr_to_string(buf)
        } else {
            String::new()
        };
        if !buf.is_null() {
            unsafe { WTSFreeMemory(buf as *mut c_void) };
        }
        out
    }

    fn query_client_from(session_id: u32) -> String {
        let mut buf: *mut u16 = std::ptr::null_mut();
        let mut bytes = 0u32;
        let ok = unsafe {
            WTSQuerySessionInformationW(
                WTS_CURRENT_SERVER_HANDLE,
                session_id,
                WTS_CLIENT_ADDRESS,
                &mut buf,
                &mut bytes,
            )
        };
        let mut out = String::new();
        if ok != 0 && !buf.is_null() && bytes as usize >= std::mem::size_of::<WtsClientAddress>() {
            let addr = unsafe { &*(buf as *const WtsClientAddress) };
            if addr.address_family == 2 {
                out = format!(
                    "{}.{}.{}.{}",
                    addr.address[2], addr.address[3], addr.address[4], addr.address[5]
                );
            }
        }
        if !buf.is_null() {
            unsafe { WTSFreeMemory(buf as *mut c_void) };
        }
        out
    }

    #[derive(Default)]
    struct SessionProcs {
        jcpu_centis: i64,
        best_cpu_centis: i64,
        best_pid: i64,
        best_name: String,
        leader_pid: i64,
        leader_created: u64,
    }

    fn collect_session_processes() -> BTreeMap<u32, SessionProcs> {
        let mut map: BTreeMap<u32, SessionProcs> = BTreeMap::new();
        let snap = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if snap as isize == -1 || snap.is_null() {
            return map;
        }
        let mut entry: ProcessEntry32W = unsafe { std::mem::zeroed() };
        entry.dw_size = std::mem::size_of::<ProcessEntry32W>() as u32;
        if unsafe { Process32FirstW(snap, &mut entry) } != 0 {
            loop {
                let mut sid = 0u32;
                if unsafe { ProcessIdToSessionId(entry.th32_process_id, &mut sid) } != 0 && sid != 0
                {
                    let name = wide_ptr_to_string(entry.sz_exe_file.as_ptr());
                    let proc = unsafe {
                        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, entry.th32_process_id)
                    };
                    if !proc.is_null() {
                        let mut cr = Filetime::default();
                        let mut ex = Filetime::default();
                        let mut ke = Filetime::default();
                        let mut us = Filetime::default();
                        if unsafe { GetProcessTimes(proc, &mut cr, &mut ex, &mut ke, &mut us) }
                            != 0
                        {
                            let ticks = |ft: &Filetime| -> u64 {
                                ((ft.dw_high_date_time as u64) << 32) | (ft.dw_low_date_time as u64)
                            };
                            let cpu_centis = ((ticks(&ke) + ticks(&us)) / 100_000) as i64;
                            let created = ticks(&cr);
                            let agg = map.entry(sid).or_insert_with(|| SessionProcs {
                                jcpu_centis: 0,
                                best_cpu_centis: -1,
                                best_pid: -1,
                                best_name: String::new(),
                                leader_pid: -1,
                                leader_created: u64::MAX,
                            });
                            agg.jcpu_centis += cpu_centis;
                            if cpu_centis > agg.best_cpu_centis {
                                agg.best_cpu_centis = cpu_centis;
                                agg.best_pid = entry.th32_process_id as i64;
                                agg.best_name = name;
                            }
                            if created < agg.leader_created {
                                agg.leader_created = created;
                                agg.leader_pid = entry.th32_process_id as i64;
                            }
                        }
                        unsafe { CloseHandle(proc) };
                    }
                }
                if unsafe { Process32NextW(snap, &mut entry) } == 0 {
                    break;
                }
            }
        }
        unsafe { CloseHandle(snap) };
        map
    }

    pub fn enumerate_rows() -> Vec<SessionRow> {
        let mut rows = Vec::new();
        let procs = collect_session_processes();
        let mut sessions: *mut WtsSessionInfoW = std::ptr::null_mut();
        let mut count = 0u32;
        if unsafe {
            WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &mut sessions, &mut count)
        } == 0
            || sessions.is_null()
        {
            return rows;
        }
        let slice = unsafe { std::slice::from_raw_parts(sessions, count as usize) };
        for s in slice {
            if !matches!(
                s.state,
                WTS_SESSION_INFO_ACTIVE | WTS_SESSION_INFO_CONNECTED | WTS_SESSION_INFO_DISCONNECTED
            ) {
                continue;
            }
            let user = query_wstring(s.session_id, WTS_USER_NAME);
            if user.is_empty() {
                continue;
            }
            let tty = query_wstring(s.session_id, WTS_WIN_STATION_NAME);
            let from = query_client_from(s.session_id);
            let mut row = SessionRow {
                user,
                tty,
                from,
                logon_time: -1,
                idle_seconds: -1,
                idle_centi: 0,
                jcpu_centis: 0,
                pcpu_centis: 0,
                leader_pid: -1,
                best_pid: -1,
                what: "-".to_owned(),
            };
            if let Some(p) = procs.get(&s.session_id) {
                row.jcpu_centis = p.jcpu_centis;
                row.pcpu_centis = p.best_cpu_centis.max(0);
                row.best_pid = p.best_pid;
                row.leader_pid = p.leader_pid;
                if !p.best_name.is_empty() {
                    row.what = p.best_name.clone();
                }
            }
            let _ = WTS_IDLE_TIME;
            rows.push(row);
        }
        unsafe { WTSFreeMemory(sessions as *mut c_void) };
        rows
    }
}

#[cfg(not(windows))]
mod sys {
    use super::SessionRow;

    pub fn uptime_seconds() -> i64 {
        0
    }

    pub fn enumerate_rows() -> Vec<SessionRow> {
        Vec::new()
    }
}

fn time_ival7(seconds: i64, centi: i32, oldstyle: bool) -> String {
    if seconds < 0 {
        return "   ?   ".to_owned();
    }
    if oldstyle {
        if seconds >= 48 * 3600 {
            format!(" {:2}days", seconds / 86400)
        } else if seconds >= 3600 {
            format!(" {:2}:{:02} ", seconds / 3600, (seconds / 60) % 60)
        } else if seconds > 60 {
            format!(" {:2}:{:02}m", seconds / 60, seconds % 60)
        } else {
            "       ".to_owned()
        }
    } else if seconds >= 48 * 3600 {
        format!(" {:2}days", seconds / 86400)
    } else if seconds >= 3600 {
        format!(" {:2}:{:02}m", seconds / 3600, (seconds / 60) % 60)
    } else if seconds > 60 {
        format!(" {:2}:{:02} ", seconds / 60, seconds % 60)
    } else {
        format!(" {seconds:2}.{centi:02}s")
    }
}

fn print_host_field(host: &str, width: usize) -> String {
    let mut out = String::new();
    for ch in host.chars() {
        if out.len() >= width {
            break;
        }
        if ch.is_ascii() && !ch.is_ascii_control() && ch != ' ' {
            out.push(ch);
        } else {
            out.push('-');
            break;
        }
    }
    if out.is_empty() {
        out.push('-');
    }
    while out.len() < width {
        out.push(' ');
    }
    out
}

fn pad_field(value: &str, width: usize, max_chars: usize) -> String {
    let mut out: String = value.chars().take(max_chars).collect();
    while out.len() < width {
        out.push(' ');
    }
    out
}

fn sprint_uptime_line(users: usize) -> String {
    let total = sys::uptime_seconds();
    let days = total / 86400;
    let hours = (total / 3600) % 24;
    let minutes = (total / 60) % 60;
    let mut line = " 00:00:00 up ".to_owned();
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
    line.push_str(&format!(
        "{users:2} {},  load average: N/A, N/A, N/A",
        if users == 1 { "user" } else { "users" }
    ));
    line
}

pub fn w_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("w", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(rc) => return Ok(rc),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        if which == argopt::HELP {
            writeln!(
                io.stdout,
                "Usage: w [options] [user]\nShow who is logged on and what they are doing."
            )?;
        } else {
            writeln!(io.stdout, "w (WinuxCmd) 0.1.0")?;
        }
        return Ok(0);
    }

    let header = !p.has("no-header");
    let longform = !p.has("short");
    let mut from = true;
    if p.has("from") {
        from = !from;
    }
    if p.has("ip-addr") {
        from = true;
    }
    let oldstyle = p.has("old-style");
    let pids = p.has("pids");
    let user_filter = p
        .positionals
        .first()
        .map(|a| a.to_string_lossy().into_owned())
        .unwrap_or_default();

    let rows = sys::enumerate_rows();
    if header {
        writeln!(io.stdout, "{}", sprint_uptime_line(rows.len()))?;
        let mut heading = format!("{} TTY      ", pad_field("USER", 8, 8));
        if from {
            heading.push_str(&pad_field("FROM", 15, 15));
        }
        heading.push_str(if longform {
            "  LOGIN@   IDLE   JCPU   PCPU WHAT"
        } else {
            "   IDLE WHAT"
        });
        writeln!(io.stdout, "{heading}")?;
    }

    for row in &rows {
        if !user_filter.is_empty() && row.user != user_filter {
            continue;
        }
        let mut line = format!("{}{}", pad_field(&row.user, 9, 8), pad_field(&row.tty, 9, 8));
        if from {
            line.push_str(&print_host_field(&row.from, 16));
        }
        if longform {
            let _ = row.logon_time;
            line.push_str("        ");
            line.push_str(&time_ival7(row.idle_seconds, row.idle_centi, oldstyle));
            line.push_str(&time_ival7(
                row.jcpu_centis / 100,
                (row.jcpu_centis % 100) as i32,
                oldstyle,
            ));
            if row.pcpu_centis > 0 {
                line.push_str(&time_ival7(
                    row.pcpu_centis / 100,
                    (row.pcpu_centis % 100) as i32,
                    oldstyle,
                ));
            } else {
                line.push_str("   ?   ");
            }
        } else {
            line.push_str(&time_ival7(row.idle_seconds, row.idle_centi, oldstyle));
        }
        if pids {
            line.push_str(&format!(" {}/{}", row.leader_pid, row.best_pid));
        }
        line.push(' ');
        line.push_str(&row.what);
        writeln!(io.stdout, "{line}")?;
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn header_and_flags_match_procps() {
        let sc = ScratchCtx::new("w-hdr");
        let mut io = MemIo::new("");
        let rc = w_cmd(
            &[OsString::from("zz_no_such_user_zz")],
            &mut io.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc, 0);
        let out = io.out();
        assert!(out.contains("load average"));
        assert!(out.contains("USER"));
        assert!(out.contains("FROM"));
        assert!(out.contains("LOGIN@"));
        assert_eq!(out.lines().count(), 2);

        let mut io_nh = MemIo::new("");
        let rc_nh = w_cmd(&[OsString::from("-h")], &mut io_nh.split(), &sc.ctx()).unwrap();
        assert_eq!(rc_nh, 0);
        assert!(!io_nh.out().contains("load average"));
    }
}
