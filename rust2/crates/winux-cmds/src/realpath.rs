//! `realpath` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! Default: like `readlink -f` (all but last must exist). `-e` requires
//! existence, `-m` checks nothing, `-s`/`--strip` only lexical, `-q`
//! suppresses the error messages (statuses unchanged), `-z` NUL-terminates.
//! Errors are verbose by default (`realpath: X: No such file or directory`;
//! an empty operand is reported as `''`). `--relative-to=` relativizes.
//! Usage errors exit 1.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::canon::{canonicalize_operand, CanonMode};
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('e'),
        long: "canonicalize-existing",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "can-ex",
        takes_value: false,
    },
    OptSpec {
        short: Some('m'),
        long: "canonicalize-missing",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "can-mis",
        takes_value: false,
    },
    OptSpec {
        short: Some('q'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "sil",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "strip",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "ze",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "relative-to",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "relative-base",
        takes_value: true,
    },
];

pub fn realpath_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("realpath", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if p.positionals.is_empty() {
        let _ = usage_error(io, "realpath", "missing operand");
        return Ok(1);
    }
    let quiet = p.has("quiet");
    let zero = p.has("zero");
    let strip = p.has("strip");
    let canon = if strip {
        CanonMode::NoCheck // -s: lexical only, no symlink resolution
    } else if p.has("canonicalize-existing") {
        CanonMode::AllMustExist
    } else if p.has("canonicalize-missing") {
        CanonMode::NoCheck
    } else {
        CanonMode::AllButLast
    };
    let rel_to = p.value_of("relative-to").map(|s| s.to_string());
    let rel_base = p.value_of("relative-base").map(|s| s.to_string());

    let mut failed = false;
    {
        let out = &mut *io.stdout;
        for operand in &p.positionals {
            let name = operand.to_string_lossy().into_owned();
            let value = canonicalize_operand(ctx, &name, canon);
            match value {
                Ok(v) => {
                    let v = if let Some(base_dir) = rel_base.as_deref().or(rel_to.as_deref()) {
                        relativize(ctx, base_dir, &v)
                    } else {
                        v
                    };
                    out.write_all(v.as_bytes())?;
                    if zero {
                        out.write_all(b"\0")?;
                    } else {
                        out.write_all(b"\n")?;
                    }
                }
                Err(e) => {
                    if !quiet {
                        let shown = if name.is_empty() {
                            "''".to_string()
                        } else {
                            name.clone()
                        };
                        let _ = writeln!(
                            io.stderr,
                            "realpath: {}: {}",
                            shown,
                            crate::fsutil::reason(&e)
                        );
                    }
                    failed = true;
                }
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

fn relativize(ctx: &Ctx, base_dir: &str, target: &str) -> String {
    let base = canonicalize_operand(ctx, base_dir, CanonMode::NoCheck).unwrap_or_default();
    let bt = base.replace('\\', "/");
    let tt = target.replace('\\', "/");
    let bc: Vec<&str> = bt.split('/').filter(|s| !s.is_empty()).collect();
    let tc: Vec<&str> = tt.split('/').filter(|s| !s.is_empty()).collect();
    let mut common = 0usize;
    while common < bc.len() && common < tc.len() && bc[common] == tc[common] {
        common += 1;
    }
    if common == 0 {
        return target.to_string(); // no common prefix: absolute fallback
    }
    let mut ups = Vec::new();
    for _ in common..bc.len() {
        ups.push("..");
    }
    let mut parts: Vec<String> = ups.into_iter().map(|s| s.to_string()).collect();
    parts.extend(tc[common..].iter().map(|s| s.to_string()));
    if parts.is_empty() {
        ".".to_string()
    } else {
        parts.join("/")
    }
}

#[cfg(test)]
mod tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn default_all_but_last() {
        let sc = ScratchCtx::new("realpath");
        let ctx = sc.ctx();
        std::fs::create_dir(sc.path("sub")).unwrap();
        std::fs::write(sc.path("sub/f"), "x").unwrap();

        // missing LAST is fine by default
        let mut io = MemIo::new("");
        let args = ["sub/missing".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(!io.out().is_empty());

        // missing PARENT fails verbosely
        let mut io = MemIo::new("");
        let args = ["nodir/last".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("No such file or directory"));

        // -q suppresses the message but not the status
        let mut io = MemIo::new("");
        let args = ["-q".into(), "nodir/last".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(io.err(), "");
    }

    #[test]
    fn e_and_m_modes() {
        let sc = ScratchCtx::new("realpath2");
        let ctx = sc.ctx();
        std::fs::write(sc.path("f"), "x").unwrap();

        let mut io = MemIo::new("");
        let args = ["-e".into(), "f".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);

        let mut io = MemIo::new("");
        let args = ["-e".into(), "missing".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);

        let mut io = MemIo::new("");
        let args = ["-m".into(), "a/b/c/d".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
    }

    #[test]
    fn empty_and_missing_operands() {
        let sc = ScratchCtx::new("realpath3");
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let args = ["".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("''"));

        let mut io = MemIo::new("");
        let r = super::realpath_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("missing operand"));
    }

    #[test]
    fn relative_to() {
        let sc = ScratchCtx::new("realpath4");
        let ctx = sc.ctx();
        std::fs::create_dir_all(sc.path("a/b")).unwrap();
        std::fs::write(sc.path("a/b/f"), "x").unwrap();

        let mut io = MemIo::new("");
        let args = ["--relative-to=a".into(), "a/b/f".into()];
        super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out().trim_end(), "b/f");
    }
}
