//! `readlink` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! Default mode prints symlink values; failures are silent unless the
//! verbosity is VERBOSE. `-q`/`--quiet` and `-s`/`--silent` are accepted
//! spellings of SILENT and `-v` of VERBOSE — the LAST of them on the
//! command line wins (`-v -q` stays silent, `-s -v` talks). `-n` omits
//! the terminator entirely, `-z` included (readlink.c puts the NUL behind
//! the same `if (! no_newline)` guard); with MULTIPLE operands `-n` is
//! ignored with the "ignoring --no-newline with multiple arguments"
//! warning and the normal terminators are emitted. `-z` uses NUL.
//! Multiple operands supported; rc 1 when any operand failed.
//!
//! Windows pilot note: NTFS symlinks/junctions carry Windows-form targets,
//! so default-mode target VALUE is environment-bound (no privilege to
//! create native symlinks on this host); the gate compares rc strictly and
//! records values.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::canon::CanonMode;
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('n'),
        long: "no-newline",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "canonicalize",
        takes_value: false,
    },
    OptSpec {
        short: Some('e'),
        long: "canonicalize-existing",
        takes_value: false,
    },
    OptSpec {
        short: Some('m'),
        long: "canonicalize-missing",
        takes_value: false,
    },
    OptSpec {
        short: Some('q'),
        long: "quiet",
        takes_value: false,
    },
    // GNU readlink's second silent spelling is its own short option.
    OptSpec {
        short: Some('s'),
        long: "silent",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero",
        takes_value: false,
    },
];

pub fn readlink_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("readlink", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if p.positionals.is_empty() {
        let _ = usage_error(
            io,
            "readlink",
            &tr!(
                ctx,
                "command.readlink.error.missing_operand",
                "missing operand"
            ),
        );
        return Ok(1);
    }
    let zero = p.has("zero");
    // readlink.c verbosity: the last of -q/-s/-v on the command line wins
    // (probed 9.4: `-v -q` stays silent, `-s -v` reports). Default silent.
    let mut verbose = false;
    for flag in &p.flags {
        if flag == "verbose" {
            verbose = true;
        } else if flag == "quiet" || flag == "silent" {
            verbose = false;
        }
    }
    let mut no_newline = p.has("no-newline");
    if no_newline && p.positionals.len() > 1 {
        // [GNU] readlink.c:149-156: the warning goes to stderr and the
        // normal terminators are emitted for every operand.
        let _ = writeln!(
            io.stderr,
            "readlink: {}",
            tr!(
                ctx,
                "command.readlink.warning.no_newline_multiple_arguments",
                "ignoring --no-newline with multiple arguments"
            )
        );
        no_newline = false;
    }
    // GNU 9.4 accepts repeated canonicalize flags (last one on the command
    // line wins), so walk p.flags in order instead of rejecting combinations
    let mut canon: Option<CanonMode> = None;
    for flag in &p.flags {
        canon = match flag.as_str() {
            "canonicalize-existing" => Some(CanonMode::AllMustExist),
            "canonicalize" => Some(CanonMode::AllButLast),
            "canonicalize-missing" => Some(CanonMode::NoCheck),
            _ => canon,
        };
    }

    let mut failed = false;
    {
        let out = &mut *io.stdout;
        for operand in &p.positionals {
            let name = operand.to_string_lossy().into_owned();
            let value = match canon {
                Some(cm) => match crate::canon::canonicalize_operand(ctx, &name, cm) {
                    // [GNU] lib/canonicalize.c assembles the canonical name
                    // with '/' by construction (9.4 probe: `readlink -f
                    // d/f.txt` -> '/tmp/.../d/f.txt', zero backslashes). A
                    // '\' breaks the shell word arithmetic that consumes
                    // the output (bats-core "${BATS_PATH%/*/*}", #1491),
                    // so the canonicalize modes render the resolved name
                    // in the '/' display form. The no-mode form prints the
                    // stored target text verbatim, like GNU areadlink.
                    Ok(v) => Some(v.replace('\\', "/")),
                    Err(e) => {
                        if verbose {
                            let _ = writeln!(
                                io.stderr,
                                "readlink: {}",
                                tr!(
                                    ctx,
                                    "command.readlink.error.cannot_read",
                                    "{file}: {reason}",
                                    file = name,
                                    reason = crate::fsutil::reason(&e)
                                )
                            );
                        }
                        None
                    }
                },
                None => match std::fs::read_link(&name_abs(ctx, &name)) {
                    Ok(t) => Some(t.to_string_lossy().into_owned()),
                    Err(e) => {
                        if verbose {
                            let _ = writeln!(
                                io.stderr,
                                "readlink: {}",
                                tr!(
                                    ctx,
                                    "command.readlink.error.cannot_read",
                                    "{file}: {reason}",
                                    file = name,
                                    reason = crate::fsutil::reason(&e)
                                )
                            );
                        }
                        None
                    }
                },
            };
            match value {
                Some(v) => {
                    out.write_all(v.as_bytes())?;
                    // [GNU] readlink.c:162-168: the NUL lives behind the
                    // same `if (! no_newline)` guard — -n suppresses the
                    // terminator entirely, -z included.
                    if !no_newline {
                        if zero {
                            out.write_all(b"\0")?;
                        } else {
                            out.write_all(b"\n")?;
                        }
                    }
                }
                None => failed = true,
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

fn name_abs(ctx: &Ctx, name: &str) -> std::path::PathBuf {
    let p = std::path::Path::new(name);
    if p.is_absolute() {
        p.to_path_buf()
    } else {
        ctx.cwd.join(p)
    }
}

#[cfg(test)]
mod tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn quiet_spellings_and_last_wins_verbosity() {
        // Probed GNU 9.4: -q/--quiet/-s/--silent are accepted spellings of
        // SILENT and -v of VERBOSE; the last one on the line wins.
        let sc = ScratchCtx::new("readlink-quiet");
        let ctx = sc.ctx();
        std::fs::write(sc.path("f"), "x").unwrap();
        for args in [
            vec!["-q", "f"],
            vec!["--quiet", "f"],
            vec!["-s", "f"],
            vec!["--silent", "f"],
        ] {
            let owned: Vec<std::ffi::OsString> =
                args.iter().map(std::ffi::OsString::from).collect();
            let mut io = MemIo::new("");
            let r = super::readlink_cmd(&owned, &mut io.split(), &ctx).unwrap();
            assert_eq!(r, 1, "args={args:?}");
            assert_eq!(io.err(), "", "args={args:?}");
        }
        // -v -q stays silent; -s -v reports.
        let owned: Vec<std::ffi::OsString> = ["-v", "-q", "f"]
            .iter()
            .map(std::ffi::OsString::from)
            .collect();
        let mut io = MemIo::new("");
        super::readlink_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.err(), "");

        let owned: Vec<std::ffi::OsString> = ["-s", "-v", "f"]
            .iter()
            .map(std::ffi::OsString::from)
            .collect();
        let mut io = MemIo::new("");
        super::readlink_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert!(io.err().contains("f:"), "verbose after silent must report");
    }

    #[test]
    fn no_newline_suppresses_zero_too_and_multi_warns() {
        // Probed GNU 9.4: -n wins over -z for a single operand (no
        // terminator at all); with multiple operands -n is ignored with
        // the warning and the normal terminators are emitted.
        let sc = ScratchCtx::new("readlink-nz");
        let ctx = sc.ctx();
        std::fs::write(sc.path("f"), "x").unwrap();
        // -f mode works without symlinks (all-but-last exist).
        let owned: Vec<std::ffi::OsString> = ["-n", "-z", "-f", "f"]
            .iter()
            .map(std::ffi::OsString::from)
            .collect();
        let mut io = MemIo::new("");
        let r = super::readlink_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(
            !io.out().ends_with('\0'),
            "-n must suppress the NUL, got {:?}",
            io.out()
        );

        let owned: Vec<std::ffi::OsString> = ["-n", "-f", "f", "f"]
            .iter()
            .map(std::ffi::OsString::from)
            .collect();
        let mut io = MemIo::new("");
        let r = super::readlink_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(
            io.out().matches('\n').count(),
            2,
            "multi-operand -n warns and emits newlines"
        );
        assert!(io
            .err()
            .contains("ignoring --no-newline with multiple arguments"));
    }

    #[test]
    fn default_mode_non_symlink_is_rc1_silent() {
        let sc = ScratchCtx::new("readlink");
        let ctx = sc.ctx();
        std::fs::write(sc.path("f"), "x").unwrap();
        let mut io = MemIo::new("");
        let args = ["f".into()];
        let r = super::readlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "");
        assert_eq!(io.err(), "");
    }

    #[test]
    fn missing_operand_is_usage_error() {
        let sc = ScratchCtx::new("readlink2");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let r = super::readlink_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("missing operand"));
    }

    #[test]
    fn canonicalize_f_and_e_paths() {
        let sc = ScratchCtx::new("readlink3");
        let ctx = sc.ctx();
        std::fs::create_dir(sc.path("sub")).unwrap();
        std::fs::write(sc.path("sub/f"), "x").unwrap();

        let mut io = MemIo::new("");
        let args = ["-f".into(), "sub/f".into()];
        let r = super::readlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().to_lowercase().contains("sub"));

        // -f on missing last component still canonicalizes
        let mut io = MemIo::new("");
        let args = ["-f".into(), "sub/missing".into()];
        let r = super::readlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);

        // -f with missing parent fails silently
        let mut io = MemIo::new("");
        let args = ["-f".into(), "nodir/last".into()];
        let r = super::readlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.out(), "");

        // -e requires existence
        let mut io = MemIo::new("");
        let args = ["-e".into(), "sub/missing".into()];
        let r = super::readlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);

        // -m never fails
        let mut io = MemIo::new("");
        let args = ["-m".into(), "nodir/last/deeper".into()];
        let r = super::readlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(!io.out().is_empty());
    }

    #[test]
    fn symlink_value_if_creatable() {
        let sc = ScratchCtx::new("readlink4");
        let ctx = sc.ctx();
        std::fs::write(sc.path("tgt"), "x").unwrap();
        if let Err(_) = try_symlink(&sc, "tgt", "lnk") {
            return; // environment cannot create symlinks; skip
        }
        let mut io = MemIo::new("");
        let args = ["lnk".into()];
        let r = super::readlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(!io.out().is_empty());
    }

    #[cfg(unix)]
    fn try_symlink(sc: &ScratchCtx, target: &str, link: &str) -> Result<(), ()> {
        std::os::unix::fs::symlink(sc.path(target), sc.path(link)).map_err(|_| ())
    }

    #[cfg(windows)]
    fn try_symlink(sc: &ScratchCtx, target: &str, link: &str) -> Result<(), ()> {
        let out = std::process::Command::new("cmd")
            .args(["/C", "mklink"])
            .arg(sc.path(link))
            .arg(sc.path(target))
            .output()
            .map_err(|_| ())?;
        if out.status.success() {
            Ok(())
        } else {
            Err(())
        }
    }

    #[test]
    fn catalog_injection_translates_warning_and_read_failure() {
        use winux_core::host::MapHost;
        use winux_core::{Ctx, MapEnv};

        let sc = ScratchCtx::new("readlink-i18n");
        let mut host = MapHost::default();
        host.messages.insert(
            "command.readlink.warning.no_newline_multiple_arguments".to_string(),
            "多个参数时忽略 --no-newline".to_string(),
        );
        host.messages.insert(
            "command.readlink.error.cannot_read".to_string(),
            "{file}：{reason}".to_string(),
        );
        let env = MapEnv::new();
        let ctx = Ctx::with_host(sc.dir.clone(), &env, &host);

        let mut io = MemIo::new("");
        let args = ["-n".into(), "a".into(), "b".into()];
        let r = super::readlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(
            io.err().contains("多个参数时忽略 --no-newline"),
            "got {}",
            io.err()
        );

        let mut io = MemIo::new("");
        let args = ["-v".into(), "nosuch".into()];
        let r = super::readlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("nosuch："), "got {}", io.err());
    }
}
