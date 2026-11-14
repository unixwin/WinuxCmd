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
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('e'),
        long: "canonicalize-existing",
        takes_value: false,
    },
    // [EXT] not in GNU coreutils realpath; the 1.x applet carries it.
    OptSpec {
        short: Some('E'),
        long: "canonicalize",
        takes_value: false,
    },
    OptSpec {
        short: Some('m'),
        long: "canonicalize-missing",
        takes_value: false,
    },
    // [GNU] realpath.c mode word: -L logical, -P physical. Accepted
    // spellings; the symlink-order refinement they select is a no-op on
    // this resolver (no separate two-pass walk), the rc/output contract
    // is unchanged (probed 9.4: both accepted, output as default).
    OptSpec {
        short: Some('L'),
        long: "logical",
        takes_value: false,
    },
    OptSpec {
        short: Some('P'),
        long: "physical",
        takes_value: false,
    },
    OptSpec {
        short: Some('q'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: Some('s'),
        long: "strip",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "no-symlinks",
        takes_value: false,
    },
    OptSpec {
        short: Some('z'),
        long: "zero",
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
        let _ = usage_error(
            io,
            "realpath",
            &tr!(
                ctx,
                "command.realpath.error.missing_operand",
                "missing operand"
            ),
        );
        return Ok(1);
    }
    let quiet = p.has("quiet");
    let zero = p.has("zero");
    // -s and --no-symlinks are the same option (realpath.c registers both
    // spellings onto 's').
    let strip = p.has("strip") || p.has("no-symlinks");
    // [GNU] realpath.c mode word: the LAST of -e/-m/-E in argv order wins
    // (probed 9.4: `-e -m f` resolves permissively, `-m -e f` demands
    // existence). -s stays the lexical no-symlinks walk.
    let mut mode_word: Option<CanonMode> = None;
    for flag in &p.flags {
        match flag.as_str() {
            "canonicalize-existing" => mode_word = Some(CanonMode::AllMustExist),
            "canonicalize-missing" => mode_word = Some(CanonMode::NoCheck),
            "canonicalize" => mode_word = Some(CanonMode::AllButLast),
            _ => {}
        }
    }
    let canon = if strip {
        CanonMode::NoCheck
    } else {
        mode_word.unwrap_or(CanonMode::AllButLast)
    };
    let rel_to = p.value_of("relative-to").map(|s| s.to_string());
    let rel_base = p.value_of("relative-base").map(|s| s.to_string());
    let relative_base_mode = rel_base.is_some();

    // [GNU] An empty --relative-to/--relative-base value is an invalid
    // operand, not the cwd: the command reports it once and exits 1 with
    // no operand output (1.x wording kept: "invalid operand: empty
    // string"; GNU 9.4 fails the base canonicalization instead).
    for value in rel_to.iter().chain(rel_base.iter()) {
        if value.is_empty() {
            let _ = writeln!(
                io.stderr,
                "realpath: {}",
                tr!(
                    ctx,
                    "command.realpath.error.invalid_operand",
                    "invalid operand: empty string"
                )
            );
            return Ok(1);
        }
    }

    // [GNU] With an existence-checking mode BOTH bases are canonicalized
    // under the same mode, so a FILE base reports "Not a directory" and
    // the run fails (probed 9.4).
    let base_dir = rel_base.as_deref().or(rel_to.as_deref()).map(String::from);
    if canon == CanonMode::AllMustExist {
        for base in rel_to.iter().chain(rel_base.iter()) {
            if let Ok(m) = std::fs::metadata(ctx.cwd.join(base)) {
                if !m.is_dir() {
                    let _ = writeln!(
                        io.stderr,
                        "realpath: {}",
                        tr!(
                            ctx,
                            "command.realpath.error.not_a_directory",
                            "{file}: Not a directory",
                            file = base
                        )
                    );
                    return Ok(1);
                }
            }
        }
    }

    let mut failed = false;
    {
        let out = &mut *io.stdout;
        for operand in &p.positionals {
            let name = operand.to_string_lossy().into_owned();
            let value = canonicalize_operand(ctx, &name, canon);
            match value {
                Ok(v) => {
                    let v = if let Some(base) = base_dir.as_deref() {
                        relativize(ctx, base, &v, relative_base_mode)
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
                            "realpath: {}",
                            tr!(
                                ctx,
                                "command.realpath.error.resolve_failed",
                                "{file}: {reason}",
                                file = shown,
                                reason = crate::fsutil::reason(&e)
                            )
                        );
                    }
                    failed = true;
                }
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// GNU relativization: --relative-to always relativizes;
/// --relative-base relativizes only when the target is BELOW the base —
/// an outside target keeps its absolute form (probed 9.4:
/// `--relative-base=/a/b /a/c` -> "/a/c", `--relative-base=/a /a/b` ->
/// "b", `--relative-base=/a/b /a` -> "/a").
fn relativize(ctx: &Ctx, base_dir: &str, target: &str, below_only: bool) -> String {
    // Canonicalize the base through the SAME filesystem machinery as the
    // target, so the two spellings are comparable: the CI runner TEMP dir
    // carries a spelling variant (8.3/case/junction) that made the
    // lexical ctx.cwd-anchored base never match the canonicalized target,
    // and every --relative-to fell back to absolute (CI-only red).
    // Missing bases keep the lexical form (absolute fallback follows).
    let base = canonicalize_operand(ctx, base_dir, CanonMode::AllMustExist)
        .or_else(|_| canonicalize_operand(ctx, base_dir, CanonMode::NoCheck))
        .unwrap_or_default();
    let bt = base.replace('\\', "/");
    let tt = target.replace('\\', "/");
    let bc: Vec<&str> = bt.split('/').filter(|s| !s.is_empty()).collect();
    let tc: Vec<&str> = tt.split('/').filter(|s| !s.is_empty()).collect();
    let mut common = 0usize;
    // Windows spellings are case-variants (ctx.cwd may carry a lowercase
    // drive where the canonicalized target is uppercase): the common
    // prefix walks case-insensitively.
    while common < bc.len() && common < tc.len() && bc[common].eq_ignore_ascii_case(tc[common]) {
        common += 1;
    }
    // Component-boundary below-check: a matching character prefix is not
    // enough ("/a/bc" does not live below "/a/b"), and --relative-base
    // leaves targets outside the base absolute.
    let below = common == bc.len();
    if below_only && !below {
        return target.to_string();
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

#[cfg(test)]
mod trailing_tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn trailing_separator_on_existing_file_is_not_a_directory() {
        let sc = ScratchCtx::new("realpath-ts");
        let ctx = sc.ctx();
        std::fs::write(sc.path("file.txt"), "x").unwrap();
        let mut io = MemIo::new("");
        let args = ["file.txt/".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1, "out={:?} err={:?}", io.out(), io.err());
        assert!(io.err().contains("Not a directory"));
        // missing leaf with trailing separators still resolves
        let mut io = MemIo::new("");
        let args = ["missing/".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
    }

    #[test]
    fn catalog_injection_translates_not_a_directory() {
        use winux_core::host::MapHost;
        use winux_core::{Ctx, MapEnv};

        let sc = ScratchCtx::new("realpath-i18n");
        std::fs::write(sc.path("f.txt"), "x").unwrap();
        let mut host = MapHost::default();
        host.messages.insert(
            "command.realpath.error.not_a_directory".to_string(),
            "{file}：不是目录".to_string(),
        );
        let env = MapEnv::new();
        let ctx = Ctx::with_host(sc.dir.clone(), &env, &host);
        let mut io = MemIo::new("");
        let args = ["-e".into(), "--relative-to=f.txt".into(), "x".into()];
        let r = super::realpath_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("f.txt：不是目录"), "got {}", io.err());
    }
}
