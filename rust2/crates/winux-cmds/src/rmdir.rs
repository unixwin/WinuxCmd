//! `rmdir` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! Removes empty directories; failures print
//! `rmdir: failed to remove 'X': reason` and the aggregate status is 1.
//! `--ignore-fail-on-non-empty` suppresses exactly the non-empty failures.
//! `-p` also walks the operand's lexical parents upward, stopping at the
//! first failure (reported as `failed to remove directory 'X'`).
//! `-v` prints `rmdir: removing directory, 'X'` (comma!) **to stdout**
//! before each removal attempt. Empty operands fail like missing paths.

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::{reason, resolve};
use winux_core::{q, tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('p'),
        long: "parents",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "ignore-fail-on-non-empty",
        takes_value: false,
    },
    // [GNU] --path: deprecated hidden alias for -p/--parents (#1075).
    OptSpec {
        short: None,
        long: "path",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
];

pub fn rmdir_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("rmdir", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if p.positionals.is_empty() {
        let _ = usage_error(
            io,
            "rmdir",
            &tr!(
                ctx,
                "command.rmdir.error.missing_operand",
                "missing operand"
            ),
        );
        return Ok(1);
    }
    let parents = p.has("parents") || p.has("path");
    let ignore_nonempty = p.has("ignore-fail-on-non-empty");
    let verbose = p.has("verbose");

    let mut failed = false;
    {
        let out = &mut *io.stdout;
        for operand in &p.positionals {
            let name = operand.to_string_lossy().into_owned();
            if verbose {
                let _ = writeln!(
                    out,
                    "rmdir: {}",
                    tr!(
                        ctx,
                        "command.rmdir.status.removing_directory",
                        "removing directory, {directory}",
                        directory = q(&name)
                    )
                );
            }
            if let Err(kind) = remove_one(ctx, &name) {
                if !(ignore_nonempty && kind == FailKind::NonEmpty) {
                    let path = resolve(ctx, &name);
                    let e = fail_error(&path, kind);
                    let _ = writeln!(
                        io.stderr,
                        "rmdir: {}",
                        tr!(
                            ctx,
                            "command.rmdir.error.failed_to_remove",
                            "failed to remove {directory}: {reason}",
                            directory = q(&name),
                            reason = reason(&e)
                        )
                    );
                    failed = true;
                }
                continue;
            }
            if parents {
                let comps: Vec<&str> = name.split('/').filter(|c| !c.is_empty()).collect();
                if comps.len() > 1 {
                    for i in (1..comps.len()).rev() {
                        let parent = if name.starts_with('/') {
                            format!("/{}", comps[..i].join("/"))
                        } else {
                            comps[..i].join("/")
                        };
                        if verbose {
                            let _ = writeln!(
                                out,
                                "rmdir: {}",
                                tr!(
                                    ctx,
                                    "command.rmdir.status.removing_directory",
                                    "removing directory, {directory}",
                                    directory = q(&parent)
                                )
                            );
                        }
                        if let Err(kind) = remove_one(ctx, &parent) {
                            if !(ignore_nonempty && kind == FailKind::NonEmpty) {
                                let path = resolve(ctx, &parent);
                                let e = fail_error(&path, kind);
                                let _ = writeln!(
                                    io.stderr,
                                    "rmdir: {}",
                                    tr!(
                                        ctx,
                                        "command.rmdir.error.failed_to_remove_directory",
                                        "failed to remove directory {directory}: {reason}",
                                        directory = q(&parent),
                                        reason = reason(&e)
                                    )
                                );
                                failed = true;
                            }
                            break;
                        }
                    }
                }
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

#[derive(Clone, Copy, PartialEq)]
enum FailKind {
    NotFound,
    NonEmpty,
    NotADirectory,
    Other,
}

fn fail_error(path: &std::path::Path, kind: FailKind) -> std::io::Error {
    match kind {
        FailKind::NotFound => std::fs::remove_dir(path).unwrap_err(),
        FailKind::NonEmpty => {
            let _ = std::fs::remove_dir(path);
            std::io::Error::from(std::io::ErrorKind::DirectoryNotEmpty)
        }
        FailKind::NotADirectory => std::io::Error::from(std::io::ErrorKind::NotADirectory),
        FailKind::Other => std::io::Error::from(std::io::ErrorKind::Other),
    }
}

fn remove_one(ctx: &Ctx, name: &str) -> Result<(), FailKind> {
    if name.is_empty() {
        return Err(FailKind::NotFound);
    }
    let path = resolve(ctx, name);
    match std::fs::symlink_metadata(&path) {
        Err(_) => Err(FailKind::NotFound),
        Ok(md) => {
            if !md.is_dir() {
                return Err(FailKind::NotADirectory);
            }
            match std::fs::remove_dir(&path) {
                Ok(()) => Ok(()),
                Err(e) => match e.kind() {
                    std::io::ErrorKind::NotFound => Err(FailKind::NotFound),
                    std::io::ErrorKind::DirectoryNotEmpty => Err(FailKind::NonEmpty),
                    _ => Err(FailKind::Other),
                },
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn remove_and_fail_paths() {
        let sc = ScratchCtx::new("rmdir");
        let ctx = sc.ctx();
        std::fs::create_dir(sc.path("d")).unwrap();
        let mut io = MemIo::new("");
        let args = ["d".into()];
        let r = super::rmdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(!sc.path("d").exists());

        // missing
        let mut io = MemIo::new("");
        let args = ["d".into()];
        let r = super::rmdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("No such file or directory"));

        // non-empty
        std::fs::create_dir(sc.path("d2")).unwrap();
        std::fs::write(sc.path("d2/x"), "1").unwrap();
        let mut io = MemIo::new("");
        let args = ["d2".into()];
        let r = super::rmdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("Directory not empty"));

        // ignore-fail-on-non-empty
        let mut io = MemIo::new("");
        let args = ["--ignore-fail-on-non-empty".into(), "d2".into()];
        let r = super::rmdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.err(), "");
    }

    #[test]
    fn parents_chain() {
        let sc = ScratchCtx::new("rmdirp");
        let ctx = sc.ctx();
        std::fs::create_dir_all(sc.path("a/b/c")).unwrap();
        let mut io = MemIo::new("");
        let args = ["-p".into(), "a/b/c".into()];
        let r = super::rmdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(!sc.path("a").exists());

        // ancestor failure stops the walk with status 1
        std::fs::create_dir_all(sc.path("x/y")).unwrap();
        std::fs::write(sc.path("x/keep"), "1").unwrap();
        let mut io = MemIo::new("");
        let args = ["-p".into(), "x/y".into()];
        let r = super::rmdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("Directory not empty"));
        assert!(sc.path("x").exists());
    }

    #[test]
    fn verbose_stdout_format() {
        let sc = ScratchCtx::new("rmdirv");
        let ctx = sc.ctx();
        std::fs::create_dir(sc.path("dv")).unwrap();
        let mut io = MemIo::new("");
        let args = ["-v".into(), "dv".into()];
        super::rmdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(io.out(), "rmdir: removing directory, 'dv'\n");
    }

    #[test]
    fn catalog_injection_translates_verbose_and_failure() {
        use winux_core::host::MapHost;
        use winux_core::{Ctx, MapEnv};

        let sc = ScratchCtx::new("rmdir-i18n");
        std::fs::create_dir(sc.path("dv")).unwrap();
        let mut host = MapHost::default();
        host.messages.insert(
            "command.rmdir.status.removing_directory".to_string(),
            "正在删除目录，{directory}".to_string(),
        );
        host.messages.insert(
            "command.rmdir.error.failed_to_remove".to_string(),
            "删除 {directory} 失败：{reason}".to_string(),
        );
        let env = MapEnv::new();
        let ctx = Ctx::with_host(sc.dir.clone(), &env, &host);

        let mut io = MemIo::new("");
        let args = ["-v".into(), "dv".into()];
        let r = super::rmdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "rmdir: 正在删除目录，'dv'\n");

        let mut io = MemIo::new("");
        let args = ["nosuch".into()];
        let r = super::rmdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(
            io.err().contains("删除 'nosuch' 失败："),
            "got {}",
            io.err()
        );
    }
}
