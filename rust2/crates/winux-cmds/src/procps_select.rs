//! Shared process-selection engine for `pgrep` and `pkill` (procps 4.0.4 semantics, #1488).

use std::collections::{HashMap, HashSet};
use std::io::Read;
use std::path::Path;

use crate::bre;
use crate::fsutil;
use winux_core::{Ctx, Io};

pub const SIGNALS: &[(&str, i32)] = &[
    ("HUP", 1),
    ("INT", 2),
    ("QUIT", 3),
    ("ILL", 4),
    ("TRAP", 5),
    ("ABRT", 6),
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
    ("PWR", 30),
    ("SYS", 31),
];

pub fn signal_name_to_number(name: &str) -> Option<i32> {
    if name.is_empty() {
        return None;
    }
    if name.bytes().all(|b| b.is_ascii_digit()) {
        let v: i32 = name.parse().ok()?;
        return if (0..=64).contains(&v) { Some(v) } else { None };
    }
    let upper = name.to_ascii_uppercase();
    let key = upper.strip_prefix("SIG").unwrap_or(&upper);
    match key {
        "EXIT" | "NULL" => return Some(0),
        "CLD" => return Some(17),
        "IOT" => return Some(6),
        "POLL" | "LOST" => return Some(29),
        "UNUSED" => return Some(31),
        "RTMIN" => return Some(34),
        "RTMAX" => return Some(64),
        _ => {}
    }
    if let Some(rest) = key.strip_prefix("RTMIN") {
        if let Some(d) = rest.strip_prefix('+') {
            if let Ok(off) = d.parse::<i32>() {
                let v = 34 + off;
                return if (34..=64).contains(&v) { Some(v) } else { None };
            }
        } else if let Some(d) = rest.strip_prefix('-') {
            if let Ok(off) = d.parse::<i32>() {
                let v = 34 - off;
                return if (34..=64).contains(&v) { Some(v) } else { None };
            }
        }
        return None;
    }
    if let Some(rest) = key.strip_prefix("RTMAX") {
        if let Some(d) = rest.strip_prefix('+') {
            if let Ok(off) = d.parse::<i32>() {
                let v = 64 + off;
                return if (34..=64).contains(&v) { Some(v) } else { None };
            }
        } else if let Some(d) = rest.strip_prefix('-') {
            if let Ok(off) = d.parse::<i32>() {
                let v = 64 - off;
                return if (34..=64).contains(&v) { Some(v) } else { None };
            }
        }
        return None;
    }
    SIGNALS
        .iter()
        .find(|(n, _)| *n == key)
        .map(|(_, num)| *num)
}

pub fn split_list(value: &str) -> Option<Vec<String>> {
    let mut out = Vec::new();
    for item in value.split(',') {
        if item.is_empty() {
            return None;
        }
        out.push(item.to_owned());
    }
    Some(out)
}

pub fn parse_u32_list(value: &str, out: &mut Vec<u32>) -> bool {
    let Some(items) = split_list(value) else {
        return false;
    };
    for item in items {
        if item.is_empty() || !item.bytes().all(|b| b.is_ascii_digit()) {
            return false;
        }
        let Ok(v) = item.parse::<u32>() else {
            return false;
        };
        out.push(v);
    }
    true
}

pub fn validate_account_specs(specs: &[String]) -> Result<(), String> {
    for spec in specs {
        if spec.bytes().all(|b| b.is_ascii_digit()) {
            continue;
        }
        if !sys::account_exists(spec) {
            return Err(spec.clone());
        }
    }
    Ok(())
}

pub fn basename_without_exe(name: &str) -> &str {
    let base = name.rsplit(['/', '\\']).next().unwrap_or(name);
    if base.len() > 4 && base[base.len() - 4..].eq_ignore_ascii_case(".exe") {
        &base[..base.len() - 4]
    } else {
        base
    }
}

pub fn is_long_match(pattern: &str) -> bool {
    pattern.len() > 15 && !pattern.contains('|') && !pattern.contains('[')
}

pub fn read_pidfile(path: &str, require_lock: bool, io: &mut Io, ctx: &Ctx) -> Option<u32> {
    let content = if path == "-" {
        let mut buf = [0u8; 4096];
        let n = io.stdin.read(&mut buf).ok()?;
        String::from_utf8_lossy(&buf[..n]).into_owned()
    } else {
        let resolved = fsutil::resolve(ctx, Path::new(path));
        if require_lock && !sys::is_file_locked(&resolved) {
            return None;
        }
        let data = std::fs::read(&resolved).ok()?;
        if data.is_empty() {
            return None;
        }
        let take = data.len().min(11);
        String::from_utf8_lossy(&data[..take]).into_owned()
    };
    let trimmed = content.trim_start();
    let digits_len = trimmed.bytes().take_while(|b| b.is_ascii_digit()).count();
    if digits_len == 0 {
        return None;
    }
    let pid: u32 = trimmed[..digits_len].parse().ok()?;
    if pid < 1 {
        return None;
    }
    let rest = &trimmed[digits_len..];
    if !rest.is_empty() && !matches!(rest.as_bytes()[0], b' ' | b'\t' | b'\n' | b'\r' | 0x0b | 0x0c) {
        return None;
    }
    Some(pid)
}

pub struct Criteria {
    pub has_pattern: bool,
    pub pattern_text: String,
    pub pattern: Option<bre::Regex>,
    pub exact: bool,
    pub ignore_case: bool,
    pub full: bool,
    pub euid_specs: Vec<String>,
    pub ruid_specs: Vec<String>,
    pub group_specs: Vec<String>,
    pub ppids: Vec<u32>,
    pub sessions: Vec<u32>,
    pub pidfile_pid: Option<u32>,
    pub newest: bool,
    pub oldest: bool,
    pub older_than: Option<i64>,
    pub ignore_ancestors: bool,
    pub negate: bool,
}

impl Criteria {
    pub fn has_criteria(&self) -> bool {
        self.has_pattern
            || !self.euid_specs.is_empty()
            || !self.ruid_specs.is_empty()
            || !self.group_specs.is_empty()
            || !self.ppids.is_empty()
            || !self.sessions.is_empty()
            || self.pidfile_pid.is_some()
            || self.newest
            || self.oldest
            || self.older_than.is_some()
    }
}

pub fn compile_pattern(pattern: &str, exact: bool, ignore_case: bool) -> Result<bre::Regex, String> {
    let mut pat = if ignore_case {
        pattern.to_ascii_lowercase()
    } else {
        pattern.to_owned()
    };
    if exact {
        pat = format!("^{pat}$");
    }
    bre::Regex::compile(&pat)
}

#[derive(Clone)]
pub struct ProcInfo {
    pub pid: u32,
    pub ppid: u32,
    pub name: String,
    pub command_line: String,
}

pub struct Selected {
    pub proc_info: ProcInfo,
    pub create_filetime: u64,
}

pub fn select_processes(crit: &Criteria, processes: &[ProcInfo], now_ft: u64) -> Vec<Selected> {
    let self_pid = sys::current_pid();
    let mut ancestors = HashSet::new();
    if crit.ignore_ancestors {
        let mut parent_of = HashMap::new();
        for p in processes {
            parent_of.insert(p.pid, p.ppid);
        }
        let mut cur = parent_of.get(&self_pid).copied().unwrap_or(0);
        for _ in 0..4096 {
            if cur == 0 || !ancestors.insert(cur) {
                break;
            }
            cur = parent_of.get(&cur).copied().unwrap_or(0);
        }
    }

    let mut matched = Vec::new();
    for proc in processes {
        if proc.pid == self_pid || proc.pid == 0 {
            continue;
        }
        if ancestors.contains(&proc.pid) {
            continue;
        }
        let mut ok = true;
        if let Some(pid) = crit.pidfile_pid {
            ok = proc.pid == pid;
        }
        if ok && !crit.ppids.is_empty() {
            ok = crit.ppids.contains(&proc.ppid);
        }
        if ok && !crit.sessions.is_empty() {
            ok = sys::process_session_id(proc.pid)
                .map(|s| crit.sessions.contains(&s))
                .unwrap_or(false);
        }
        if ok && (!crit.euid_specs.is_empty() || !crit.ruid_specs.is_empty()) {
            let owner = sys::process_owner(proc.pid);
            ok = owner
                .map(|o| {
                    (crit.euid_specs.is_empty()
                        || crit.euid_specs.iter().any(|s| s.eq_ignore_ascii_case(&o)))
                        && (crit.ruid_specs.is_empty()
                            || crit.ruid_specs.iter().any(|s| s.eq_ignore_ascii_case(&o)))
                })
                .unwrap_or(false);
        }
        if ok && !crit.group_specs.is_empty() {
            ok = sys::process_group(proc.pid)
                .map(|g| crit.group_specs.iter().any(|s| s.eq_ignore_ascii_case(&g)))
                .unwrap_or(false);
        }

        let mut create_ft_cache: Option<u64> = None;
        let mut get_creation = || -> u64 {
            if let Some(v) = create_ft_cache {
                v
            } else {
                let v = sys::process_creation_filetime(proc.pid).unwrap_or(0);
                create_ft_cache = Some(v);
                v
            }
        };

        if ok {
            if let Some(older) = crit.older_than {
                let cr = get_creation();
                let elapsed = if now_ft > cr {
                    ((now_ft - cr) as f64 / 1.0e7) as i64
                } else {
                    0
                };
                ok = elapsed >= older;
            }
        }
        if ok && crit.has_pattern {
            let hay_raw = if crit.full {
                proc.command_line.as_str()
            } else {
                basename_without_exe(&proc.name)
            };
            let hay = if crit.ignore_case {
                hay_raw.to_ascii_lowercase()
            } else {
                hay_raw.to_owned()
            };
            ok = crit
                .pattern
                .as_ref()
                .map(|re| re.matches_anywhere(hay.as_bytes()))
                .unwrap_or(false);
        }
        if crit.negate {
            ok = !ok;
        }
        if !ok {
            continue;
        }
        let cr = if crit.newest || crit.oldest {
            get_creation()
        } else {
            0
        };
        matched.push(Selected {
            proc_info: proc.clone(),
            create_filetime: cr,
        });
    }

    if (crit.newest || crit.oldest) && matched.len() > 1 {
        let mut best_idx = 0usize;
        for (i, cand) in matched.iter().enumerate().skip(1) {
            let best = &matched[best_idx];
            if crit.newest {
                if cand.create_filetime > best.create_filetime
                    || (cand.create_filetime == best.create_filetime
                        && cand.proc_info.pid > best.proc_info.pid)
                {
                    best_idx = i;
                }
            } else if cand.create_filetime < best.create_filetime
                || (cand.create_filetime == best.create_filetime
                    && cand.proc_info.pid < best.proc_info.pid)
            {
                best_idx = i;
            }
        }
        let only = matched.swap_remove(best_idx);
        matched.clear();
        matched.push(only);
    }
    matched
}

#[cfg(windows)]
pub mod sys {
    use super::ProcInfo;
    use std::ffi::c_void;
    use std::os::windows::ffi::OsStrExt;
    use std::path::Path;

    const TH32CS_SNAPPROCESS: u32 = 0x0000_0002;
    const TH32CS_SNAPTHREAD: u32 = 0x0000_0004;
    const PROCESS_QUERY_LIMITED_INFORMATION: u32 = 0x1000;
    const PROCESS_TERMINATE: u32 = 0x0001;
    const SYNCHRONIZE: u32 = 0x0010_0000;
    const TOKEN_QUERY: u32 = 0x0008;
    const TOKEN_USER: u32 = 1;
    const TOKEN_PRIMARY_GROUP: u32 = 5;
    const PROCESS_COMMAND_LINE_INFO: u32 = 60;
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
    struct ThreadEntry32 {
        dw_size: u32,
        cnt_usage: u32,
        th32_thread_id: u32,
        th32_owner_process_id: u32,
        tp_base_pri: i32,
        tp_delta_pri: i32,
        dw_flags: u32,
    }

    #[repr(C)]
    #[derive(Default)]
    struct Filetime {
        dw_low_date_time: u32,
        dw_high_date_time: u32,
    }

    #[repr(C)]
    struct UnicodeString {
        length: u16,
        maximum_length: u16,
        buffer: *const u16,
    }

    extern "system" {
        fn GetCurrentProcessId() -> u32;
        fn GetSystemTimeAsFileTime(lp: *mut Filetime);
        fn ProcessIdToSessionId(pid: u32, sid: *mut u32) -> i32;
        fn CreateToolhelp32Snapshot(flags: u32, pid: u32) -> *mut c_void;
        fn Process32FirstW(snap: *mut c_void, pe: *mut ProcessEntry32W) -> i32;
        fn Process32NextW(snap: *mut c_void, pe: *mut ProcessEntry32W) -> i32;
        fn Thread32First(snap: *mut c_void, te: *mut ThreadEntry32) -> i32;
        fn Thread32Next(snap: *mut c_void, te: *mut ThreadEntry32) -> i32;
        fn OpenProcess(access: u32, inherit: i32, pid: u32) -> *mut c_void;
        fn GetProcessTimes(
            h: *mut c_void,
            cr: *mut Filetime,
            ex: *mut Filetime,
            ke: *mut Filetime,
            us: *mut Filetime,
        ) -> i32;
        fn OpenProcessToken(proc_h: *mut c_void, access: u32, tok: *mut *mut c_void) -> i32;
        fn GetTokenInformation(
            tok: *mut c_void,
            class: u32,
            info: *mut c_void,
            len: u32,
            ret: *mut u32,
        ) -> i32;
        fn LookupAccountSidW(
            sys: *const u16,
            sid: *mut c_void,
            name: *mut u16,
            cch_name: *mut u32,
            dom: *mut u16,
            cch_dom: *mut u32,
            pe_use: *mut u32,
        ) -> i32;
        fn LookupAccountNameW(
            sys: *const u16,
            acct: *const u16,
            sid: *mut c_void,
            cb_sid: *mut u32,
            dom: *mut u16,
            cch_dom: *mut u32,
            pe_use: *mut u32,
        ) -> i32;
        fn TerminateProcess(h: *mut c_void, code: u32) -> i32;
        fn WaitForSingleObject(h: *mut c_void, ms: u32) -> u32;
        fn GetLastError() -> u32;
        fn CloseHandle(h: *mut c_void) -> i32;
        fn NtQueryInformationProcess(
            proc_h: *mut c_void,
            info_class: u32,
            info: *mut c_void,
            len: u32,
            ret_len: *mut u32,
        ) -> i32;
        fn CreateFileW(
            name: *const u16,
            access: u32,
            share: u32,
            sec: *mut c_void,
            disp: u32,
            flags: u32,
            tmpl: *mut c_void,
        ) -> *mut c_void;
        fn LockFileEx(
            h: *mut c_void,
            flags: u32,
            res: u32,
            lo: u32,
            hi: u32,
            ov: *mut [u8; 32],
        ) -> i32;
        fn UnlockFileEx(
            h: *mut c_void,
            res: u32,
            lo: u32,
            hi: u32,
            ov: *mut [u8; 32],
        ) -> i32;
    }

    pub fn current_pid() -> u32 {
        unsafe { GetCurrentProcessId() }
    }

    pub fn own_session_id() -> Option<u32> {
        let mut s = 0u32;
        if unsafe { ProcessIdToSessionId(GetCurrentProcessId(), &mut s) } != 0 {
            Some(s)
        } else {
            None
        }
    }

    pub fn now_filetime() -> u64 {
        let mut ft = Filetime::default();
        unsafe { GetSystemTimeAsFileTime(&mut ft) };
        ((ft.dw_high_date_time as u64) << 32) | (ft.dw_low_date_time as u64)
    }

    pub fn account_exists(name: &str) -> bool {
        let mut w: Vec<u16> = std::ffi::OsStr::new(name).encode_wide().collect();
        w.push(0);
        let mut cb = 0u32;
        let mut cd = 0u32;
        let mut u = 0u32;
        unsafe {
            LookupAccountNameW(
                std::ptr::null(),
                w.as_ptr(),
                std::ptr::null_mut(),
                &mut cb,
                std::ptr::null_mut(),
                &mut cd,
                &mut u,
            );
        }
        cb > 0
    }

    pub fn is_file_locked(path: &Path) -> bool {
        let mut w: Vec<u16> = path.as_os_str().encode_wide().collect();
        w.push(0);
        let h = unsafe {
            CreateFileW(
                w.as_ptr(),
                0x8000_0000,
                7,
                std::ptr::null_mut(),
                3,
                0x80,
                std::ptr::null_mut(),
            )
        };
        if h as isize == -1 || h.is_null() {
            let err = unsafe { GetLastError() };
            return err == 32 || err == 33;
        }
        let mut ov = [0u8; 32];
        let locked = if unsafe { LockFileEx(h, 3, 0, u32::MAX, u32::MAX, &mut ov) } != 0 {
            unsafe { UnlockFileEx(h, 0, u32::MAX, u32::MAX, &mut ov) };
            false
        } else {
            let err = unsafe { GetLastError() };
            err == 33 || err == 32
        };
        unsafe { CloseHandle(h) };
        locked
    }

    pub fn process_session_id(pid: u32) -> Option<u32> {
        let mut s = 0u32;
        if unsafe { ProcessIdToSessionId(pid, &mut s) } != 0 {
            Some(s)
        } else {
            None
        }
    }

    pub fn process_creation_filetime(pid: u32) -> Option<u64> {
        let h = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid) };
        if h.is_null() {
            return None;
        }
        let mut cr = Filetime::default();
        let mut ex = Filetime::default();
        let mut ke = Filetime::default();
        let mut us = Filetime::default();
        let ok = unsafe { GetProcessTimes(h, &mut cr, &mut ex, &mut ke, &mut us) };
        unsafe { CloseHandle(h) };
        if ok != 0 {
            Some(((cr.dw_high_date_time as u64) << 32) | (cr.dw_low_date_time as u64))
        } else {
            None
        }
    }

    fn query_token_sid_name(pid: u32, class: u32) -> Option<String> {
        let h = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid) };
        if h.is_null() {
            return None;
        }
        let mut tok: *mut c_void = std::ptr::null_mut();
        let ok = unsafe { OpenProcessToken(h, TOKEN_QUERY, &mut tok) };
        unsafe { CloseHandle(h) };
        if ok == 0 || tok.is_null() {
            return None;
        }
        let mut need = 0u32;
        unsafe { GetTokenInformation(tok, class, std::ptr::null_mut(), 0, &mut need) };
        if need == 0 {
            unsafe { CloseHandle(tok) };
            return None;
        }
        let mut buf = vec![0u8; need as usize];
        let ok = unsafe {
            GetTokenInformation(
                tok,
                class,
                buf.as_mut_ptr() as *mut c_void,
                need,
                &mut need,
            )
        };
        unsafe { CloseHandle(tok) };
        if ok == 0 {
            return None;
        }
        let sid = unsafe { *(buf.as_ptr() as *const *mut c_void) };
        let mut ns = 0u32;
        let mut ds = 0u32;
        let mut u = 0u32;
        unsafe {
            LookupAccountSidW(
                std::ptr::null(),
                sid,
                std::ptr::null_mut(),
                &mut ns,
                std::ptr::null_mut(),
                &mut ds,
                &mut u,
            );
        }
        if ns == 0 {
            return None;
        }
        let mut name = vec![0u16; ns as usize];
        let mut dom = vec![0u16; ds.max(1) as usize];
        if unsafe {
            LookupAccountSidW(
                std::ptr::null(),
                sid,
                name.as_mut_ptr(),
                &mut ns,
                dom.as_mut_ptr(),
                &mut ds,
                &mut u,
            )
        } == 0
        {
            return None;
        }
        let len = name.iter().take_while(|&&c| c != 0).count();
        Some(String::from_utf16_lossy(&name[..len]))
    }

    pub fn process_owner(pid: u32) -> Option<String> {
        query_token_sid_name(pid, TOKEN_USER)
    }

    pub fn process_group(pid: u32) -> Option<String> {
        query_token_sid_name(pid, TOKEN_PRIMARY_GROUP)
    }

    fn query_cmdline(pid: u32) -> String {
        let h = unsafe { OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, 0, pid) };
        if h.is_null() {
            return String::new();
        }
        let mut need = 0u32;
        unsafe {
            NtQueryInformationProcess(
                h,
                PROCESS_COMMAND_LINE_INFO,
                std::ptr::null_mut(),
                0,
                &mut need,
            );
        }
        let mut out = String::new();
        if need > 0 {
            let mut buf = vec![0u8; need as usize];
            if unsafe {
                NtQueryInformationProcess(
                    h,
                    PROCESS_COMMAND_LINE_INFO,
                    buf.as_mut_ptr() as *mut c_void,
                    need,
                    &mut need,
                )
            } == 0
            {
                let us = unsafe { &*(buf.as_ptr() as *const UnicodeString) };
                if !us.buffer.is_null() && us.length > 0 {
                    let slice = unsafe {
                        std::slice::from_raw_parts(us.buffer, (us.length / 2) as usize)
                    };
                    out = String::from_utf16_lossy(slice);
                }
            }
        }
        unsafe { CloseHandle(h) };
        out
    }

    pub fn enumerate_processes(need_cmdline: bool) -> Vec<ProcInfo> {
        let mut out = Vec::new();
        let snap = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0) };
        if snap as isize == -1 || snap.is_null() {
            return out;
        }
        let mut pe: ProcessEntry32W = unsafe { std::mem::zeroed() };
        pe.dw_size = std::mem::size_of::<ProcessEntry32W>() as u32;
        if unsafe { Process32FirstW(snap, &mut pe) } != 0 {
            loop {
                let len = pe.sz_exe_file.iter().take_while(|&&c| c != 0).count();
                let name = String::from_utf16_lossy(&pe.sz_exe_file[..len]);
                let command_line = if need_cmdline {
                    let cl = query_cmdline(pe.th32_process_id);
                    if cl.is_empty() {
                        name.clone()
                    } else {
                        cl
                    }
                } else {
                    String::new()
                };
                out.push(ProcInfo {
                    pid: pe.th32_process_id,
                    ppid: pe.th32_parent_process_id,
                    name,
                    command_line,
                });
                if unsafe { Process32NextW(snap, &mut pe) } == 0 {
                    break;
                }
            }
        }
        unsafe { CloseHandle(snap) };
        out
    }

    pub fn process_thread_ids(pid: u32) -> Vec<u32> {
        let mut tids = Vec::new();
        let snap = unsafe { CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0) };
        if snap as isize == -1 || snap.is_null() {
            return tids;
        }
        let mut te: ThreadEntry32 = unsafe { std::mem::zeroed() };
        te.dw_size = std::mem::size_of::<ThreadEntry32>() as u32;
        if unsafe { Thread32First(snap, &mut te) } != 0 {
            loop {
                if te.th32_owner_process_id == pid {
                    tids.push(te.th32_thread_id);
                }
                if unsafe { Thread32Next(snap, &mut te) } == 0 {
                    break;
                }
            }
        }
        unsafe { CloseHandle(snap) };
        tids
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
            return Err((err, std::io::Error::from_raw_os_error(err).to_string()));
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
pub mod sys {
    use super::ProcInfo;
    use std::path::Path;

    pub fn current_pid() -> u32 {
        std::process::id()
    }
    pub fn own_session_id() -> Option<u32> {
        Some(1)
    }
    pub fn now_filetime() -> u64 {
        0
    }
    pub fn account_exists(_name: &str) -> bool {
        false
    }
    pub fn is_file_locked(_path: &Path) -> bool {
        false
    }
    pub fn process_session_id(_pid: u32) -> Option<u32> {
        None
    }
    pub fn process_creation_filetime(_pid: u32) -> Option<u64> {
        None
    }
    pub fn process_owner(_pid: u32) -> Option<String> {
        None
    }
    pub fn process_group(_pid: u32) -> Option<String> {
        None
    }
    pub fn enumerate_processes(_need_cmdline: bool) -> Vec<ProcInfo> {
        Vec::new()
    }
    pub fn process_thread_ids(_pid: u32) -> Vec<u32> {
        Vec::new()
    }
    pub fn check_process(_pid: u32) -> bool {
        false
    }
    pub fn terminate_process(_pid: u32) -> Result<(), (i32, String)> {
        Err((1, "unsupported".to_owned()))
    }
}
