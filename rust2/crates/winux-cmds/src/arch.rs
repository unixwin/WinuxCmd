//! `arch` — GNU coreutils 9.4 arch (uname -m variant), probed byte-for-byte
//! against WSL coreutils 9.4 (2026-11-12).
//!
//! * Output is `Print machine architecture`: the native machine name. The
//!   C++ applet (src/commands/arch.cpp) and this implementation use
//!   GetNativeSystemInfo so a WOW64 process still reports the native
//!   architecture (x86_64/aarch64/...), like GNU reports its own host.
//!   Content is platform-inherent: value rows gate `win` (Rust rc vs C++
//!   rc); rc-only rows (bad options, extra operands) gate `exit`.
//! * Argument grammar (getopt_long with ONLY long options help/version,
//!   probed): `-h`/`-V` are INVALID (`arch: invalid option -- 'h'` rc 1);
//!   long abbreviations work (`arch --ver` prints the version); `--` ends
//!   options; any operand after option processing reports
//!   `arch: extra operand '<x>'` rc 1 (options permute first, so
//!   `arch foo --help` still prints help — probed pattern of coreutils
//!   permutation, matching argopt).

use std::ffi::OsString;

use crate::argopt;
use crate::gnuhelp;
use crate::winsys;
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

// No entries: GNU arch's getopt table has only the virtual --help/--version
// long options (no short options at all — `-h` is invalid there).
const OPTS: &[argopt::OptSpec] = &[];

// `--help` body verbatim from `env arch --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const ARCH_HELP_BODY: &str = r#"Usage: arch [OPTION]...
Print machine architecture.

      --help        display this help and exit
      --version     output version information and exit

"#;

pub fn arch_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("arch", args, OPTS, io, ctx, 1) {
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
                    ARCH_HELP_BODY,
                    gnuhelp::footer("arch", "arch invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("arch", "David MacKenzie and Karel Zak")
                )?;
                0
            }
        });
    }
    // GNU arch.c: after the option loop, any remaining argv is an extra
    // operand -> error + usage, rc 1 (WinuxCmd #1215 parity).
    if let Some(first) = p.positionals.first() {
        let name = first.to_string_lossy();
        // [GNU] arch.c: extra operand + usage, rc 1 (WinuxCmd #1215 parity).
        let _ = usage_error(
            io,
            "arch",
            &tr!(
                ctx,
                "command.arch.error.extra_operand",
                "extra operand '{operand}'",
                operand = name
            ),
        );
        return Ok(1);
    }
    writeln!(io.stdout, "{}", winsys::imp::machine_arch())?;
    Ok(0)
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    use super::*;

    fn run(args: &[&str]) -> (u32, String, String) {
        let sc = ScratchCtx::new("arch");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = arch_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn prints_machine_arch() {
        let (r, out, err) = run(&[]);
        assert_eq!((r, err.as_str()), (0, ""));
        // platform-inherent content: non-empty, single line (win rows
        // compare rc only; GNU prints x86_64 under WSL)
        let v = out.trim_end();
        assert!(!v.is_empty() && !v.contains('\n'), "got {out}");
        assert_eq!(out, format!("{v}\n"));
    }

    #[test]
    fn help_version_surface() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: arch [OPTION]...\n"));
        assert!(out.ends_with("info '(coreutils) arch invocation'\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("arch (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie and Karel Zak.\n"));
        // long-option abbreviation (probed: arch --ver works)
        let (r, out, _) = run(&["--ver"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("arch (GNU coreutils) 9.4\n"));
        // options permute past operands (probed coreutils shape)
        let (r, out, _) = run(&["foo", "--help"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage:"));
        let (r, out, _) = run(&["--version", "--help"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("arch (GNU coreutils) 9.4\n"));
    }

    #[test]
    fn gnu_error_conditions() {
        // extra operand
        let (r, out, err) = run(&["foo"]);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(err.contains("arch: extra operand 'foo'"), "got {err}");
        assert!(err.contains("Try 'arch --help' for more information."));
        // two operands: the first is reported
        let (r, _, err) = run(&["a", "b"]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand 'a'"));
        // short options are invalid in GNU arch (no shorts in the table)
        let (r, _, err) = run(&["-x"]);
        assert_eq!(r, 1);
        assert!(err.contains("arch: invalid option -- 'x'"));
        // unknown long
        let (r, _, err) = run(&["--bogus"]);
        assert_eq!(r, 1);
        assert!(err.contains("arch: unrecognized option '--bogus'"));
        // `--` ends options: the next token is an operand
        let (r, _, err) = run(&["--", "--help"]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand '--help'"));
        let (r, out, _) = run(&["--"]);
        assert_eq!(r, 0);
        assert_eq!(out.lines().count(), 1);
        // NOTE: a lone `-h` maps to help in argopt (the shared pilot
        // deviation, classified `note` in the gate); GNU arch rejects it.
    }
}
