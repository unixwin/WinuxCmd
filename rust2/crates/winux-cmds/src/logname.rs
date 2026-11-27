//! `logname` — GNU coreutils semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Prints the current user's login name (via `winsys::imp::user_name()`,
//! matching `GetUserNameW` in the 1.x C++ applet) and exits 0.
//! Rejects any operand (`extra operand 'X'`) and unrecognized options with rc 1.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use crate::winsys;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[];

const LOGNAME_HELP_BODY: &str = r#"Usage: logname [OPTION]...
Print the user's login name.

      --help        display this help and exit
      --version     output version information and exit

"#;

pub fn logname_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    if args.iter().any(|a| a.to_string_lossy() == "-h") {
        let _ = usage_error(ctx, io, "logname", &argopt::invalid_option(ctx, "h"));
        return Ok(1);
    }
    let p = match argopt::parse("logname", args, OPTS, io, ctx, 1) {
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
                    LOGNAME_HELP_BODY,
                    gnuhelp::footer("logname", "logname invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("logname", "FIXME: unknown")
                )?;
                0
            }
        });
    }
    if let Some(first) = p.positionals.first() {
        let shown = first.to_string_lossy().into_owned();
        let _ = usage_error(
            ctx,
            io,
            "logname",
            &crate::messages::extra_operand(ctx, &shown),
        );
        return Ok(1);
    }
    match winsys::imp::user_name() {
        Some(name) => {
            writeln!(io.stdout, "{name}")?;
            Ok(0)
        }
        None => {
            let _ = writeln!(io.stderr, "logname: no login name");
            Ok(1)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn logname_help_version_and_extra_operand() {
        let sc = ScratchCtx::new("logname-test");
        let ctx = sc.ctx();

        let mut io_help = MemIo::new("");
        let rc = logname_cmd(&[OsString::from("--help")], &mut io_help.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert!(io_help.out().starts_with("Usage: logname [OPTION]...\n"));

        let mut io_ver = MemIo::new("");
        let rc = logname_cmd(&[OsString::from("--version")], &mut io_ver.split(), &ctx).unwrap();
        assert_eq!(rc, 0);
        assert!(io_ver.out().starts_with("logname "));

        let mut io_extra = MemIo::new("");
        let rc = logname_cmd(&[OsString::from("extra")], &mut io_extra.split(), &ctx).unwrap();
        assert_eq!(rc, 1);
        assert!(io_extra.out().is_empty());
        assert!(io_extra.err().contains("extra operand 'extra'"));
        assert!(io_extra.err().contains("Try 'logname --help'"));
    }

    #[test]
    fn logname_rejects_short_h() {
        let sc = ScratchCtx::new("logname-short-h");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let rc = logname_cmd(&[OsString::from("-h")], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 1);
        assert!(io.err().contains("invalid option -- 'h'"));
    }
}
