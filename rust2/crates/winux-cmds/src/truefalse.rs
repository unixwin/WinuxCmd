//! `true` / `false` — GNU coreutils semantics.
//!
//! GNU true(1)/false(1) never run getopt: every argument is ignored (even
//! `--bogus`), and `--help` / `--version` are honored only when one of them
//! is the SOLE argument (coreutils true.c: `if (argc == 2) ...`). The exit
//! status is always the command's own — `false --help` prints the help and
//! still exits 1 (true.c built as false.c with `EXIT_STATUS` =
//! `EXIT_FAILURE`). Probe evidence (WSL coreutils 9.4, #1488 wiring wave):
//! `false --help` -> rc 1 + text; `true --help x` / `true x --help` -> rc 0,
//! no output; `POSIXLY_CORRECT=1 true --help` still prints (9.4 has no
//! POSIXLY_CORRECT gate here).
//!
//! In winuxcmd.exe the dispatcher intercepts the sole `--help`/`--version`
//! before this function runs (product-branded help/version text, shared by
//! every applet), so on the product path only the "ignore everything"
//! branch is reachable; the help/version branch serves the standalone
//! driver and the in-process ABI.

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
    if let [only] = args {
        let s = only.to_string_lossy();
        if s == "--help" {
            write!(io.stdout, "{}", help_text(prog))?;
            return Ok(status);
        }
        if s == "--version" {
            write!(io.stdout, "{}", version_line(prog))?;
            return Ok(status);
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

    // [GNU] false --help / --version print, then return EXIT_STATUS (1).
    #[test]
    fn false_help_version_keep_failure_status() {
        let sc = ScratchCtx::new("falsehv");
        let ctx = sc.ctx();
        for opt in ["--help", "--version"] {
            let mut io = MemIo::new("");
            let r = super::false_cmd(&[opt.into()], &mut io.split(), &ctx).unwrap();
            assert_eq!(r, 1, "false {opt}");
            assert!(!io.out().is_empty(), "false {opt} prints");
        }
    }

    // [GNU] --help/--version count only as the sole argument; anywhere
    // else they are operands to ignore (true.c `argc == 2`).
    #[test]
    fn help_version_not_sole_are_ignored() {
        let sc = ScratchCtx::new("tfnotsole");
        let ctx = sc.ctx();
        let cases: [&[&str]; 4] = [
            &["--help", "x"],
            &["x", "--help"],
            &["--version", "x"],
            &["--help", "--version"],
        ];
        for args in cases {
            let args: Vec<std::ffi::OsString> = args.iter().map(|s| s.into()).collect();
            let mut io = MemIo::new("");
            assert_eq!(super::true_cmd(&args, &mut io.split(), &ctx).unwrap(), 0);
            assert_eq!(io.out(), "");
            let mut io = MemIo::new("");
            assert_eq!(super::false_cmd(&args, &mut io.split(), &ctx).unwrap(), 1);
            assert_eq!(io.out(), "");
        }
    }
}
