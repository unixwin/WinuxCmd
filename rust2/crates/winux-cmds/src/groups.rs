//! `groups` — GNU coreutils groups semantics on the Windows value model of
//! the 1.x C++ applet (`src/commands/groups.cpp` on main; probe base:
//! target/probe/g1.sh).
//!
//! GNU contract:
//! * `groups [OPTION]... [USERNAME]...`; with no USERNAME the current
//!   process's groups print as one space-joined line; with USER arguments
//!   each line is prefixed "user : " (probed: `groups root` → "root :
//!   root"). Any per-user failure exits 1 after all users are processed.
//! * An unknown user is "groups: 'U': no such user" (probed), rc 1.
//! * GNU has no -h short option ("invalid option -- 'h'"); --help /
//!   --version print the probed GNU text.
//!
//! Windows value model (C++ groups.cpp):
//! * Group membership comes from NetUserGetGroups (global group list),
//!   *not* the process token — the gate's win rows diff against the C++
//!   Debug exe carrying the same API choice.
//! * The well-known "None" primary group has no POSIX equivalent and
//!   prints as its numeric account id, resolved through
//!   LookupAccountNameW + the win32_account_id_from_sid derivation
//!   (RID; 0x30000+RID for S-1-5-21-... domain accounts; 0x60000+RID for
//!   integrity SIDs) — the #1024 rule, shared with id.rs's winsys model.
//! * Empty group names from the API are skipped; NetUserGetGroups reports
//!   NERR_UserNotFound (2221) as the GNU "no such user" wording; other
//!   API failures keep the system text (rc 1, win rows).

use std::ffi::OsString;
use std::io::Write as _;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    // GNU groups has no options besides --help/--version; -h is rejected.
    argopt::H_REJECT_SPEC,
];

const GROUPS_HELP_BODY: &str = r#"Usage: groups [OPTION]... [USERNAME]...
Print group memberships for each USERNAME or, if no USERNAME is specified, for
the current process (which may differ if the groups database has changed).
      --help        display this help and exit
      --version     output version information and exit

"#;

#[cfg(windows)]
mod ffi {
    pub const NERR_USER_NOT_FOUND: u32 = 2221;

    #[repr(C)]
    pub struct GroupUsersInfo0 {
        pub grui0_name: *const u16,
    }

    #[link(name = "netapi32")]
    #[link(name = "netapi32")]
    extern "system" {
        pub fn NetUserGetGroups(
            servername: *const u16,
            username: *const u16,
            level: u32,
            bufptr: *mut *mut u8,
            prefmaxlen: u32,
            entriesread: *mut u32,
            totalentries: *mut u32,
        ) -> u32;
        pub fn NetApiBufferFree(buffer: *mut u8) -> u32;
    }

    #[link(name = "advapi32")]
    #[link(name = "netapi32")]
    extern "system" {
        pub fn LookupAccountNameW(
            systemname: *const u16,
            accountname: *const u16,
            sid: *mut u8,
            cbsid: *mut u32,
            referenceddomain: *mut u16,
            cchdomain: *mut u32,
            use_kind: *mut u32,
        ) -> i32;
        pub fn IsValidSid(sid: *mut u8) -> i32;
        pub fn GetSidSubAuthorityCount(sid: *mut u8) -> *mut u8;
        pub fn GetSidSubAuthority(sid: *mut u8, index: u32) -> *mut u32;
        pub fn GetSidIdentifierAuthority(sid: *mut u8) -> *mut u8;
    }
}

#[cfg(windows)]
fn wide(s: &str) -> Vec<u16> {
    use std::os::windows::ffi::OsStrExt;
    std::ffi::OsStr::new(s)
        .encode_wide()
        .chain(std::iter::once(0))
        .collect()
}

#[cfg(windows)]
fn wide_to_string(buf: &[u16]) -> String {
    let len = buf.iter().position(|&c| c == 0).unwrap_or(buf.len());
    String::from_utf16_lossy(&buf[..len])
}

/// [C++ parity] win32_account_id_from_sid (src/utils/win32.cppm): the RID,
/// remapped 0x30000+RID for NT-domain accounts and 0x60000+RID for
/// integrity SIDs — the same derivation winsys::sid_to_account applies.
#[cfg(windows)]
fn account_id_from_sid(sid: *mut u8) -> String {
    unsafe {
        if ffi::IsValidSid(sid) == 0 {
            return String::new();
        }
        let count_ptr = ffi::GetSidSubAuthorityCount(sid);
        if count_ptr.is_null() || *count_ptr == 0 {
            return String::new();
        }
        let count = *count_ptr;
        let rid_ptr = ffi::GetSidSubAuthority(sid, u32::from(count - 1));
        if rid_ptr.is_null() {
            return String::new();
        }
        let rid = *rid_ptr;
        let authority = ffi::GetSidIdentifierAuthority(sid);
        let authority_value = if authority.is_null() {
            0u64
        } else {
            let b = std::slice::from_raw_parts(authority, 6);
            u64::from(b[0]) << 40
                | u64::from(b[1]) << 32
                | u64::from(b[2]) << 24
                | u64::from(b[3]) << 16
                | u64::from(b[4]) << 8
                | u64::from(b[5])
        };
        let first = ffi::GetSidSubAuthority(sid, 0);
        let first_subauth = if first.is_null() { 0 } else { *first };
        if authority_value == 5 && first_subauth == 21 && count >= 5 {
            (0x30000u64 + u64::from(rid)).to_string()
        } else if authority_value == 16 {
            (0x60000u64 + u64::from(rid)).to_string()
        } else {
            rid.to_string()
        }
    }
}

/// LookupAccountNameW over the shared SID model; None when unresolvable.
#[cfg(windows)]
fn lookup_account_id(name: &str) -> Option<String> {
    let wname = wide(name);
    let mut sid = [0u8; 68];
    let mut sid_size = sid.len() as u32;
    let mut domain = [0u16; 256];
    let mut domain_size = domain.len() as u32;
    let mut use_kind = 0u32;
    let ok = unsafe {
        ffi::LookupAccountNameW(
            std::ptr::null(),
            wname.as_ptr(),
            sid.as_mut_ptr(),
            &mut sid_size,
            domain.as_mut_ptr(),
            &mut domain_size,
            &mut use_kind,
        )
    };
    if ok == 0 {
        return None;
    }
    let id = account_id_from_sid(sid.as_mut_ptr());
    if id.is_empty() {
        None
    } else {
        Some(id)
    }
}

/// Collect the group entries for `user`: names, except the well-known
/// "None" group which prints as its numeric account id (#1024).
#[cfg(windows)]
fn collect_user_groups(user: &str) -> Result<Vec<String>, String> {
    let wuser = wide(user);
    let mut raw: *mut u8 = std::ptr::null_mut();
    let mut entries = 0u32;
    let mut total = 0u32;
    let status = unsafe {
        ffi::NetUserGetGroups(
            std::ptr::null(),
            wuser.as_ptr(),
            0,
            &mut raw,
            0xFFFF_FFFF, // MAX_PREFERRED_LENGTH
            &mut entries,
            &mut total,
        )
    };
    if status != 0 {
        let message = if status == ffi::NERR_USER_NOT_FOUND {
            format!("groups: '{user}': no such user")
        } else {
            format!("groups: {user}: system error {status}")
        };
        if !raw.is_null() {
            unsafe { ffi::NetApiBufferFree(raw) };
        }
        return Err(message);
    }
    let mut names = Vec::new();
    if entries > 0 && !raw.is_null() {
        let groups = raw as *const ffi::GroupUsersInfo0;
        for i in 0..entries as usize {
            let ptr = unsafe { (*groups.add(i)).grui0_name };
            if ptr.is_null() {
                continue;
            }
            // read the NUL-terminated wide name
            let mut len = 0usize;
            unsafe {
                while *ptr.add(len) != 0 {
                    len += 1;
                }
            }
            let slice = unsafe { std::slice::from_raw_parts(ptr, len) };
            let name = wide_to_string(slice);
            if name.is_empty() {
                continue;
            }
            if name.eq_ignore_ascii_case("None") {
                if let Some(id) = lookup_account_id(&name) {
                    names.push(id);
                }
                continue;
            }
            names.push(name);
        }
    }
    if !raw.is_null() {
        unsafe { ffi::NetApiBufferFree(raw) };
    }
    Ok(names)
}

#[cfg(not(windows))]
fn collect_user_groups(_user: &str) -> Result<Vec<String>, String> {
    Ok(Vec::new())
}

/// One output line: `user : g1 g2 ...` with the prefix only when a
/// username argument was given (GNU groups.c formatting).
fn format_line(user: Option<&str>, names: &[String]) -> String {
    let mut line = match user {
        Some(u) => format!("{u} : "),
        None => String::new(),
    };
    line.push_str(&names.join(" "));
    line
}

pub fn groups_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("groups", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(status) = argopt::rejected_short_h(io, "groups", &p, 1) {
        return Ok(status);
    }
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
                    GROUPS_HELP_BODY,
                    gnuhelp::footer("groups", "groups invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("groups", "David MacKenzie and James Youngman")
                )?;
                0
            }
        });
    }

    let mut exit_code = 0;
    if p.positionals.is_empty() {
        // Current user: GetUserNameW (the C++ model); the line has no
        // prefix.
        let current = crate::winsys::imp::user_name();
        match current {
            Some(user) if !user.is_empty() => match collect_user_groups(&user) {
                Ok(names) => {
                    let _ = writeln!(io.stdout, "{}", format_line(None, &names));
                }
                Err(message) => {
                    let _ = writeln!(io.stderr, "{message}");
                    exit_code = 1;
                }
            },
            _ => {
                exit_code = 1;
            }
        }
        return Ok(exit_code);
    }

    for operand in &p.positionals {
        let user = operand.to_string_lossy().into_owned();
        match collect_user_groups(&user) {
            Ok(names) => {
                let _ = writeln!(io.stdout, "{}", format_line(Some(&user), &names));
            }
            Err(message) => {
                let _ = writeln!(io.stderr, "{message}");
                exit_code = 1;
            }
        }
    }
    Ok(exit_code)
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("groups");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::groups_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn current_user_prints_one_line() {
        // Windows value model: the current user's group list (usually the
        // "None" group as its numeric id). Shape-gated: one non-empty
        // line, no prefix.
        let (r, out, err) = run(&[]);
        assert_eq!(err, "");
        if r == 0 {
            let line = out.trim_end();
            assert!(!line.is_empty(), "out={out}");
            assert!(!line.contains(" : "), "prefixed unexpectedly: {out}");
        }
    }

    #[test]
    fn unknown_user_is_gnu_worded() {
        // GNU probed: groups: 'nosuchuser123': no such user, rc 1
        let (r, out, err) = run(&["nosuchuser123"]);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(
            err.contains("groups: 'nosuchuser123': no such user"),
            "{err}"
        );
    }

    #[test]
    fn multiple_users_each_get_a_prefix() {
        let (r, out, err) = run(&["nosuch-a-1", "nosuch-b-2"]);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(err.contains("'nosuch-a-1'"));
        assert!(err.contains("'nosuch-b-2'"));
        // a valid user (the current one) still prints with the prefix
        let current = crate::winsys::imp::user_name().unwrap_or_default();
        if !current.is_empty() {
            let (r, out, _) = run(&[&current]);
            if r == 0 {
                assert!(out.starts_with(&format!("{current} : ")), "{out}");
            }
        }
    }

    #[test]
    fn gnu_help_version_surface() {
        let (r, out, _) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: groups [OPTION]... [USERNAME]...\n"));
        assert!(out.ends_with("info '(coreutils) groups invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("groups (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie and James Youngman.\n"));
        // GNU groups has no -h
        let (r, _, err) = run(&["-h"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid option -- 'h'"));
        let (r, _, err) = run(&["-x"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid option -- 'x'"));
    }
}
