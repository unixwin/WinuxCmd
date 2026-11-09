//! `true` / `false` — GNU coreutils semantics.
//!
//! GNU true(1)/false(1) ignore every argument except `--help` / `--version`
//! (even `--bogus` is silently ignored) and exit with their fixed status.

use std::ffi::OsString;

use winux_core::{CmdResult, Ctx, Io};

fn help_text(prog: &str) -> String {
    format!(
        "Usage: {prog} [ignored command line arguments]\n  \
or:  {prog} OPTION\n\
Exit with a status code indicating success.\n\
\n\
--help     display this help and exit\n\
--version  output version information and exit\n\
\n\
NOTE: your shell may have its own version of {prog}, which usually supersedes \
the version described here.  Please refer to your shell's documentation for \
details about the options it supports.\n"
    )
}

fn version_line(prog: &str) -> String {
    format!("{prog} (WinuxCmd) 0.1.0\n")
}

fn run(prog: &str, status: u32, args: &[OsString], io: &mut Io) -> CmdResult {
    for a in args {
        let s = a.to_string_lossy();
        if s == "--help" {
            write!(io.stdout, "{}", help_text(prog))?;
            return Ok(0);
        }
        if s == "--version" {
            write!(io.stdout, "{}", version_line(prog))?;
            return Ok(0);
        }
    }
    Ok(status)
}

/// `true` — exit 0 regardless of arguments.
pub fn true_cmd(args: &[OsString], io: &mut Io, _ctx: &Ctx) -> CmdResult {
    run("true", 0, args, io)
}

/// `false` — exit 1 regardless of arguments.
pub fn false_cmd(args: &[OsString], io: &mut Io, _ctx: &Ctx) -> CmdResult {
    run("false", 1, args, io)
}

#[cfg(test)]
mod tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn true_ignores_everything() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("true");
        let ctx = sc.ctx();
        let args = ["--bogus".into(), "extra".into()];
        let r = super::true_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "");
        assert_eq!(io.err(), "");
    }

    #[test]
    fn false_status_one() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("false");
        let ctx = sc.ctx();
        let args: Vec<std::ffi::OsString> = vec![];
        let r = super::false_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
    }

    #[test]
    fn help_and_version() {
        let sc = ScratchCtx::new("truev");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let r = super::true_cmd(&["--help".into()], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().contains("Usage: true"));
        let mut io = MemIo::new("");
        let r = super::true_cmd(&["--version".into()], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().starts_with("true (WinuxCmd)"));
    }
}
