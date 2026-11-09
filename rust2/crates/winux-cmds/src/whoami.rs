//! `whoami` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! Prints the effective user name and a newline; takes no options beyond
//! --help/--version (both rc 0). Any operand is
//! `whoami: extra operand 'X'` + usage on stderr, rc 1 (WinuxCmd #1215
//! parity). An unrecognized option is rc 1, not rc 2.
//!
//! Windows value model: the current user name via GetUserNameW (winsys) —
//! the same source the 1.x C++ applet uses (`src/commands/whoami.cpp`).
//! The gate compares GNU rc where platform-neutral and Rust-vs-C++ rc for
//! the platform-inherent name itself (win mode).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::winsys;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[];

pub fn whoami_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // GNU whoami registers no -h short option, so a bare `-h` is
    // "invalid option -- 'h'" rc 1 (not --help); --help stays valid.
    if args.iter().any(|a| a.to_string_lossy() == "-h") {
        let _ = usage_error(io, "whoami", "invalid option -- 'h'");
        return Ok(1);
    }
    let p = match argopt::parse("whoami", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if let Some(first) = p.positionals.first() {
        let shown = first.to_string_lossy().into_owned();
        let _ = usage_error(io, "whoami", &format!("extra operand '{shown}'"));
        return Ok(1);
    }
    match winsys::imp::user_name() {
        Some(name) => {
            writeln!(io.stdout, "{name}")?;
            Ok(0)
        }
        None => {
            let _ = writeln!(io.stderr, "whoami: cannot find name of user ID");
            Ok(1)
        }
    }
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn prints_user_name() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("whoami");
        let ctx = sc.ctx();
        let r = super::whoami_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        // The name is platform-inherent but must be non-empty and newline
        // terminated in every environment the tests can run in.
        assert!(io.out().ends_with('\n'));
        assert!(!io.out().is_empty());
    }

    #[test]
    fn extra_operand_rc1() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("whoami2");
        let ctx = sc.ctx();
        let args: Vec<OsString> = vec!["extra".into()];
        let r = super::whoami_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.out().is_empty());
        assert!(io.err().contains("extra operand 'extra'"));
        assert!(io.err().contains("Try 'whoami --help'"));
    }

    #[test]
    fn unknown_option_rc1_and_help() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("whoami3");
        let ctx = sc.ctx();
        let args: Vec<OsString> = vec!["--bogus".into()];
        let r = super::whoami_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);

        let mut io = MemIo::new("");
        let args: Vec<OsString> = vec!["--help".into()];
        let r = super::whoami_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
    }
}
