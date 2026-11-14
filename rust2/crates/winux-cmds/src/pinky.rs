//! `pinky` — GNU coreutils pinky.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4
//! (target/issue-suites/results/wave5/probe-pinky*.sh, gnu-pinky.c vendored
//! from coreutils v9.4 for the format strings).
//!
//! A lightweight finger: the short format prints one row per login entry
//! with an optional column heading (pinky.c print_heading, print_entry);
//! `-l USER` prints the long per-user block (print_long_entry). The
//! option string is `"sfwiqbhlp"` with NO long options (pinky.c:532 —
//! every non-help/version long token is "unrecognized option", probed via
//! `pinky --lookup`), so each option registers short-only in argopt and
//! last-wins applies where GNU's switch simply assigns (-l/-s: `pinky
//! -l -s root` is short format, `pinky -s -l root` long, probed).
//!
//! Windows value model (the 1.x C++ applet's, src/commands/pinky.cpp —
//! the differential anchor for platform-inherent output):
//! * login entries come from WTSEnumerateSessionsW (Active/Disconnected),
//!   the utmp substitute, as in who/users; with no enumerable session the
//!   current interactive user is reported with logon time = now.
//! * heading columns are %-8s Login, " %-19s" Name, " %-9s" of " TTY",
//!   " %-6s" Idle, then the When field at the C++ 1.x width 12
//!   ("%b %e %H:%M"); GNU's C-locale build prints width 12 too, but a
//!   hard LC_TIME locale (C.UTF-8 on the WSL oracle host) switches GNU to
//!   the ISO "%Y-%m-%d %H:%M" at width 16 (pinky.c scan_entries) — the
//!   documented heading divergence, gate rows for headings are win-mode
//!   (Rust vs C++ Debug only).
//! * rows: no GECOS on Windows, so the Name cell is GNU's unknown form
//!   ("???", right-aligned 19); the tty stat GNU does for the mesg char
//!   has no device to stat here -> '?'; the line name is the C++ "console";
//!   idle comes from WTSIdleTime ("?????" unknown / blank under a minute /
//!   HH:MM under a day / Nd past it, pinky.c idle_string); Where stays
//!   empty (console sessions carry no ut_host) and rows are rstripped.
//! * long format: real name only exists for the current user
//!   ("DOMAIN\\user" via USERDOMAIN, the C++ current_user model); any
//!   other name takes GNU's unresolved branch (" ???" and early return,
//!   no Directory/Shell, no trailing blank). Directory prints as the C++
//!   "C:/Users/<user>" value with GNU's %-29s padding and the constant
//!   "Shell:  /usr/bin/bash". -h/-p (project/plan) are accepted no-ops:
//!   no project or plan files exist on this platform.
//!
//! Exit status: 0 on success, 1 for usage failures (`pinky -l` without a
//! user, unknown options — pinky.c usage(EXIT_FAILURE)).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

/// Short-only specs (empty long name): pinky.c:532 `"sfwiqbhlp"` has no
/// longopts besides the built-in help/version.
const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('s'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('w'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('i'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('q'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('b'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('h'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('l'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('p'),
        long: "",
        takes_value: false,
    },
];

// `--help` body verbatim from `env pinky --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail (probe-pinky.sh case 16).
const PINKY_HELP_BODY: &str = r#"Usage: pinky [OPTION]... [USER]...

  -l              produce long format output for the specified USERs
  -b              omit the user's home directory and shell in long format
  -h              omit the user's project file in long format
  -p              omit the user's plan file in long format
  -s              do short format output, this is the default
  -f              omit the line of column headings in short format
  -w              omit the user's full name in short format
  -i              omit the user's full name and remote host in short format
  -q              omit the user's full name, remote host and idle time
                  in short format
      --help        display this help and exit
      --version     output version information and exit

A lightweight 'finger' program;  print user information.
The utmp file will be /var/run/utmp.

"#;

/// The format flags, all true by default (pinky.c main's initial state).
struct Flags {
    short_format: bool,
    heading: bool,
    fullname: bool,
    where_col: bool,
    idle: bool,
    home_shell: bool,
}

impl Flags {
    fn new() -> Self {
        Flags {
            short_format: true,
            heading: true,
            fullname: true,
            where_col: true,
            idle: true,
            home_shell: true,
        }
    }
}

/// One login entry: the utmp USER_PROCESS row the Windows WTS table
/// substitutes (C++ SessionEntry; seconds models, -1 unknown).
pub(crate) struct SessionEntry {
    user: String,
    /// unix seconds; -1 when unknown
    logon_time: i64,
    idle_seconds: i64,
}

/// [GNU] pinky.c idle_string(): blank under a minute, HH:MM under a day,
/// "<days>d" past it; the unknown form is "?????" (print_entry's
/// last_change == 0 face, which the missing-stat Windows model always
/// pairs with WTSIdleTime = -1).
fn idle_string(idle_seconds: i64) -> String {
    if idle_seconds < 0 {
        return "?????".to_string();
    }
    if idle_seconds < 60 {
        return "     ".to_string();
    }
    if idle_seconds < 24 * 60 * 60 {
        return format!(
            "{:02}:{:02}",
            idle_seconds / (60 * 60),
            (idle_seconds % (60 * 60)) / 60
        );
    }
    format!("{}d", idle_seconds / (24 * 60 * 60))
}

/// [GNU] pinky.c print_heading(): "%-8s" Login, " %-19s" Name, " %-9s" of
/// " TTY", " %-6s" Idle, " %-*s" When, " %s" Where. The When width here is
/// the C++ 1.x model's 12 (C-locale "%b %e %H:%M"); the WSL oracle host
/// runs a hard LC_TIME locale and prints 16 — recorded divergence.
pub(crate) fn short_heading(fullname: bool, idle: bool, where_col: bool) -> String {
    let mut out = pad_right("Login", 8);
    if fullname {
        out += " ";
        out += &pad_right("Name", 19);
    }
    out += " ";
    out += &pad_right(" TTY", 9);
    if idle {
        out += " ";
        out += &pad_right("Idle", 6);
    }
    out += " ";
    out += "When";
    out += &" ".repeat(8);
    if where_col {
        out += " Where";
    }
    out + "\n"
}

/// [GNU] pinky.c print_entry(): "%-8s" user, " %19s" of the unknown-name
/// form, " " + mesg + "%-8s" line, " %-6s" idle, " %s" time; the Windows
/// model ends the row at the time field (empty ut_host) and rstrips, as
/// the 1.x C++ applet does. `time` arrives preformatted ("%b %e %H:%M").
pub(crate) fn short_row(
    user: &str,
    logon_time: i64,
    idle_seconds: i64,
    include_fullname: bool,
    include_idle: bool,
) -> String {
    let mut out = pad_right(user, 8);
    if include_fullname {
        // [DIFFERS] no GECOS on Windows: GNU's unknown-name face
        // (print_entry's getpwnam == nullptr printf " %19s" "        ???").
        out += " ";
        out += &pad_right_justified("???", 19);
    }
    out += " ?";
    out += &pad_right("console", 8);
    if include_idle {
        out += " ";
        out += &pad_right(&idle_string(idle_seconds), 6);
    }
    out += " ";
    out += &time_string(logon_time);
    out.trim_end_matches(' ').to_string() + "\n"
}

/// [GNU] pinky.c time_string() under the C++ 1.x model: "%b %e %H:%M"
/// local time (space-padded day), empty when the time is unknown.
pub(crate) fn time_string(logon_time: i64) -> String {
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

/// [GNU] pinky.c print_long_entry(): "Login name: " + "%-28s" name +
/// "In real life: " + " " + real name; the unresolved branch prints " ???"
/// and returns (no Directory/Shell, no trailing blank), the resolved one
/// optionally adds "Directory: " + "%-29s" + "Shell:  %s" and the final
/// blank line (putchar('\n') at function end). Windows resolves only the
/// current user (C++ model: "DOMAIN\\user" or the bare name).
pub(crate) fn long_entry(
    name: &str,
    include_home_shell: bool,
    current_user: &str,
    domain: &str,
) -> String {
    let mut out = String::from("Login name: ");
    out += &pad_right(name, 28);
    out += "In real life: ";
    let current = !current_user.is_empty() && name == current_user;
    if !current {
        // GNU's getpwnam == nullptr face: "In real life: " + " %s" of
        // "???\n" — two spaces total — then the early return.
        out += " ???\n";
        return out;
    }
    let real = if domain.is_empty() {
        current_user.to_string()
    } else {
        format!("{domain}\\{current_user}")
    };
    out += " ";
    out += &real;
    out += "\n";
    if include_home_shell {
        let dir = format!("C:/Users/{current_user}");
        out += "Directory: ";
        out += &pad_right(&dir, 29);
        out += "Shell:  /usr/bin/bash\n";
    }
    out += "\n";
    out
}

fn pad_right(value: &str, width: usize) -> String {
    let mut out = value.to_string();
    if value.len() < width {
        out.push_str(&" ".repeat(width - value.len()));
    }
    out
}

fn pad_right_justified(value: &str, width: usize) -> String {
    if value.len() >= width {
        value.to_string()
    } else {
        format!("{}{value}", " ".repeat(width - value.len()))
    }
}

#[cfg(windows)]
mod wts {
    use super::SessionEntry;

    pub const WTS_ACTIVE: i32 = 0;
    pub const WTS_DISCONNECTED: i32 = 4;
    pub const WTS_USER_NAME: u32 = 5;
    pub const WTS_LOGON_TIME: u32 = 16;
    pub const WTS_IDLE_TIME: u32 = 17;

    #[repr(C)]
    pub struct WtsSessionInfoW {
        pub session_id: u32,
        pub winstation_name: *mut u16,
        pub state: i32,
    }

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

    fn query_wstring(session_id: u32, kind: u32) -> String {
        let mut buffer: *mut u16 = std::ptr::null_mut();
        let mut bytes = 0u32;
        let mut result = String::new();
        let ok = unsafe {
            WTSQuerySessionInformationW(0isize, session_id, kind, &mut buffer, &mut bytes)
        };
        if ok != 0 && !buffer.is_null() {
            let mut len = 0usize;
            unsafe {
                while *buffer.add(len) != 0 {
                    len += 1;
                }
            }
            let slice = unsafe { std::slice::from_raw_parts(buffer, len) };
            result = String::from_utf16_lossy(slice);
        }
        if !buffer.is_null() {
            unsafe { WTSFreeMemory(buffer as *mut u8) };
        }
        result
    }

    /// WTSLogonTime (FILETIME ticks -> unix seconds) and WTSIdleTime
    /// (milliseconds -> seconds), -1 when unavailable.
    fn query_time(session_id: u32, kind: u32) -> i64 {
        let mut buffer: *mut u16 = std::ptr::null_mut();
        let mut bytes = 0u32;
        let mut result = -1i64;
        let ok = unsafe {
            WTSQuerySessionInformationW(0isize, session_id, kind, &mut buffer, &mut bytes)
        };
        if ok != 0 && !buffer.is_null() && bytes as usize >= std::mem::size_of::<u64>() {
            let raw = unsafe { *(buffer as *const u64) };
            result = if kind == WTS_LOGON_TIME {
                (raw / 10_000_000) as i64 - 11_644_473_600
            } else {
                ((raw & 0xFFFF_FFFF) / 1000) as i64
            };
        }
        if !buffer.is_null() {
            unsafe { WTSFreeMemory(buffer as *mut u8) };
        }
        result
    }

    /// The utmp USER_PROCESS substitute (C++ pinky.cpp session_entries):
    /// Active/Disconnected sessions with a user name; logon time from
    /// WTSLogonTime, idle from WTSIdleTime.
    pub fn session_entries() -> Vec<SessionEntry> {
        let mut entries = Vec::new();
        let mut sessions: *mut WtsSessionInfoW = std::ptr::null_mut();
        let mut count = 0u32;
        let ok = unsafe { WTSEnumerateSessionsW(0isize, 0, 1, &mut sessions, &mut count) };
        if ok != 0 && !sessions.is_null() {
            for i in 0..count as usize {
                let s = unsafe { &*sessions.add(i) };
                if s.state != WTS_ACTIVE && s.state != WTS_DISCONNECTED {
                    continue;
                }
                let user = query_wstring(s.session_id, WTS_USER_NAME);
                if user.is_empty() {
                    continue;
                }
                entries.push(SessionEntry {
                    user,
                    logon_time: query_time(s.session_id, WTS_LOGON_TIME),
                    idle_seconds: query_time(s.session_id, WTS_IDLE_TIME),
                });
            }
            unsafe { WTSFreeMemory(sessions as *mut u8) };
        }
        if entries.is_empty() {
            // Simulation face: with no enumerable login session report the
            // current interactive user at logon time = now (the C++ fallback).
            if let Some(user) = crate::winsys::imp::user_name() {
                if !user.is_empty() {
                    entries.push(SessionEntry {
                        user,
                        logon_time: std::time::SystemTime::now()
                            .duration_since(std::time::UNIX_EPOCH)
                            .map(|d| d.as_secs() as i64)
                            .unwrap_or(-1),
                        idle_seconds: -1,
                    });
                }
            }
        }
        entries
    }
}

#[cfg(not(windows))]
mod wts {
    use super::SessionEntry;

    pub fn session_entries() -> Vec<SessionEntry> {
        Vec::new()
    }
}

/// The current account name (C++ current_user_name: GetUserNameW) and
/// domain (current_domain_name: USERDOMAIN), the long-format real-name
/// model.
fn current_user_and_domain() -> (String, String) {
    let user = crate::winsys::imp::user_name().unwrap_or_default();
    let domain = std::env::var("USERDOMAIN").unwrap_or_default();
    (user, domain)
}

pub fn pinky_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("pinky", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                write!(
                    io.stdout,
                    "{}{}",
                    PINKY_HELP_BODY,
                    gnuhelp::footer("pinky", "pinky invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version(
                        "pinky",
                        "Joseph Arceneaux, David MacKenzie, and Kaveh Ghazi"
                    )
                )?;
                0
            }
        });
    }

    // GNU main()'s switch assigns per option in getopt order, so -l/-s
    // last-wins (`pinky -l -s root` is short format, probed).
    let mut flags = Flags::new();
    for flag in &p.flags {
        match flag.as_str() {
            "s" => flags.short_format = true,
            "l" => flags.short_format = false,
            "f" => flags.heading = false,
            "w" => flags.fullname = false,
            "i" => {
                flags.fullname = false;
                flags.where_col = false;
            }
            "q" => {
                flags.fullname = false;
                flags.where_col = false;
                flags.idle = false;
            }
            "b" => flags.home_shell = false,
            // -h/-p (project/plan files): accepted no-ops on this platform,
            // as the 1.x C++ applet documents.
            _ => {}
        }
    }

    let users: Vec<String> = p
        .positionals
        .iter()
        .map(|a| a.to_string_lossy().into_owned())
        .collect();

    if !flags.short_format && users.is_empty() {
        // pinky.c main(): error(0, 0, ...) + usage(EXIT_FAILURE) — the
        // try-help tail goes to stderr and the status is 1 (probed). The
        // program prefix and the shared try-help tail stay in code; the
        // usage diagnostic body is the tr! key.
        let message = tr!(
            ctx,
            "command.pinky.error.no_username_for_long",
            "no username specified; at least one must be specified when using -l"
        );
        let _ = usage_error(io, "pinky", &message);
        return Ok(1);
    }

    if flags.short_format {
        if flags.heading {
            write!(
                io.stdout,
                "{}",
                short_heading(flags.fullname, flags.idle, flags.where_col)
            )?;
        }
        // [GNU] scan_entries: one row per USER_PROCESS entry; with USER
        // arguments only matching entries print (an unknown name matches
        // nothing).
        let entries = wts::session_entries();
        for entry in &entries {
            if !users.is_empty() && !users.iter().any(|u| *u == entry.user) {
                continue;
            }
            write!(
                io.stdout,
                "{}",
                short_row(
                    &entry.user,
                    entry.logon_time,
                    entry.idle_seconds,
                    flags.fullname,
                    flags.idle
                )
            )?;
        }
    } else {
        let (current_user, domain) = current_user_and_domain();
        for user in &users {
            write!(
                io.stdout,
                "{}",
                long_entry(user, flags.home_shell, &current_user, &domain)
            )?;
        }
    }
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], stdin: &str) -> (u32, String, String) {
        let sc = ScratchCtx::new("pinky");
        let mut io = MemIo::new(stdin);
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = pinky_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    // ===== pure format faces (GNU pinky.c byte models) =====

    #[test]
    fn heading_full_matches_gnu_column_offsets() {
        // GNU offsets (measured under WSL 9.4, width-12 C++ model):
        // Login 0, Name 9, TTY 30, Idle 39, When 46, Where 62+... with the
        // C++ When width 12 the Where lands 4 columns earlier than the
        // oracle host's hard-locale ISO face — the recorded divergence.
        let h = short_heading(true, true, true);
        assert_eq!(h.find("Login"), Some(0));
        assert_eq!(h.find("Name"), Some(9));
        assert_eq!(h.find("TTY"), Some(30));
        assert_eq!(h.find("Idle"), Some(39));
        assert_eq!(h.find("When"), Some(46));
        assert_eq!(h.find("Where"), Some(59));
        assert_eq!(h.len(), 65);
    }

    #[test]
    fn heading_option_faces_shrink_exactly() {
        // Byte lengths measured on the GNU side with the C++ width-12 When:
        // full 65 (69 - 4), -w 45 (49 - 4), -q 32 (36 - 4), -i 39 (43 - 4).
        assert_eq!(short_heading(false, true, true).len(), 45);
        assert_eq!(short_heading(false, false, false).len(), 32);
        assert_eq!(short_heading(false, true, false).len(), 39);
        // -f suppresses the heading entirely (pinky | wc -c == 0 probed).
    }

    #[test]
    fn idle_string_faces() {
        assert_eq!(idle_string(-1), "?????");
        assert_eq!(idle_string(0), "     ");
        assert_eq!(idle_string(59), "     ");
        assert_eq!(idle_string(60), "00:01");
        assert_eq!(idle_string(3661), "01:01");
        assert_eq!(idle_string(24 * 60 * 60), "1d");
        assert_eq!(idle_string(3 * 24 * 60 * 60 + 61), "3d");
    }

    #[test]
    fn long_entry_resolved_current_user() {
        let out = long_entry("cmx", true, "cmx", "BOX");
        assert!(out.starts_with("Login name: cmx"));
        assert!(out.contains("In real life:  BOX\\cmx\n"));
        assert!(out.contains("Directory: C:/Users/cmx"));
        assert!(out.contains("Shell:  /usr/bin/bash\n"));
        assert!(out.ends_with("\n\n"));
        // "Login name: " is 12 bytes; the name field is %-28s.
        assert_eq!(out.find("In real life:"), Some(40));
    }

    #[test]
    fn long_entry_unresolved_early_return() {
        // GNU print_long_entry: " ???" then return — no Directory/Shell
        // and no trailing blank line (probed `pinky -l root nosuchuser99`).
        let out = long_entry("nosuchuser99", true, "cmx", "BOX");
        assert_eq!(
            out,
            "Login name: nosuchuser99                In real life:  ???\n"
        );
        let no_shell = long_entry("other", false, "cmx", "BOX");
        assert!(!no_shell.contains("Directory:"));
        assert_eq!(no_shell.find("\n\n"), None);
    }

    #[test]
    fn long_entry_without_domain() {
        let out = long_entry("cmx", false, "cmx", "");
        assert!(out.contains("In real life:  cmx\n"));
        assert!(!out.contains("Directory:"));
        assert!(out.ends_with("\n\n"));
    }

    #[test]
    fn short_row_shape() {
        // Row bytes mirror the C++ model: %-8s user, right-aligned ???
        // Name cell, '?' mesg, console line, unknown idle, time field,
        // rstripped (the Where column never fills). Byte layout:
        // %-8s user | " " + %-19s-free right justify | " ?" | %-8s console
        // | " " + %-6s idle | " " + time — trailing pad never survives.
        let row = short_row("cmx", -1, -1, true, true);
        let expect = format!(
            "cmx     {}??? ?console  ?????\n",
            " ".repeat(1 + 16) // column separator + right-justify(???, 19)
        );
        assert_eq!(row, expect);
        // With the name and idle columns off, the row shortens to the
        // rstripped user/mesg/line core (the empty time field's separator
        // space is trailing and does not survive).
        let bare = short_row("cmx", -1, -1, false, false);
        assert_eq!(bare, "cmx      ?console\n");
    }

    // ===== surface behaviors (argv order, usage, help/version) =====

    #[test]
    fn long_format_requires_a_user() {
        // Probed: `pinky -l` -> stderr diagnostic + try-help, rc 1.
        let (r, out, err) = run(&["-l"], "");
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains(
            "pinky: no username specified; at least one must be specified when using -l"
        ));
        assert!(err.contains("Try 'pinky --help' for more information."));
        let (r, _, _) = run(&["-l", "-b"], "");
        assert_eq!(r, 1);
    }

    #[test]
    fn last_wins_between_s_and_l() {
        // GNU main assigns per option: `-l -s` is short format, `-s -l`
        // long. The long face with an unknown user prints " ???" only.
        let (r, out, _) = run(&["-s", "-l", "nosuchuser99"], "");
        assert_eq!(r, 0);
        assert!(out.contains("Login name: nosuchuser99"));
        assert!(out.contains("In real life:  ???"));
        // `-l -s` is the short face: with a filter that matches nothing the
        // output is the heading (win-mode content; here only the rc and the
        // absence of long-format text are pinned).
        let (r, out, _) = run(&["-l", "-s", "nosuchuser99"], "");
        assert_eq!(r, 0);
        assert!(!out.contains("Login name:"));
    }

    #[test]
    fn dashdash_and_bundling() {
        // Probed: `pinky --` behaves like plain pinky (rc 0); bundled
        // `-lb root` keeps both flags (long face, -b no Directory).
        let (r, _, _) = run(&["--"], "");
        assert_eq!(r, 0);
        let (r, out, _) = run(&["-s", "-lb", "nosuchuser99"], "");
        assert_eq!(r, 0);
        assert!(out.contains("Login name: nosuchuser99"));
        assert!(!out.contains("Directory:"));
    }

    #[test]
    fn bogus_options_are_usage_errors() {
        // Probed: `pinky -Z` -> "invalid option -- 'Z'" rc 1;
        // `pinky --lookup root` -> "unrecognized option '--lookup'" rc 1.
        let (r, out, err) = run(&["-Z"], "");
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("pinky: invalid option -- 'Z'"));
        let (r, _, err) = run(&["--lookup", "root"], "");
        assert_eq!(r, 1);
        assert!(err.contains("pinky: unrecognized option '--lookup'"));
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err) = run(&["--help"], "");
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: pinky [OPTION]... [USER]...\n"));
        assert!(
            out.contains("  -l              produce long format output for the specified USERs\n")
        );
        assert!(out.contains("                  in short format\n"));
        assert!(out.contains("The utmp file will be /var/run/utmp.\n"));
        assert!(out.ends_with("info '(coreutils) pinky invocation'\n"));
        let (r, out, _) = run(&["--version"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("pinky (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Joseph Arceneaux, David MacKenzie, and Kaveh Ghazi.\n"));
    }

    #[test]
    fn help_short_circuits_later_tokens() {
        // #1573 sequential contract; also a long-option abbreviation.
        let (r, out, _) = run(&["--help", "--bogus"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: pinky"));
        let (r, out, _) = run(&["--hel"], "");
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: pinky"));
        let (r, _, _) = run(&["--bogus", "--help"], "");
        assert_eq!(r, 1);
    }

    #[test]
    fn stdin_ignored_on_success_surface() {
        // The command never reads stdin bytes; a pending buffer stays.
        let (r, _, _) = run(&["--"], "unused stdin content");
        assert_eq!(r, 0);
    }
}
