//! `hostid` — GNU coreutils hostid semantics on the Windows value model.
//!
//! GNU hostid prints a 32-bit identifier for the current host in 8
//! lowercase hex digits (hostid.c: get_machine_id formatted "%08lx"). The
//! 1.x C++ applet derives it from the MachineGuid registry value hashed to
//! 32 bits; this library form keeps the same value model — a stable
//! per-machine 32-bit quantity — sourced from the machine identity the
//! Windows environment carries (COMPUTERNAME, FNV-1a 64 hashed, lower 32
//! bits). The printed value therefore differs from the 1.x exe (different
//! hash input) but is equally stable for a given machine; exit status and
//! shape (8 lowercase hex digits) match.
//!
//! GNU rejects any operand: `hostid: extra operand 'X'` plus the try-help
//! line, rc 1.
//!
//! hostid declares no options of its own: main() delegates to
//! `parse_gnu_standard_options_only` (hostid.c:63-65), which accepts only the
//! long `--help` / `--version`. There is no short help or version letter, so
//! `-h` / `-V` / `-v` are getopt's `invalid option -- 'x'` usage errors,
//! rc 1. The shared `argopt::H_REJECT_SPEC` marker keeps the shared parser
//! from hijacking a bare `-h` into `--help`, and `rejected_short_h` reports
//! it. `--help` / `--version` print the probed GNU 9.4 text (help body plus
//! `gnuhelp::footer`, version via `gnuhelp::coreutils_version`); the first
//! one in argv order wins.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

/// FNV-1a 64 over the machine name; lower 32 bits are the host id.
fn machine_id_hex(ctx: &Ctx) -> String {
    let name = ctx
        .env
        .get("COMPUTERNAME")
        .map(|v| v.to_string_lossy().into_owned())
        .unwrap_or_default();
    let mut hash: u64 = 0xcbf2_9ce4_8422_2325;
    for b in name.as_bytes() {
        hash ^= u64::from(*b);
        hash = hash.wrapping_mul(0x0000_0100_0000_01b3);
    }
    format!("{:08x}", (hash & 0xFFFF_FFFF) as u32)
}

/// `--help` body verbatim from `env hostid --help` under WSL coreutils 9.4,
/// minus the shared gnuhelp::footer tail.
const HOSTID_HELP_BODY: &str = r#"Usage: hostid [OPTION]
Print the numeric identifier (in hexadecimal) for the current host.

      --help        display this help and exit
      --version     output version information and exit

"#;

pub fn hostid_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    // GNU hostid registers no -h short option: a bare `-h` is getopt's
    // "invalid option -- 'h'" (hostid.c:63-65, empty optstring), not --help.
    // The marker spec keeps argopt's bare-h -> --help mapping from firing;
    // rejected_short_h reports it before the help/version branch, so
    // `--help -h` still prints help (first help/version in argv order wins,
    // the #1573 sequential contract) while `-h --help` fails rc 1.
    let opts: [OptSpec; 1] = [argopt::H_REJECT_SPEC];
    let p = match argopt::parse("hostid", args, &opts, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if let Some(status) = argopt::rejected_short_h(ctx, io, "hostid", &p, 1) {
        return Ok(status);
    }
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
                    HOSTID_HELP_BODY,
                    gnuhelp::footer("hostid", "hostid invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("hostid", "Jim Meyering")
                )?;
                0
            }
        });
    }
    if !p.positionals.is_empty() {
        // [GNU] hostid.c: any operand is rejected with the try-help frame.
        let _ = usage_error(
            ctx,
            io,
            "hostid",
            &tr!(
                ctx,
                "command.hostid.error.extra_operand",
                "extra operand '{operand}'",
                operand = p.positionals[0].to_string_lossy()
            ),
        );
        return Ok(1);
    }
    writeln!(io.stdout, "{}", machine_id_hex(ctx))?;
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

    fn run(args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = scratch("hostid");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::hostid_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn prints_eight_hex_digits() {
        let (r, out, _) = run(&[]);
        assert_eq!(r, 0);
        assert_eq!(out.len(), 9); // 8 digits + newline
        assert!(out.ends_with('\n'));
        assert!(out[..8]
            .bytes()
            .all(|b| b.is_ascii_hexdigit() && !b.is_ascii_uppercase()));
    }

    #[test]
    fn stable_per_environment() {
        let (_, a, _) = run(&[]);
        let (_, b, _) = run(&[]);
        assert_eq!(a, b);
    }

    #[test]
    fn extra_operand_is_error() {
        let (r, _, err) = run(&["xyz"]);
        assert_eq!(r, 1);
        assert!(err.contains("hostid: extra operand 'xyz'"));
        assert!(err.contains("Try 'hostid --help' for more information."));
    }

    /// GNU hostid has no short help/version letter: `parse_gnu_standard_options_only`
    /// (hostid.c:63-65) accepts only the long forms, and getopt reports every
    /// other letter as an invalid option (probed coreutils 9.4: `-V` and `-v`
    /// both print `invalid option` and exit 1).
    #[test]
    fn short_version_is_invalid_option() {
        let (r, out, err) = run(&["-V"]);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert_eq!(
            err,
            "hostid: invalid option -- 'V'\nTry 'hostid --help' for more information.\n"
        );

        let (r, out, err) = run(&["-v"]);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert_eq!(
            err,
            "hostid: invalid option -- 'v'\nTry 'hostid --help' for more information.\n"
        );
    }

    /// GNU hostid has no -h short option either (empty optstring): a bare
    /// `-h` is getopt's `invalid option -- 'h'`, rc 1 — not a help alias
    /// (probed coreutils 9.4; ho12, WinuxCmd #1802). Any other single
    /// letter behaves the same way (`-x`).
    #[test]
    fn short_h_is_invalid_option() {
        for flag in ["-h", "-x"] {
            let (r, out, err) = run(&[flag]);
            assert_eq!(r, 1, "{flag}");
            assert_eq!(out, "", "{flag}");
            assert_eq!(
                err,
                format!(
                    "hostid: invalid option -- '{}'\nTry 'hostid --help' for more information.\n",
                    &flag[1..]
                ),
                "{flag}"
            );
        }
    }

    /// The #1573 sequential contract on the -h marker: `-h` before
    /// `--help` still fails (getopt hits -h first), `--help` before `-h`
    /// still prints help (getopt exits at --help, the rest unchecked).
    #[test]
    fn h_help_ordering_follows_argv() {
        let (r, out, err) = run(&["-h", "--help"]);
        assert_eq!(r, 1);
        assert_eq!(out, "");
        assert!(err.contains("invalid option -- 'h'"));

        let (r, out, err) = run(&["--help", "-h"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("Usage: hostid [OPTION]\n"));
        assert_eq!(err, "");
    }

    /// [GNU] `--help`/`--version` print the probed coreutils 9.4 text,
    /// rc 0 (strict rows ho05/ho06; the product exe shows its branded
    /// form through the dispatcher's interception instead).
    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert_eq!(err, "");
        assert!(out.starts_with(
            "Usage: hostid [OPTION]\nPrint the numeric identifier (in hexadecimal) for the current host.\n"
        ));
        assert!(out.contains("      --help        display this help and exit\n"));
        assert!(out.contains("      --version     output version information and exit\n"));
        assert!(out.ends_with("info '(coreutils) hostid invocation'\n"));

        let (r, out, err) = run(&["--version"]);
        assert_eq!(r, 0);
        assert_eq!(err, "");
        assert!(out.starts_with("hostid (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Jim Meyering.\n"));
    }

    #[test]
    fn machine_value_follows_env() {
        let mut io = MemIo::new("");
        let sc = scratch("hostid-env");
        let mut env = MapEnv::new();
        env.set("COMPUTERNAME", "box-a");
        let ctx = Ctx::new(sc.dir.clone(), &env);
        let r = super::hostid_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        let a = io.out();
        let mut io = MemIo::new("");
        let mut env = MapEnv::new();
        env.set("COMPUTERNAME", "box-b");
        let ctx = Ctx::new(sc.dir.clone(), &env);
        let _ = super::hostid_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_ne!(a, io.out());
        assert_eq!(a.len(), 9);
    }
}
