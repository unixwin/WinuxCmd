//! `pwd` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! Default is physical; `POSIXLY_CORRECT` (presence) flips the default to
//! logical. `-L` uses `$PWD` when it is absolute and names the same
//! directory, silently falling back to physical otherwise. `-P` never uses
//! `$PWD`. Non-option arguments are ignored with a warning (rc stays 0).
//!
//! Note: output is inherently platform-form-bound (POSIX vs Windows paths);
//! the differential gate compares pwd against the C++ exe (win-only mode)
//! and records GNU rc/stderr, which are still verified.

use std::ffi::OsString;

use winux_core::{abs_path, strip_verbatim, usage_error, CmdResult, Ctx, Io};

pub fn pwd_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut logical: Option<bool> = None;
    let mut extras = 0usize;
    let mut no_more = false;
    for a in args {
        let s = a.to_string_lossy().into_owned();
        if no_more {
            extras += 1;
            continue;
        }
        match s.as_str() {
            "--" => no_more = true,
            "-L" | "--logical" => logical = Some(true),
            "-P" | "--physical" => logical = Some(false),
            "--help" | "--version" => return Ok(0),
            _ => {
                if s.starts_with('-') && s.len() > 1 {
                    let msg = format!("unrecognized option '{s}'");
                    let _ = usage_error(io, "pwd", &msg);
                    return Ok(1);
                }
                extras += 1;
            }
        }
    }
    if extras > 0 {
        let _ = writeln!(io.stderr, "pwd: ignoring non-option arguments");
    }

    let default_logical = ctx.env.get("POSIXLY_CORRECT").is_some();
    let use_logical = logical.unwrap_or(default_logical);

    let cwd = abs_path(ctx, std::path::Path::new("."));
    let physical = match std::fs::canonicalize(&cwd) {
        Ok(p) => strip_verbatim(p),
        Err(_) => cwd.clone(),
    };

    let result = if use_logical {
        match ctx.env.get("PWD") {
            Some(p)
                if is_absolute(&p.to_string_lossy())
                    && !has_relative_components(&p.to_string_lossy())
                    && same_dir(&cwd, &p) =>
            {
                p.to_string_lossy().into_owned()
            }
            _ => physical.to_string_lossy().into_owned(),
        }
    } else {
        physical.to_string_lossy().into_owned()
    };
    writeln!(io.stdout, "{result}")?;
    Ok(0)
}

fn is_absolute(p: &str) -> bool {
    std::path::Path::new(p).is_absolute()
}

/// GNU rejects a $PWD whose components include "." or ".." (coreutils
/// pwd.c logical_getcwd; probed against coreutils 9.4: `PWD=<dir>/.`,
/// `PWD=<dir>/./x`, `PWD=<dir>/x/../y` all fall back to the physical
/// spelling, while trailing and redundant slashes — `<dir>/`, `<a>//b` —
/// are accepted verbatim). Split on both separators: on Windows $PWD may
/// carry either spelling.
fn has_relative_components(p: &str) -> bool {
    p.split(['/', '\\'])
        .any(|c| c == "." || c == "..")
}

fn same_dir(cwd: &std::path::Path, pwd: &std::ffi::OsStr) -> bool {
    let pwd_path = std::path::Path::new(pwd);
    match (std::fs::canonicalize(cwd), std::fs::canonicalize(pwd_path)) {
        (Ok(a), Ok(b)) => strip_verbatim(a) == strip_verbatim(b),
        _ => false,
    }
}

#[cfg(test)]
mod tests {
    use winux_core::test_support::{MemIo, ScratchCtx};
    use winux_core::Ctx;

    #[test]
    fn physical_default() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("pwd");
        let ctx = sc.ctx();
        let r = super::pwd_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        let out = io.out();
        assert!(out.trim_end().ends_with(&format!(
            "{}",
            sc.dir.file_name().unwrap().to_string_lossy()
        )));
    }

    #[test]
    fn logical_uses_pwd_env_when_valid() {
        let mut io = MemIo::new("");
        let mut sc = ScratchCtx::new("pwd2");
        sc.env.set("PWD", sc.dir.to_string_lossy().as_ref());
        let ctx = sc.ctx();
        let args = ["-L".into()];
        let r = super::pwd_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), format!("{}\n", sc.dir.to_string_lossy()));

        // invalid PWD falls back silently to physical
        let mut io = MemIo::new("");
        sc.env.set("PWD", "/definitely/not/here");
        let ctx = sc.ctx();
        let r = super::pwd_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().trim_end().ends_with(&format!(
            "{}",
            sc.dir.file_name().unwrap().to_string_lossy()
        )));
    }

    #[test]
    fn ignores_non_option_args_with_warning() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("pwd3");
        let ctx = sc.ctx();
        let args = ["extra".into()];
        let r = super::pwd_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.err().contains("ignoring non-option arguments"));
    }

    #[test]
    fn logical_rejects_pwd_with_relative_components() {
        // GNU coreutils 9.4 (logical_getcwd): a $PWD containing "." or ".."
        // components is not used verbatim even when it names the same
        // directory — `pwd -L` falls back to the physical spelling. Each
        // probe names the scratch dir itself, so only the component rule
        // (not the same-dir check) can reject it.
        let base = ScratchCtx::new("pwd-dot-base");
        for suffix in ["/.", "/sub/..", "/./", "\\."] {
            std::fs::create_dir_all(base.path("sub")).unwrap();
            let pwd = format!("{}{suffix}", base.dir.to_string_lossy());
            let mut io = MemIo::new("");
            let mut sc = ScratchCtx::new("pwd-dot");
            sc.env.set("PWD", pwd.as_ref());
            // The command's cwd must be the dir PWD names, so same_dir holds.
            let ctx = Ctx {
                cwd: base.dir.clone(),
                env: &sc.env,
            };
            let args = ["-L".into()];
            let r = super::pwd_cmd(&args, &mut io.split(), &ctx).unwrap();
            assert_eq!(r, 0);
            let out = io.out();
            assert!(
                !out.contains("/./") && !out.contains("/../") && !out.contains("\\.")
                    && !out.contains("\\.."),
                "PWD={pwd} must not be echoed verbatim, got {out}"
            );
            assert!(
                out.trim_end().ends_with(
                    base.dir.file_name().unwrap().to_string_lossy().as_ref()
                ),
                "PWD={pwd} must fall back to physical, got {out}"
            );
        }
    }

    #[test]
    fn logical_accepts_redundant_slashes_verbatim() {
        // GNU keeps redundant slashes in an accepted $PWD verbatim
        // (`PWD=//tmp/x` -> `//tmp/x`, `PWD=/tmp//x` -> `/tmp//x`).
        assert!(!super::has_relative_components("//a/b"));
        assert!(!super::has_relative_components("/a//b"));
        assert!(!super::has_relative_components("/a/b/"));
        assert!(super::has_relative_components("/a/./b"));
        assert!(super::has_relative_components("/a/../b"));
        assert!(super::has_relative_components("/a/b/.."));
        assert!(super::has_relative_components("\\a\\."));
    }
}
