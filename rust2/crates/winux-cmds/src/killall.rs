//! `killall` — signal processes by executable name (#1488).
//!
//! Port of `src/commands/killall.cpp` (psmisc semantics on Windows with
//! non-Windows stub). Explicit `Io`/`Ctx`, zero globals.

use std::ffi::OsString;
use std::io::{BufRead, Write};
use std::thread::sleep;
use std::time::Duration;

use crate::argopt::{self, OptSpec};
use crate::bre;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('e'),
        long: "exact",
        takes_value: false,
    },
    OptSpec {
        short: Some('I'),
        long: "ignore-case",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "interactive",
        takes_value: false,
    },
    OptSpec {
        short: Some('l'),
        long: "list",
        takes_value: false,
    },
    OptSpec {
        short: Some('o'),
        long: "older-than",
        takes_value: true,
    },
    OptSpec {
        short: Some('q'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: Some('r'),
        long: "regexp",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "signal",
        takes_value: true,
    },
    OptSpec {
        short: Some('u'),
        long: "user",
        takes_value: true,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: Some('w'),
        long: "wait",
        takes_value: false,
    },
    OptSpec {
        short: Some('y'),
        long: "younger-than",
        takes_value: true,
    },
];

const SIGNALS: &[(&str, i32)] = &[
    ("HUP", 1),
    ("INT", 2),
    ("QUIT", 3),
    ("ILL", 4),
    ("TRAP", 5),
    ("ABRT", 6),
    ("IOT", 6),
    ("BUS", 7),
    ("FPE", 8),
    ("KILL", 9),
    ("USR1", 10),
    ("SEGV", 11),
    ("USR2", 12),
    ("PIPE", 13),
    ("ALRM", 14),
    ("TERM", 15),
    ("STKFLT", 16),
    ("CHLD", 17),
    ("CLD", 17),
    ("CONT", 18),
    ("STOP", 19),
    ("TSTP", 20),
    ("TTIN", 21),
    ("TTOU", 22),
    ("URG", 23),
    ("XCPU", 24),
    ("XFSZ", 25),
    ("VTALRM", 26),
    ("PROF", 27),
    ("WINCH", 28),
    ("IO", 29),
    ("POLL", 29),
    ("PWR", 30),
    ("SYS", 31),
];

fn signal_name_to_number(raw: &str) -> Option<i32> {
    if let Ok(n) = raw.parse::<i32>() {
        return if (0..=64).contains(&n) { Some(n) } else { None };
    }
    let upper = raw.to_ascii_uppercase();
    let trimmed = upper.strip_prefix("SIG").unwrap_or(&upper);
    if trimmed == "RTMIN" {
        return Some(34);
    }
    if trimmed == "RTMAX" {
        return Some(64);
    }
    SIGNALS
        .iter()
        .find(|(name, _)| *name == trimmed)
        .map(|(_, num)| *num)
}

fn parse_age(text: &str) -> Option<f64> {
    if text.is_empty() {
        return None;
    }
    let (num_str, mult) = match text.as_bytes().last().copied() {
        Some(b's') => (&text[..text.len() - 1], 1.0),
        Some(b'm') => (&text[..text.len() - 1], 60.0),
        Some(b'h') => (&text[..text.len() - 1], 3600.0),
        Some(b'd') => (&text[..text.len() - 1], 86400.0),
        Some(b'w') => (&text[..text.len() - 1], 604800.0),
        Some(b'M') => (&text[..text.len() - 1], 2630016.0),
        Some(b'y') => (&text[..text.len() - 1], 31557600.0),
        _ => (text, 1.0),
    };
    if num_str.is_empty() {
        return None;
    }
    let val: f64 = num_str.parse().ok()?;
    if !val.is_finite() {
        return None;
    }
    Some(val * mult)
}

fn basename_without_exe(name: &str) -> &str {
    let base = name.rsplit(['/', '\\']).next().unwrap_or(name);
    if base.len() > 4 && base[base.len() - 4..].eq_ignore_ascii_case(".exe") {
        &base[..base.len() - 4]
    } else {
        base
    }
}

struct ProcEntry {
    pid: u32,
    name: String,
    image_path: String,
    creation_100ns: Option<u64>,
    owner_name: Option<String>,
    owner_id: Option<String>,
}

#[cfg(windows)]
mod sys {
    use super::ProcEntry;
    use std::ffi::c_void;
    use std::os::windows::ffi::OsStrExt;

    const TH32CS_SNAPPROCESS: u32 = 0x0000_0002;
    const PROCESS_QUERY_LIMITED_INFORMATION: u32 = 0x1000;
    const PROCESS_TERMINATE: u32 = 0x0001;
    const SYNCHRONIZE: u32 = 0x0010_0000;
    const TOKEN_QUERY: u32 = 0x0008;
    const TOKEN_USER: u32 = 1;
    const ERROR_INVALID_PARAMETER: i32 = 87;

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

    #[repr(C)]
    struct SidAndAttributes {
        sid: *mut c_void,
        attributes: u32,
    }

    extern "system" {
        fn GetCurrentProcessId() -> u32;
        fn GetSystemTimeAsFileTime(lp_system_time_as_file_time: *mut Filetime);
        fn CreateToolhelp32Snapshot(dw_flags: u32, th32_process_id: u32) -> *mut c_void;
        fn Process32FirstW(h_snapshot: *mut c_void, lppe: *mut ProcessEntry32W) -> i32;
        fn Process32NextW(h_snapshot: *mut c_void, lppe: *mut ProcessEntry32W) -> i32;
        fn OpenProcess(
            dw_desired_access: u32,
            b_inherit_handle: i32,
            dw_process_id: u32,
        ) -> *mut c_void;
        fn QueryFullProcessImageNameW(
            h_process: *mut c_void,
            dw_flags: u32,
            lp_exe_name: *mut u16,
            lpdw_size: *mut u32,
        ) -> i32;
        fn GetProcessTimes(
            h_process: *mut c_void,
            lp_creation_time: *mut Filetime,
            lp_exit_time: *mut Filetime,
            lp_kernel_time: *mut Filetime,
            lp_user_time: *mut Filetime,
        ) -> i32;
        fn OpenProcessToken(
            process_handle: *mut c_void,
            desired_access: u32,
            token_handle: *mut *mut c_void,
        ) -> i32;
        fn GetTokenInformation(
            token_handle: *mut c_void,
            token_information_class: u32,
            token_information: *mut c_void,
            token_information_length: u32,
            return_length: *mut u32,
        ) -> i32;
        fn LookupAccountSidW(
            lp_system_name: *const u16,
            sid: *mut c_void,
            name: *mut u16,
            cch_name: *mut u32,
            referenced_domain_name: *mut u16,
            cch_referenced_domain_name: *mut u32,
            pe_use: *mut u32,
        ) -> i32;
        fn LookupAccountNameW(
            lp_system_name: *const u16,
            lp_account_name: *const u16,
            sid: *mut c_void,
            cb_sid: *mut u32,
            referenced_domain_name: *mut u16,
            cch_referenced_domain_name: *mut u32,
            pe_use: *mut u32,
        ) -> i32;
        fn TerminateProcess(h_process: *mut c_void, u_exit_code: u32) -> i32;
        fn WaitForSingleObject(h_handle: *mut c_void, dw_milliseconds: u32) -> u32;
        fn GetLastError() -> u32;
        fn CloseHandle(h_object: *mut c_void) -> i32;
    }

    pub fn current_pid() -> u32 {
        unsafe { GetCurrentProcessId() }
    }

    pub fn now_filetime_100ns() -> u64 {
        let mut ft = Filetime::default();
        unsafe { GetSystemTimeAsFileTime(&mut ft) };
        ((ft.dw_high_date_time as u64) << 32) | (ft.dw_low_date_time as u64)
    }

    pub fn account_exists(name: &str) -> bool {
        let mut w: Vec<u16> = std::ffi::OsStr::new(name).encode_wide().collect();
        w.push(0);
        let mut cb_sid = 0u32;
        let mut cch_dom = 0u32;
        let mut use_ = 0u32;
        unsafe {
            LookupAccountNameW(
                std::ptr::null(),
                w.as_ptr(),
                std::ptr::null_mut(),
                &mut cb_sid,
                std::ptr::null_mut(),
                &mut cch_dom,
                &mut use_,
            );
        }
        cb_sid > 0
    }

    fn query_owner(proc: *mut c_void) -> (Option<String>, Option<String>) {
        let mut token: *mut c_void = std::ptr::null_mut();
        if unsafe { OpenProcessToken(proc, TOKEN_QUERY, &mut token) } == 0 || token.is_null() {
            return (None, None);
        }
        let mut needed = 0u32;
        unsafe { GetTokenInformation(token, TOKEN_USER, std::ptr::null_mut(), 0, &mut needed) };
        if needed == 0 {
            unsafe { CloseHandle(token) };
            return (None, None);
        }
        let mut buf = vec![0u8; needed as usize];
        let ok = unsafe {
            GetTokenInformation(
                token,
                TOKEN_USER,
                buf.as_mut_ptr() as *mut c_void,
                needed,
                &mut needed,
            )
        };
        unsafe { CloseHandle(token) };
        if ok == 0 {
            return (None, None);
        }
        let sid = unsafe { (*(buf.as_ptr() as *const SidAndAttributes)).sid };
        let mut nsz = 0u32;
        let mut dsz = 0u32;
        let mut use_ = 0u32;
        unsafe {
            LookupAccountSidW(
                std::ptr::null(),
                sid,
                std::ptr::null_mut(),
                &mut nsz,
                std::ptr::null_mut(),
                &mut dsz,
                &mut use_,
            );
        }
        if nsz == 0 {
            return (None, None);
        }
        let mut name = vec![0u16; nsz as usize];
        let mut dom = vec![0u16; dsz.max(1) as usize];
        let ok = unsafe {
            LookupAccountSidW(
                std::ptr::null(),
                sid,
                name.as_mut_ptr(),
                &mut nsz,
                dom.as_mut_ptr(),
                &mut dsz,
                &mut use_,
            )
        };
        if ok == 0 {
            return (None, None);
        }
        let len = name.iter().take_while(|&&c| c != 0).count();
        (Some(String::from_utf16_lossy(&name[..len])), None)
    }

    pub fn enumerate_processes() -> Vec<ProcEntry> {
        let mut out = Vec::new();
        let snap = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if snap as isize == -1 || snap.is_null() {
            return out;
        }
        let mut entry: ProcessEntry32W = unsafe { std::mem::zeroed() };
        entry.dw_size = std::mem::size_of::<ProcessEntry32W>() as u32;
        if unsafe { Process32FirstW(snap, &mut entry) } != 0 {
            loop {
                let len = entry
                    .sz_exe_file
                    .iter()
                    .take_while(|&&c| c != 0)
                    .count();
                let name = String::from_utf16_lossy(&entry.sz_exe_file[..len]);
                let mut image_path = String::new();
                let mut creation_100ns = None;
                let mut owner_name = None;
                let mut owner_id = None;
                let proc = unsafe {
                    OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, entry.th32_process_id)
                };
                if !proc.is_null() {
                    let mut pbuf = vec![0u16; 1024];
                    let mut plen = pbuf.len() as u32;
                    if unsafe {
                        QueryFullProcessImageNameW(proc, 0, pbuf.as_mut_ptr(), &mut plen)
                    } != 0
                    {
                        image_path = String::from_utf16_lossy(&pbuf[..plen as usize]);
                    }
                    let mut cr = Filetime::default();
                    let mut ex = Filetime::default();
                    let mut ke = Filetime::default();
                    let mut us = Filetime::default();
                    if unsafe { GetProcessTimes(proc, &mut cr, &mut ex, &mut ke, &mut us) } != 0 {
                        creation_100ns =
                            Some(((cr.dw_high_date_time as u64) << 32) | (cr.dw_low_date_time as u64));
                    }
                    let (on, oi) = query_owner(proc);
                    owner_name = on;
                    owner_id = oi;
                    unsafe { CloseHandle(proc) };
                }
                out.push(ProcEntry {
                    pid: entry.th32_process_id,
                    name,
                    image_path,
                    creation_100ns,
                    owner_name,
                    owner_id,
                });
                if unsafe { Process32NextW(snap, &mut entry) } == 0 {
                    break;
                }
            }
        }
        unsafe { CloseHandle(snap) };
        out
    }

    pub fn check_process(pid: u32) -> bool {
        let h = unsafe { OpenProcess(SYNCHRONIZE, 0, pid) };
        if h.is_null() {
            return false;
        }
        let alive = unsafe { WaitForSingleObject(h, 0) } == 0x0000_0102;
        unsafe { CloseHandle(h) };
        alive
    }

    pub fn terminate_process(pid: u32) -> Result<(), (i32, String)> {
        let h = unsafe { OpenProcess(PROCESS_TERMINATE, 0, pid) };
        if h.is_null() {
            let err = unsafe { GetLastError() } as i32;
            return Err((
                err,
                std::io::Error::from_raw_os_error(err).to_string(),
            ));
        }
        let ok = unsafe { TerminateProcess(h, 1) };
        let err = if ok == 0 {
            unsafe { GetLastError() } as i32
        } else {
            0
        };
        unsafe { CloseHandle(h) };
        if ok != 0 || err == ERROR_INVALID_PARAMETER {
            Ok(())
        } else {
            Err((err, std::io::Error::from_raw_os_error(err).to_string()))
        }
    }
}

#[cfg(not(windows))]
mod sys {
    use super::ProcEntry;

    pub fn current_pid() -> u32 {
        std::process::id()
    }
    pub fn now_filetime_100ns() -> u64 {
        0
    }
    pub fn account_exists(_name: &str) -> bool {
        false
    }
    pub fn enumerate_processes() -> Vec<ProcEntry> {
        Vec::new()
    }
    pub fn check_process(_pid: u32) -> bool {
        false
    }
    pub fn terminate_process(_pid: u32) -> Result<(), (i32, String)> {
        Err((1, "unsupported".to_owned()))
    }
}

pub fn killall_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // Pre-scan for -NUM / -SIGNAL tokens before passing the rest to argopt::parse
    let mut cleaned: Vec<OsString> = Vec::with_capacity(args.len());
    let mut pre_signals: Vec<i32> = Vec::new();
    let mut after_dd = false;
    for a in args {
        let s = a.to_string_lossy();
        if after_dd {
            cleaned.push(a.clone());
            continue;
        }
        if s == "--" {
            after_dd = true;
            cleaned.push(a.clone());
            continue;
        }
        if s.starts_with('-') && s.len() > 1 && !s.starts_with("--") {
            let rest = &s[1..];
            if rest.bytes().all(|b| b.is_ascii_digit()) {
                if let Some(n) = signal_name_to_number(rest) {
                    pre_signals.push(n);
                    continue;
                } else {
                    writeln!(io.stderr, "killall: invalid signal: '{rest}'")?;
                    writeln!(io.stderr, "Try 'killall --help' for more information.")?;
                    return Ok(1);
                }
            }
            if rest.len() > 1 {
                if let Some(n) = signal_name_to_number(rest) {
                    pre_signals.push(n);
                    continue;
                }
            }
        }
        cleaned.push(a.clone());
    }

    if pre_signals.len() > 1 {
        writeln!(io.stderr, "killall: only one signal may be specified")?;
        writeln!(io.stderr, "Try 'killall --help' for more information.")?;
        return Ok(1);
    }

    let p = match argopt::parse("killall", &cleaned, OPTS, io, ctx, 1) {
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
                "Usage: killall [OPTION]... NAME...\nSignal processes by executable name."
            )?;
        } else {
            writeln!(io.stdout, "killall (WinuxCmd) 0.2.0")?;
        }
        return Ok(0);
    }

    if p.has("list") {
        let mut col = 0usize;
        for (name, _) in SIGNALS {
            if col > 0 {
                write!(io.stdout, " ")?;
                col += 1;
            }
            write!(io.stdout, "{name}")?;
            col += name.len();
            if col >= 72 {
                writeln!(io.stdout)?;
                col = 0;
            }
        }
        for extra in ["RTMIN", "RTMAX"] {
            if col > 0 {
                write!(io.stdout, " ")?;
                col += 1;
            }
            write!(io.stdout, "{extra}")?;
            col += extra.len();
        }
        if col > 0 {
            writeln!(io.stdout)?;
        }
        return Ok(0);
    }

    let signal = if let Some(sig_str) = p.value_of("signal") {
        match signal_name_to_number(sig_str) {
            Some(n) => n,
            None => {
                writeln!(io.stderr, "killall: invalid signal: '{sig_str}'")?;
                writeln!(io.stderr, "Try 'killall --help' for more information.")?;
                return Ok(1);
            }
        }
    } else if let Some(&n) = pre_signals.first() {
        n
    } else {
        15
    };

    let user_spec = p.value_of("user");
    if let Some(u) = user_spec {
        if !u.bytes().all(|b| b.is_ascii_digit()) && !sys::account_exists(u) {
            writeln!(io.stderr, "killall: invalid user: '{u}'")?;
            writeln!(io.stderr, "Try 'killall --help' for more information.")?;
            return Ok(1);
        }
    }

    let older_than = if let Some(raw) = p.value_of("older-than") {
        match parse_age(raw) {
            Some(v) => Some(v),
            None => {
                writeln!(io.stderr, "killall: invalid time specification: '{raw}'")?;
                return Ok(1);
            }
        }
    } else {
        None
    };

    let younger_than = if let Some(raw) = p.value_of("younger-than") {
        match parse_age(raw) {
            Some(v) => Some(v),
            None => {
                writeln!(io.stderr, "killall: invalid time specification: '{raw}'")?;
                return Ok(1);
            }
        }
    } else {
        None
    };

    let names: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();
    if names.is_empty() && user_spec.is_none() {
        writeln!(io.stderr, "killall: missing process name")?;
        writeln!(io.stderr, "Try 'killall --help' for more information.")?;
        return Ok(1);
    }

    let ignore_case = p.has("ignore-case");
    let regexp = p.has("regexp");
    let interactive = p.has("interactive");
    let quiet = p.has("quiet");
    let verbose = p.has("verbose");
    let wait_flag = p.has("wait");

    let mut regexes = Vec::new();
    if regexp {
        for n in &names {
            let pat = if ignore_case {
                n.to_ascii_lowercase()
            } else {
                n.clone()
            };
            match bre::Regex::compile(&pat) {
                Ok(r) => regexes.push(r),
                Err(_) => {
                    writeln!(io.stderr, "killall: invalid regular expression: '{n}'")?;
                    return Ok(1);
                }
            }
        }
    }

    let processes = sys::enumerate_processes();
    let self_pid = sys::current_pid();
    let now_ft = sys::now_filetime_100ns();
    let by_user_only = names.is_empty();

    let mut signaled_pids = Vec::new();
    let mut all_satisfied = true;
    let mut any_signaled = false;
    let rounds = if by_user_only { 1 } else { names.len() };

    for round in 0..rounds {
        let mut found = false;
        let mut signaled = false;
        for proc in &processes {
            if proc.pid == 0 || proc.pid == 4 || proc.pid == self_pid {
                continue;
            }
            if !by_user_only {
                let target = &names[round];
                let matched = if target.contains(['/', '\\']) {
                    !proc.image_path.is_empty()
                        && proc.image_path.replace('/', "\\").eq_ignore_ascii_case(&target.replace('/', "\\"))
                } else {
                    let base = basename_without_exe(&proc.name);
                    if regexp {
                        if ignore_case {
                            regexes[round].matches_anywhere(base.to_ascii_lowercase().as_bytes())
                        } else {
                            regexes[round].matches_anywhere(base.as_bytes())
                        }
                    } else {
                        let tbase = basename_without_exe(target);
                        if ignore_case {
                            base.eq_ignore_ascii_case(tbase)
                        } else {
                            base == tbase
                        }
                    }
                };
                if !matched {
                    continue;
                }
            }

            if let Some(u) = user_spec {
                let ok_u = proc
                    .owner_name
                    .as_deref()
                    .map(|n| n.eq_ignore_ascii_case(u))
                    .unwrap_or(false)
                    || proc.owner_id.as_deref() == Some(u);
                if !ok_u {
                    continue;
                }
            }
            if older_than.is_some() || younger_than.is_some() {
                let Some(cr) = proc.creation_100ns else {
                    continue;
                };
                let age = if now_ft > cr {
                    (now_ft - cr) as f64 / 1.0e7
                } else {
                    0.0
                };
                if let Some(ot) = older_than {
                    if !(age > ot) {
                        continue;
                    }
                }
                if let Some(yt) = younger_than {
                    if !(age < yt) {
                        continue;
                    }
                }
            }

            found = true;
            if interactive {
                write!(io.stderr, "Kill {}({})? (y/N) ", proc.name, proc.pid)?;
                let mut ans = String::new();
                let mut reader = std::io::BufReader::new(&mut *io.stdin);
                if reader.read_line(&mut ans).unwrap_or(0) == 0
                    || !ans.starts_with(['y', 'Y'])
                {
                    continue;
                }
            }

            let res = if signal == 0 {
                if sys::check_process(proc.pid) {
                    Ok(())
                } else {
                    Err((1, "No such process".to_owned()))
                }
            } else {
                sys::terminate_process(proc.pid)
            };

            match res {
                Ok(()) => {
                    signaled = true;
                    any_signaled = true;
                    signaled_pids.push(proc.pid);
                    if verbose && signal != 0 {
                        writeln!(io.stdout, "{} {}", proc.pid, proc.name)?;
                    }
                }
                Err((_, msg)) => {
                    if !quiet {
                        writeln!(io.stderr, "killall: {}: {msg}", proc.pid)?;
                    }
                }
            }
        }
        if !found {
            if !by_user_only && !quiet {
                writeln!(io.stderr, "{}: no process found", names[round])?;
            }
            all_satisfied = false;
        } else if !signaled {
            all_satisfied = false;
        }
    }

    if wait_flag && !signaled_pids.is_empty() {
        loop {
            if !signaled_pids.iter().any(|&pid| sys::check_process(pid)) {
                break;
            }
            sleep(Duration::from_secs(1));
        }
    }

    Ok(if all_satisfied && any_signaled { 0 } else { 1 })
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn list_signals_and_quiet_no_match() {
        let sc = ScratchCtx::new("killall-test");
        let mut io = MemIo::new("");
        let rc = killall_cmd(&[OsString::from("-l")], &mut io.split(), &sc.ctx()).unwrap();
        assert_eq!(rc, 0);
        assert!(io.out().contains("TERM"));
        assert!(io.out().contains("KILL"));

        let mut io_q = MemIo::new("");
        let rc_q = killall_cmd(
            &[
                OsString::from("-q"),
                OsString::from("WINUXCMD_KILLALL_NO_SUCH_PROCESS"),
            ],
            &mut io_q.split(),
            &sc.ctx(),
        )
        .unwrap();
        assert_eq!(rc_q, 1);
        assert!(io_q.err().is_empty());
    }

    #[test]
    fn parse_age_units() {
        assert_eq!(parse_age("10s"), Some(10.0));
        assert_eq!(parse_age("2m"), Some(120.0));
        assert_eq!(parse_age("1h"), Some(3600.0));
    }
}
