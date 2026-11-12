//! `who` — GNU coreutils who semantics on the Windows WTS value model
//! (C++ value model: `src/commands/who.cpp` on main; probe base:
//! target/probe/g1.sh).
//!
//! GNU contract implemented here:
//! * `who [OPTION]... [ FILE | ARG1 ARG2 ]`; zero or one (utmp FILE) or
//!   two ("am i" — implies -m) operands; a third operand is "who: extra
//!   operand 'C'" + try-help rc 1 (GNU probed). A FILE operand that cannot
//!   be opened simply yields zero records and a silent exit 0 (probed:
//!   `who /no/such/utmp` prints nothing, rc 0 — this port treats any
//!   missing FILE operand that way, exactly like the C++ applet).
//! * Default output rows: `%-8s user [ x] %-12s line %-12s time [ %-6s
//!   idle %10s pid] %-8s comment`, trailing blanks stripped (who.c
//!   print_line); -u/-w select the long form, -s forces short.
//! * -q: space-joined names plus "# users=N".
//! * -H prints the heading line through the same field renderer.
//! * --bogus → usage rc 1; GNU has no -h ("invalid option -- 'h'").
//!
//! Windows value model (documented; win-classified gate rows):
//! * Sessions come from WTSEnumerateSessionsW (Active/Disconnected), the
//!   utmp substitute this platform has; with no enumerable session the
//!   current interactive user is reported (the C++ simulation face).
//! * The mesg state is always '+' (no tty-writability bit on Windows).
//! * The TIME column width is 12 in this model; GNU uses 16 in the C
//!   locale (who.c TIME_FORMAT_WIDTH) — even the -H heading therefore
//!   differs from GNU bytes and gates win. The idle/pid columns come from
//!   WTSIdleTime and the oldest process in the session (toolhelp32 walk).
//! * The record-selecting options GNU reads from utmp (-a -b -d -l -p -r
//!   -t --lookup --writable) have no Windows representation: rejected
//!   with the C++ "option is not supported on Windows: X" diagnostic rc 1
//!   (GNU accepts them; win/note rows).

use std::ffi::OsString;
use std::io::Write as _;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('H'),
        long: "heading",
        takes_value: false,
    },
    // The utmp views with no Windows representation (rejected after parse
    // with the C++ diagnostic, so the grammar must still accept them).
    OptSpec {
        short: Some('a'),
        long: "all",
        takes_value: false,
    },
    OptSpec {
        short: Some('b'),
        long: "boot",
        takes_value: false,
    },
    OptSpec {
        short: Some('d'),
        long: "dead",
        takes_value: false,
    },
    OptSpec {
        short: Some('l'),
        long: "login",
        takes_value: false,
    },
    OptSpec {
        short: Some('p'),
        long: "process",
        takes_value: false,
    },
    OptSpec {
        short: Some('r'),
        long: "runlevel",
        takes_value: false,
    },
    OptSpec {
        short: Some('t'),
        long: "time",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "lookup",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "writable",
        takes_value: false,
    },
    // GNU alias: --message is the same as -T.
    OptSpec {
        short: None,
        long: "message",
        takes_value: false,
    },
    OptSpec {
        short: Some('m'),
        long: "w-internal-m",
        takes_value: false,
    },
    OptSpec {
        short: Some('q'),
        long: "count",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "short",
        takes_value: false,
    },
    OptSpec {
        short: Some('T'),
        long: "mesg-T",
        takes_value: false,
    },
    OptSpec {
        short: Some('u'),
        long: "users",
        takes_value: false,
    },
    OptSpec {
        short: Some('w'),
        long: "mesg",
        takes_value: false,
    },
    // GNU has no -h short option.
    argopt::H_REJECT_SPEC,
];

const WHO_HELP_BODY: &str = r#"Usage: who [OPTION]... [ FILE | ARG1 ARG2 ]
Print information about users who are currently logged in.

  -a, --all         same as -b -d --login -p -r -t -T -u
  -b, --boot        time of last system boot
  -d, --dead        print dead processes
  -H, --heading     print line of column headings
  -l, --login       print system login processes
      --lookup      attempt to canonicalize hostnames via DNS
  -m                only hostname and user associated with stdin
  -p, --process     print active processes spawned by init
  -q, --count       all login names and number of users logged on
  -r, --runlevel    print current runlevel
  -s, --short       print only name, line, and time (default)
  -t, --time        print last system clock change
  -T, -w, --mesg    add user's message status as +, - or ?
  -u, --users       list users logged in
      --message     same as -T
      --writable    same as -T
      --help        display this help and exit
      --version     output version information and exit

If FILE is not specified, use /var/run/utmp.  /var/log/wtmp as FILE is common.
If ARG1 ARG2 given, -m presumed: 'am i' or 'mom likes' are usual.

GNU coreutils online help: <https://www.gnu.org/software/coreutils/>
Report any translation bugs to <https://translationproject.org/team/>
Full documentation <https://www.gnu.org/software/coreutils/who>
or available locally via: info '(coreutils) who invocation'
"#;

#[cfg(windows)]
mod ffi {
    pub const WTS_ACTIVE: i32 = 0;
    pub const WTS_DISCONNECTED: i32 = 4;
    pub const WTS_CLIENT_NAME: u32 = 14;
    pub const WTS_USER_NAME: u32 = 5;
    pub const WTS_WINSTATION_NAME: u32 = 6;
    pub const WTS_LOGON_TIME: u32 = 16;
    pub const WTS_IDLE_TIME: u32 = 17;

    #[repr(C)]
    pub struct WtsSessionInfoW {
        pub session_id: u32,
        pub winstation_name: *mut u16,
        pub state: i32,
    }

    #[link(name = "wtsapi32")]
    #[link(name = "wtsapi32")]
    extern "system" {
        pub fn WTSEnumerateSessionsW(
            server: isize,
            reserved: u32,
            version: u32,
            sessions: *mut *mut WtsSessionInfoW,
            count: *mut u32,
        ) -> i32;
        pub fn WTSQuerySessionInformationW(
            server: isize,
            session_id: u32,
            class: u32,
            buffer: *mut *mut u16,
            bytes: *mut u32,
        ) -> i32;
        pub fn WTSFreeMemory(memory: *mut u8);
    }

    #[link(name = "kernel32")]
    #[link(name = "wtsapi32")]
    extern "system" {
        pub fn WTSGetActiveConsoleSessionId() -> u32;
        pub fn ProcessIdToSessionId(pid: u32, session_id: *mut u32) -> i32;
        pub fn OpenProcess(access: u32, inherit: i32, pid: u32) -> isize;
        pub fn GetProcessTimes(
            process: isize,
            creation: *mut u64,
            exit: *mut u64,
            kernel: *mut u64,
            user: *mut u64,
        ) -> i32;
        pub fn CloseHandle(handle: isize) -> i32;
        pub fn CreateToolhelp32Snapshot(flags: u32, pid: u32) -> isize;
        pub fn Process32FirstW(snapshot: isize, entry: *mut ProcessEntry32W) -> i32;
        pub fn Process32NextW(snapshot: isize, entry: *mut ProcessEntry32W) -> i32;
    }

    #[repr(C)]
    pub struct ProcessEntry32W {
        pub size: u32,
        pub cnt_usage: u32,
        pub process_id: u32,
        pub default_heap: usize,
        pub module_id: u32,
        pub cnt_threads: u32,
        pub parent_process_id: u32,
        pub pri_class_base: i32,
        pub flags: u32,
        pub exe_file: [u16; 260],
    }

    pub const TH32CS_SNAPPROCESS: u32 = 0x2;
    pub const PROCESS_QUERY_LIMITED_INFORMATION: u32 = 0x1000;
    pub const INVALID_HANDLE_VALUE: isize = -1;
}

#[cfg(windows)]
fn wide_to_string(buf: &[u16]) -> String {
    let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..len])
}

struct WhoEntry {
    user: String,
    line: String,
    /// unix seconds; -1 when unknown (C++ logon_time model)
    logon_time: i64,
    idle_seconds: i64,
    pid: i64,
    comment: String,
    session_id: u32,
}

#[cfg(windows)]
fn query_wstring(session_id: u32, kind: u32) -> String {
    let mut buffer: *mut u16 = std::ptr::null_mut();
    let mut bytes = 0u32;
    let mut result = String::new();
    let ok = unsafe {
        ffi::WTSQuerySessionInformationW(0isize, session_id, kind, &mut buffer, &mut bytes)
    };
    if ok != 0 && !buffer.is_null() {
        // the buffer is a NUL-terminated wide string for name classes
        let mut len = 0usize;
        unsafe {
            while *buffer.add(len) != 0 {
                len += 1;
            }
        }
        result = wide_to_string(unsafe { std::slice::from_raw_parts(buffer, len) });
    }
    if !buffer.is_null() {
        unsafe { ffi::WTSFreeMemory(buffer as *mut u8) };
    }
    result
}

#[cfg(windows)]
fn query_i64(session_id: u32, kind: u32, wide_units: bool) -> i64 {
    let mut buffer: *mut u16 = std::ptr::null_mut();
    let mut bytes = 0u32;
    let mut result = -1i64;
    let ok = unsafe {
        ffi::WTSQuerySessionInformationW(0isize, session_id, kind, &mut buffer, &mut bytes)
    };
    if ok != 0 && !buffer.is_null() && bytes as usize >= std::mem::size_of::<i64>() {
        let raw = buffer as *const u64;
        let value = unsafe { *raw };
        if wide_units {
            // WTSLogonTime: FILETIME ticks -> unix seconds
            result = (value / 10_000_000) as i64 - 11_644_473_600;
        } else {
            // WTSIdleTime: milliseconds (u32 in the low dword)
            result = ((value & 0xFFFF_FFFF) / 1000) as i64;
        }
    }
    if !buffer.is_null() {
        unsafe { ffi::WTSFreeMemory(buffer as *mut u8) };
    }
    result
}

/// The utmp ut_pid of a login entry corresponds to the session's login
/// (leader) process: the oldest process living in the session (C++
/// collect_leader_pids via the toolhelp32 snapshot).
#[cfg(windows)]
fn collect_leader_pids() -> std::collections::BTreeMap<u32, i64> {
    let mut leaders = std::collections::BTreeMap::new();
    let mut created: std::collections::BTreeMap<u32, u64> = std::collections::BTreeMap::new();
    unsafe {
        let snapshot = ffi::CreateToolhelp32Snapshot(ffi::TH32CS_SNAPPROCESS, 0);
        if snapshot == ffi::INVALID_HANDLE_VALUE {
            return leaders;
        }
        let mut entry = ffi::ProcessEntry32W {
            size: std::mem::size_of::<ffi::ProcessEntry32W>() as u32,
            cnt_usage: 0,
            process_id: 0,
            default_heap: 0,
            module_id: 0,
            cnt_threads: 0,
            parent_process_id: 0,
            pri_class_base: 0,
            flags: 0,
            exe_file: [0; 260],
        };
        let mut ok = ffi::Process32FirstW(snapshot, &mut entry);
        while ok != 0 {
            let mut session_id = 0u32;
            if ffi::ProcessIdToSessionId(entry.process_id, &mut session_id) != 0 {
                let process =
                    ffi::OpenProcess(ffi::PROCESS_QUERY_LIMITED_INFORMATION, 0, entry.process_id);
                if process != 0 {
                    let mut creation = 0u64;
                    let mut exit_t = 0u64;
                    let mut kernel = 0u64;
                    let mut user = 0u64;
                    if ffi::GetProcessTimes(
                        process,
                        &mut creation,
                        &mut exit_t,
                        &mut kernel,
                        &mut user,
                    ) != 0
                    {
                        let better = match created.get(&session_id) {
                            None => true,
                            Some(&prev) => creation < prev,
                        };
                        if better {
                            created.insert(session_id, creation);
                            leaders.insert(session_id, i64::from(entry.process_id));
                        }
                    }
                    ffi::CloseHandle(process);
                }
            }
            ok = ffi::Process32NextW(snapshot, &mut entry);
        }
        ffi::CloseHandle(snapshot);
    }
    leaders
}

#[cfg(windows)]
fn current_session_id() -> u32 {
    let mut session_id = 0u32;
    unsafe {
        ffi::ProcessIdToSessionId(std::process::id(), &mut session_id);
    }
    session_id
}

#[cfg(windows)]
fn enumerate_entries(want_idle_pid: bool) -> Vec<WhoEntry> {
    let leaders = if want_idle_pid {
        collect_leader_pids()
    } else {
        std::collections::BTreeMap::new()
    };
    let console_session = unsafe { ffi::WTSGetActiveConsoleSessionId() };
    let mut entries = Vec::new();
    let mut sessions: *mut ffi::WtsSessionInfoW = std::ptr::null_mut();
    let mut count = 0u32;
    let ok = unsafe { ffi::WTSEnumerateSessionsW(0isize, 0, 1, &mut sessions, &mut count) };
    if ok != 0 && !sessions.is_null() {
        for i in 0..count as usize {
            let s = unsafe { &*sessions.add(i) };
            if s.state != ffi::WTS_ACTIVE && s.state != ffi::WTS_DISCONNECTED {
                continue;
            }
            let user = query_wstring(s.session_id, ffi::WTS_USER_NAME);
            if user.is_empty() {
                continue;
            }
            let line = query_wstring(s.session_id, ffi::WTS_WINSTATION_NAME);
            let logon_time = query_i64(s.session_id, ffi::WTS_LOGON_TIME, true);
            let idle_seconds = if want_idle_pid {
                query_i64(s.session_id, ffi::WTS_IDLE_TIME, false)
            } else {
                -1
            };
            let pid = if want_idle_pid {
                leaders.get(&s.session_id).copied().unwrap_or(-1)
            } else {
                -1
            };
            let comment = if s.session_id == console_session {
                "(console)".to_string()
            } else {
                let client = query_wstring(s.session_id, ffi::WTS_CLIENT_NAME);
                if client.is_empty() {
                    String::new()
                } else {
                    format!("({client})")
                }
            };
            entries.push(WhoEntry {
                user,
                line,
                logon_time,
                idle_seconds,
                pid,
                comment,
                session_id: s.session_id,
            });
        }
        unsafe { ffi::WTSFreeMemory(sessions as *mut u8) };
    }
    if entries.is_empty() {
        // Simulation face: with no enumerable login sessions report the
        // current interactive user (the C++ fallback).
        let user = crate::winsys::imp::user_name().unwrap_or_default();
        if !user.is_empty() {
            let session_id = current_session_id();
            entries.push(WhoEntry {
                user,
                line: "console".to_string(),
                logon_time: std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH)
                    .map(|d| d.as_secs() as i64)
                    .unwrap_or(-1),
                idle_seconds: -1,
                pid: -1,
                comment: "(console)".to_string(),
                session_id,
            });
        }
    }
    entries
}

#[cfg(not(windows))]
fn enumerate_entries(_want_idle_pid: bool) -> Vec<WhoEntry> {
    Vec::new()
}

/// [GNU] who.c idle_string(): "  .  " under a minute, HH:MM under a day,
/// " old " past it, "  ?" when unknown.
fn idle_string(idle_seconds: i64) -> String {
    if idle_seconds < 0 {
        return "  ?".to_string();
    }
    if idle_seconds < 60 {
        return "  .  ".to_string();
    }
    if idle_seconds < 24 * 60 * 60 {
        return format!(
            "{:02}:{:02}",
            idle_seconds / (60 * 60),
            (idle_seconds % (60 * 60)) / 60
        );
    }
    " old ".to_string()
}

/// [GNU] the C/POSIX time face is "%b %e %H:%M" (who.c:803-811).
fn format_logon(logon_time: i64) -> String {
    if logon_time < 0 {
        return String::new();
    }
    let ticks = (logon_time + 11_644_473_600) * 10_000_000;
    match crate::fs_win::to_local(ticks) {
        Some((local, _offset)) => {
            const MONTHS: [&str; 12] = [
                "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
            ];
            let month = MONTHS
                .get((local.month as usize).saturating_sub(1).min(11))
                .copied()
                .unwrap_or("Jan");
            format!(
                "{} {:2} {:02}:{:02}",
                month, local.day, local.hour, local.minute
            )
        }
        None => String::new(),
    }
}

fn pad_right(value: &str, width: usize) -> String {
    let len = value.chars().count();
    if len >= width {
        value.to_string()
    } else {
        format!("{value}{}", " ".repeat(width - len))
    }
}

fn pad_left(value: &str, width: usize) -> String {
    let len = value.chars().count();
    if len >= width {
        value.to_string()
    } else {
        format!("{}{}", " ".repeat(width - len), value)
    }
}

fn rtrim(value: String) -> String {
    value.trim_end_matches(' ').to_string()
}

/// [GNU] who.c print_line(): "%-8s" user, optional " x" mesg, " %-12s"
/// line, " %-*s" time, optional " %-6s" idle + " %10s" pid (long form),
/// " %-8s" comment; trailing blanks stripped. TIME width 12 per the C++
/// model (GNU's C locale uses 16 — documented deviation).
#[allow(clippy::too_many_arguments)]
fn print_line(
    include_mesg: bool,
    include_idle: bool,
    short_output: bool,
    user: &str,
    state: char,
    line: &str,
    time_str: &str,
    idle: &str,
    pid: &str,
    comment: &str,
) -> String {
    let mut out = pad_right(user, 8);
    if include_mesg {
        out.push(' ');
        out.push(state);
    }
    out.push(' ');
    out.push_str(&pad_right(line, 12));
    out.push(' ');
    out.push_str(&pad_right(time_str, 12));
    if include_idle && !short_output {
        out.push(' ');
        out.push_str(&pad_right(idle, 6));
        out.push(' ');
        out.push_str(&pad_left(pid, 10));
    }
    out.push(' ');
    out.push_str(&pad_right(comment, 8));
    rtrim(out)
}

const UNSUPPORTED: [(&str, &str); 9] = [
    ("all", "--all"),
    ("boot", "--boot"),
    ("dead", "--dead"),
    ("login", "--login"),
    ("process", "--process"),
    ("runlevel", "--runlevel"),
    ("time", "--time"),
    ("lookup", "--lookup"),
    ("writable", "--writable"),
];

pub fn who_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("who", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(status) = argopt::rejected_short_h(io, "who", &p, 1) {
        return Ok(status);
    }
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                // the body already carries the GNU footer block verbatim
                write!(io.stdout, "{}", WHO_HELP_BODY)?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version(
                        "who",
                        "Joseph Arceneaux, David MacKenzie, and Michael Stone"
                    )
                )?;
                0
            }
        });
    }

    // [GNU] who.c: the record-selecting utmp views have no Windows
    // representation; the C++ applet rejects them (first one wins).
    for (key, spelling) in UNSUPPORTED {
        if p.has(key) {
            let _ = writeln!(
                io.stderr,
                "who: option is not supported on Windows: {spelling}"
            );
            return Ok(1);
        }
    }

    let heading = p.has("heading");
    let short_list = p.has("count");
    let include_mesg = p.has("mesg") || p.has("mesg-T") || p.has("message");
    let include_idle = p.has("users");
    let mut my_line_only = p.has("w-internal-m");
    // [GNU] with no record-selecting option the output stays short; -u
    // selects the long form and an explicit -s forces short back on.
    let mut short_output = !include_idle;
    if p.has("short") {
        short_output = true;
    }

    // Operands: zero, one (utmp FILE), or two ("am i").
    if p.positionals.len() > 2 {
        let _ = writeln!(
            io.stderr,
            "who: extra operand '{}'",
            p.positionals[2].to_string_lossy()
        );
        let _ = writeln!(io.stderr, "Try 'who --help' for more information.");
        return Ok(1);
    }
    if p.positionals.len() == 2 {
        my_line_only = true;
    }
    let mut no_records = false;
    if p.positionals.len() == 1 {
        // A FILE operand that cannot be opened simply yields zero records
        // (GNU probed rc 0); an existing one is accepted but its records
        // are not parsed — the session table replaces utmp on Windows.
        let operand = p.positionals[0].to_string_lossy().into_owned();
        let full = crate::fsutil::resolve(ctx, &operand);
        if !full.exists() {
            no_records = true;
        }
    }

    let mut entries = if no_records {
        Vec::new()
    } else {
        #[cfg(windows)]
        {
            enumerate_entries(include_idle)
        }
        #[cfg(not(windows))]
        {
            Vec::new()
        }
    };

    if my_line_only && !short_list {
        #[cfg(windows)]
        {
            let mine = current_session_id();
            entries.retain(|e| e.session_id == mine);
        }
    }

    let out = &mut *io.stdout;
    if short_list {
        // [GNU] list_entries_who(): names space-joined on one line, then
        // "# users=N".
        let line = entries
            .iter()
            .map(|e| e.user.as_str())
            .collect::<Vec<_>>()
            .join(" ");
        let _ = writeln!(out, "{line}");
        let _ = writeln!(out, "# users={}", entries.len());
        return Ok(0);
    }

    if heading {
        let _ = writeln!(
            out,
            "{}",
            print_line(
                include_mesg,
                include_idle,
                short_output,
                "NAME",
                ' ',
                "LINE",
                "TIME",
                "IDLE",
                "PID",
                "COMMENT"
            )
        );
    }

    for entry in &entries {
        let _ = writeln!(
            out,
            "{}",
            print_line(
                include_mesg,
                include_idle,
                short_output,
                &entry.user,
                '+',
                &entry.line,
                &format_logon(entry.logon_time),
                &idle_string(entry.idle_seconds),
                &if entry.pid >= 0 {
                    entry.pid.to_string()
                } else {
                    String::new()
                },
                &entry.comment
            )
        );
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("who");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::who_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn missing_file_operand_is_silent() {
        // GNU probed: `who /no/such/utmp` prints nothing, rc 0
        let (r, out, err) = run(&["who-nosuch-utmp-file"]);
        assert_eq!((r, out.as_str(), err.as_str()), (0, "", ""));
    }

    #[test]
    fn short_list_counts_users() {
        // -q prints names + "# users=N" (Windows value model: usually 1)
        let (r, out, _) = run(&["-q"]);
        assert_eq!(r, 0);
        let lines: Vec<&str> = out.lines().collect();
        assert_eq!(lines.len(), 2, "{out}");
        assert!(lines[1].starts_with("# users="), "{out}");
        let n: usize = lines[1].trim_start_matches("# users=").parse().unwrap();
        assert_eq!(lines[0].split(' ').filter(|s| !s.is_empty()).count(), n);
    }

    #[test]
    fn heading_shape() {
        // C++ model widths: NAME 8, LINE 12, TIME 12, COMMENT 8 (GNU uses
        // TIME 16 — documented deviation; win-classified rows)
        let (r, out, _) = run(&["-H"]);
        assert_eq!(r, 0);
        let lines: Vec<&str> = out.lines().collect();
        assert_eq!(lines.len(), 2, "{out}");
        assert!(lines[0].starts_with("NAME     LINE         TIME"), "{out}");
        assert!(lines[0].ends_with("COMMENT"), "{out}");
        // row: user padded to 8 then " Console" (or the WTS name)
        assert!(lines[1].contains("onsole"), "{out}");
    }

    #[test]
    fn am_i_and_m() {
        // "who am i" implies -m; filters to this session (simulation face
        // keeps one row on this host)
        let (r, out, _) = run(&["am", "i"]);
        assert_eq!(r, 0);
        let (r2, out2, _) = run(&["-m"]);
        assert_eq!(r2, 0);
        if !out.is_empty() && !out2.is_empty() {
            assert_eq!(out, out2, "am i vs -m");
        }
    }

    #[test]
    fn long_form_columns() {
        let (r, out, _) = run(&["-u", "-H"]);
        assert_eq!(r, 0);
        let lines: Vec<&str> = out.lines().collect();
        assert_eq!(lines.len(), 2, "{out}");
        assert!(lines[0].contains("IDLE"), "{out}");
        assert!(lines[0].contains("PID"), "{out}");
    }

    #[test]
    fn unsupported_views_are_rejected() {
        for opt in [
            "-a",
            "--all",
            "-b",
            "--boot",
            "-r",
            "--runlevel",
            "--writable",
        ] {
            let (r, _, err) = run(&[opt]);
            assert_eq!(r, 1, "opt={opt}");
            assert!(
                err.contains("option is not supported on Windows:"),
                "opt={opt} err={err}"
            );
        }
    }

    #[test]
    fn extra_operand_is_gnu_worded() {
        // GNU probed: who: extra operand 'c' + try-help, rc 1
        let (r, _, err) = run(&["a", "b", "c"]);
        assert_eq!(r, 1);
        assert!(err.contains("who: extra operand 'c'"), "{err}");
        assert!(err.contains("Try 'who --help' for more information."));
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, _) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: who [OPTION]... [ FILE | ARG1 ARG2 ]\n"));
        assert!(out.contains("If ARG1 ARG2 given, -m presumed: 'am i' or 'mom likes' are usual."));
        assert!(out.ends_with("info '(coreutils) who invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("who (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Joseph Arceneaux, David MacKenzie, and Michael Stone.\n"));
        let (r, _, err) = run(&["-h"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid option -- 'h'"));
    }

    #[test]
    fn idle_string_shapes() {
        assert_eq!(super::idle_string(-1), "  ?");
        assert_eq!(super::idle_string(30), "  .  ");
        assert_eq!(super::idle_string(3700), "01:01");
        assert_eq!(super::idle_string(25 * 3600), " old ");
    }

    #[test]
    fn print_line_rstrips_trailing_blanks() {
        let l = super::print_line(false, false, true, "cmx", '+', "Console", "", "", "", "");
        assert_eq!(l, "cmx      Console", "{l}");
    }
}
