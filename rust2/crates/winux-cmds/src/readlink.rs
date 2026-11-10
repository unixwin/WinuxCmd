//! `readlink` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! Default mode prints symlink values (rc 1, silent, for non-symlinks).
//! Canonicalize modes are mutually exclusive (`-f` all-but-last exist,
//! `-e` all exist, `-m` none required); failures are silent unless `-v`.
//! `-n` omits the newline, `-z` uses NUL. Multiple operands supported.
//!
//! Windows pilot note: NTFS symlinks/junctions carry Windows-form targets,
//! so default-mode target VALUE is environment-bound (no privilege to
//! create native symlinks on this host); the gate compares rc strictly and
//! records values.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::canon::CanonMode;
use winux_core::{usage_error, CmdResult, Ctx, Io};

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
        short: Some('z'),
        long: "zero",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
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
        let _ = usage_error(io, "readlink", "missing operand");
        return Ok(1);
    }
    if p.has("no-newline") && p.has("zero") {
        let _ = usage_error(io, "readlink", "cannot combine --zero with --no-newline");
        return Ok(1);
    }
    let no_newline = p.has("no-newline");
    let zero = p.has("zero");
    let verbose = p.has("verbose");
    // GNU 9.4 accepts repeated canonicalize flags (last one on the command
    // line wins), so walk p.flags in order instead of rejecting combinations
    let mut canon: Option<CanonMode> = None;
    for flag in &p.flags {
        canon = match flag.as_str() {
            "canonicalize-existing" | "can-ex" => Some(CanonMode::AllMustExist),
            "canonicalize" | "can" => Some(CanonMode::AllButLast),
            "canonicalize-missing" | "can-mis" => Some(CanonMode::NoCheck),
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
                    Ok(v) => Some(v),
                    Err(e) => {
                        if verbose {
                            let _ = writeln!(
                                io.stderr,
                                "readlink: {}: {}",
                                name,
                                crate::fsutil::reason(&e)
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
                                "readlink: {}: {}",
                                name,
                                crate::fsutil::reason(&e)
                            );
                        }
                        None
                    }
                },
            };
            match value {
                Some(v) => {
                    out.write_all(v.as_bytes())?;
                    if zero {
                        out.write_all(b"\0")?;
                    } else if !no_newline {
                        out.write_all(b"\n")?;
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
}
