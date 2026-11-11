//! `users` — GNU coreutils users semantics on the Windows value model.
//!
//! GNU users.c prints one login name per USER_PROCESS utmp entry, sorted,
//! space-separated on a single line; duplicate names are kept (one per
//! session). Windows has no utmp; the 1.x C++ applet reads the WTS session
//! table and falls back to the current interactive user when no session is
//! enumerable. This library form follows that model with the environment
//! face (USERNAME) as the session source: one name, sorted trivially.
//! When the environment carries no user name, GNU's no-entries output (a
//! bare newline) is printed.
//!
//! Operand behavior follows GNU users.c: at most one operand (the utmp
//! FILE). GNU 9.4 opens the file for reading and, when it cannot, simply
//! prints no entries (probed: `users nosuch123` prints an empty line, rc 0
//! under WSL coreutils 9.4) — so the FILE operand never fails here either.
//! A second operand is `users: extra operand 'b'` plus try-help, rc 1.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use winux_core::{usage_error, CmdResult, Ctx, Io};

pub fn users_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // getopt: unknown options are usage errors; --help/--version win.
    let opts = [
        argopt::OptSpec {
            short: Some('v'),
            long: "version",
            takes_value: false,
        },
        argopt::OptSpec {
            short: Some('V'),
            long: "version",
            takes_value: false,
        },
    ];
    let p = match argopt::parse("users", args, &opts, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    let args: Vec<OsString> = p.positionals.clone();
    if args.len() > 1 {
        let _ = usage_error(
            io,
            "users",
            &format!("extra operand '{}'", args[1].to_string_lossy()),
        );
        return Ok(1);
    }
    // The FILE operand replaces utmp on this platform: its records are not
    // parsed, and an unreadable file yields zero entries (GNU 9.4 probed).

    let mut names: Vec<String> = Vec::new();
    if let Some(user) = ctx.env.get("USERNAME") {
        let user = user.to_string_lossy().into_owned();
        if !user.is_empty() {
            names.push(user);
        }
    }
    names.sort();

    writeln!(io.stdout, "{}", names.join(" "))?;
    Ok(0)
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use std::sync::atomic::{AtomicU64, Ordering};

    static SEQ: AtomicU64 = AtomicU64::new(0);

    fn scratch(tag: &str) -> winux_core::test_support::ScratchCtx {
        let n = SEQ.fetch_add(1, Ordering::SeqCst);
        winux_core::test_support::ScratchCtx::new(&format!("{}-{}", tag, n))
    }
    use winux_core::test_support::{MemIo, ScratchCtx};
    use winux_core::Ctx;
    use winux_core::MapEnv;

    fn run_with(env: &MapEnv, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = scratch("users");
        let ctx = Ctx {
            cwd: sc.dir.clone(),
            env,
        };
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::users_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn prints_the_current_user() {
        let mut env = MapEnv::new();
        env.set("USERNAME", "alice");
        let (r, out, _) = run_with(&env, &[]);
        assert_eq!((r, out.as_str()), (0, "alice\n"));
    }

    #[test]
    fn no_entries_prints_bare_newline() {
        let env = MapEnv::new();
        let (r, out, _) = run_with(&env, &[]);
        assert_eq!((r, out.as_str()), (0, "\n"));
    }

    #[test]
    fn file_operand_accepted() {
        let mut env = MapEnv::new();
        env.set("USERNAME", "alice");
        let (r, out, _) = run_with(&env, &["some-utmp-file"]);
        assert_eq!((r, out.as_str()), (0, "alice\n"));
        // a missing file is still not an error (GNU 9.4 probed)
        let (r, out, _) = run_with(&env, &["users-nosuch-file"]);
        assert_eq!((r, out.as_str()), (0, "alice\n"));
    }

    #[test]
    fn extra_operand_is_error() {
        let mut env = MapEnv::new();
        env.set("USERNAME", "alice");
        let (r, _, err) = run_with(&env, &["a", "b"]);
        assert_eq!(r, 1);
        assert!(err.contains("users: extra operand 'b'"));
        assert!(err.contains("Try 'users --help' for more information."));
    }
}
