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
use crate::gnuhelp;
use crate::winsys;
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[];

// `--help` body verbatim from `env whoami --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const WHOAMI_HELP_BODY: &str = r#"Usage: whoami [OPTION]...
Print the user name associated with the current effective user ID.
Same as id -un.

      --help        display this help and exit
      --version     output version information and exit

"#;

pub fn whoami_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // GNU whoami registers no -h short option, so a bare `-h` is
    // "invalid option -- 'h'" rc 1 (not --help); --help stays valid.
    if args.iter().any(|a| a.to_string_lossy() == "-h") {
        // [GNU] whoami registers no -h short option; the wording is the
        // shared getopt diagnostic (whoami.c:317 error()).
        let _ = usage_error(ctx, io, "whoami", &argopt::invalid_option(ctx, "h"));
        return Ok(1);
    }
    let p = match argopt::parse("whoami", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    // GNU exits at the first --help/--version getopt_long encounters; the
    // ordered find reproduces that for `--version --help` (version wins).
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
                    WHOAMI_HELP_BODY,
                    gnuhelp::footer("whoami", "whoami invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("whoami", "Richard Mlynarik")
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
            "whoami",
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
            // [GNU] whoami.c: error(1, 0, "cannot find name of user ID") —
            // the program prefix is the error() frame, the inner wording is
            // the translatable unit.
            let _ = writeln!(
                io.stderr,
                "whoami: {}",
                tr!(
                    ctx,
                    "command.whoami.error.cannot_find_name",
                    "cannot find name of user ID"
                )
            );
            Ok(1)
        }
    }
}

#[cfg(test)]
mod tests {
    // [GNU] `--help`/`--version` print the probed coreutils text, rc 0
    // (strict rows wa05/wa06).
    #[test]
    fn gnu_help_version_surface() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("whoami-hv");
        let ctx = sc.ctx();
        let args = ["--help".into()];
        assert_eq!(super::whoami_cmd(&args, &mut io.split(), &ctx).unwrap(), 0);
        assert!(io.out().starts_with("Usage: whoami [OPTION]...\n"));
        assert!(io.out().ends_with("info '(coreutils) whoami invocation'\n"));
        let mut io = MemIo::new("");
        let args = ["--version".into()];
        assert_eq!(super::whoami_cmd(&args, &mut io.split(), &ctx).unwrap(), 0);
        assert!(io.out().starts_with("whoami (GNU coreutils) 9.4\n"));
        assert!(io.out().ends_with("Written by Richard Mlynarik.\n"));
    }

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

    // The tr! seam end to end: with a host catalog injected the catalog text
    // (including the named {operand} placeholder) wins; with the default
    // empty host the compiled English fallback keeps the gate-recorded bytes.
    #[test]
    fn catalog_injection_translates_extra_operand() {
        use winux_core::host::MapHost;
        use winux_core::{Ctx, MapEnv};

        let sc = ScratchCtx::new("whoami-i18n");
        let mut host = MapHost::default();
        // The shared key's catalog text stays positioned for the C++ side;
        // the Rust rendering maps it back onto the named binding.
        host.messages.insert(
            "common.error.extra_operand".to_string(),
            "多余的参数 '{}'".to_string(),
        );
        let env = MapEnv::new();
        let ctx = Ctx::with_host(sc.dir.clone(), &env, &host);
        let args: Vec<OsString> = vec!["extra".into()];
        let mut io = MemIo::new("");
        let r = super::whoami_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("多余的参数 'extra'"), "got {}", io.err());
    }
}
