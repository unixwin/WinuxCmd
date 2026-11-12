//! `groups` — GNU coreutils 9.4 groups output shape with the Windows value
//! model of the 1.x C++ applet (src/commands/groups.cpp; probed against
//! WSL coreutils 9.4 for rc and wording, 2026-11-12).
//!
//! * No operands: print the current user's group names space-separated
//!   (GNU `groups` self form, no `<user> : ` prefix). Operands: one line
//!   per user as `<user> : <g1> <g2> ...` (spaces around the colon),
//!   processed left to right; any failure makes rc 1 while later users
//!   are still processed (probed with a mixed valid/invalid list).
//! * Membership data source: NetUserGetGroups (level 0), exactly the C++
//!   pipeline's source. The Windows well-known "None" group has no POSIX
//!   equivalent and prints as its numeric SID-derived id like an
//!   unresolvable gid (#1024) — same as the C++ applet. Content is
//!   platform-inherent: value rows gate `win` (Rust rc vs C++ rc); rc-only
//!   error rows (unknown users) gate `exit` (GNU rc 1, wording matches).
//! * Unknown user: `groups: '<user>': no such user` on stderr (GNU
//!   wording; ASCII quotes on our stderr like the C++ applet). An empty
//!   operand is also an unknown user (probed GNU rc 1).
//! * Argument grammar: getopt_long GNU-shaped with only the virtual
//!   --help/--version longs (no shorts; `-h` maps to help in argopt — the
//!   shared pilot deviation classified `note`, GNU rejects it). `--` ends
//!   options (`groups --` prints the self line, probed rc 0).

use std::ffi::OsString;

use crate::argopt;
use crate::gnuhelp;
use crate::winsys;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[argopt::OptSpec] = &[];

// `--help` body verbatim from `env groups --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const GROUPS_HELP_BODY: &str = r#"Usage: groups [OPTION]... [USERNAME]...
Print group memberships for each USERNAME or, if no USERNAME is specified, for
the current process (which may differ if the groups database has changed).
      --help        display this help and exit
      --version     output version information and exit

"#;

/// The Windows well-known "None" primary group: no POSIX equivalent, so
/// print its numeric id like an unresolvable gid (#1024), matching the
/// C++ applet's is_none_group_name + win32_lookup_account fallback.
fn display_name(name: &str) -> String {
    if name.eq_ignore_ascii_case("None") {
        if let Some(id) = winsys::imp::account_id_from_name(name) {
            if !id.is_empty() {
                return id;
            }
        }
    }
    name.to_string()
}

pub fn groups_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let _ = ctx;
    let p = match argopt::parse("groups", args, OPTS, io, ctx, 1) {
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

    let mut failed = false;
    let out = &mut *io.stdout;
    let err = &mut *io.stderr;
    if p.positionals.is_empty() {
        let Some(user) = winsys::imp::user_name() else {
            return Ok(1);
        };
        failed |= !print_user(out, err, &user, false);
    } else {
        for operand in &p.positionals {
            let name = operand.to_string_lossy().into_owned();
            if !print_user(out, err, &name, true) {
                failed = true;
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// One `<user> : <groups>` (prefixed) or bare-names line; false on
/// membership lookup failure (GNU wording to stderr, processing of the
/// remaining users continues).
fn print_user(
    out: &mut dyn std::io::Write,
    err: &mut dyn std::io::Write,
    user: &str,
    prefixed: bool,
) -> bool {
    match winsys::imp::group_names_of_user(user) {
        Ok(names) => {
            let line = names
                .iter()
                .map(|n| display_name(n))
                .collect::<Vec<_>>()
                .join(" ");
            if prefixed {
                let _ = writeln!(out, "{user} : {line}");
            } else {
                let _ = writeln!(out, "{line}");
            }
            true
        }
        Err(winsys::imp::NERR_USER_NOT_FOUND) => {
            let _ = writeln!(err, "groups: '{user}': no such user");
            false
        }
        Err(code) => {
            let _ = writeln!(err, "groups: {user}: netapi error {code}");
            false
        }
    }
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    use super::*;

    fn run(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("groups");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = groups_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: groups [OPTION]... [USERNAME]...\n"));
        assert!(out.ends_with("info '(coreutils) groups invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("groups (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie and James Youngman.\n"));
        // options permute; first of help/version wins (probed getopt order)
        let (r, out, _) = run(&["--version", "--help"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("groups (GNU coreutils) 9.4\n"));
    }

    #[test]
    fn self_line_shape() {
        // self form: bare group names, no `<user> : ` prefix; rc 0
        let (r, out, err) = run(&[]);
        assert_eq!(r, 0, "self lookup should succeed ({err})");
        assert!(!out.is_empty(), "expected a group-name line");
        assert!(
            !out.contains(" : "),
            "self form must not be prefixed: {out}"
        );
        assert_eq!(out.lines().count(), 1);
    }

    #[test]
    fn unknown_user_error() {
        // GNU probed: `groups: '<user>': no such user` rc 1 (also for the
        // empty name); later users still processed.
        let (r, out, err) = run(&["nosuchuser_wave6"]);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(
            err.contains("groups: 'nosuchuser_wave6': no such user"),
            "got {err}"
        );
        let (r, _, err) = run(&[""]);
        assert_eq!(r, 1);
        assert!(err.contains("groups: '': no such user"));
    }

    #[test]
    fn option_grammar() {
        // `--` ends options: bare `--` is the self form (probed rc 0)
        let (r, out, _) = run(&["--"]);
        assert_eq!(r, 0);
        assert!(!out.contains(" : "));
        // `-- x` treats x as a username operand
        let (r, _, err) = run(&["--", "nosuchuser_wave6"]);
        assert_eq!(r, 1);
        assert!(err.contains("no such user"));
        // invalid option
        let (r, _, err) = run(&["-x"]);
        assert_eq!(r, 1);
        assert!(err.contains("groups: invalid option -- 'x'"));
        assert!(err.contains("Try 'groups --help' for more information."));
        let (r, _, err) = run(&["--bogus"]);
        assert_eq!(r, 1);
        assert!(err.contains("groups: unrecognized option '--bogus'"));
    }

    #[test]
    fn display_name_none_fallback() {
        // Non-"None" names pass through; the None branch needs a live
        // LookupAccountNameW (no deterministic unit data), so only assert
        // the pass-through side here.
        assert_eq!(display_name("Users"), "Users");
        assert_eq!(display_name("some group"), "some group");
    }
}
