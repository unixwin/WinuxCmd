//! `mkdir` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! `-p` creates parents silently tolerating existing directories, but an
//! existing non-directory anywhere in the chain is `Not a directory`, and an
//! existing final non-directory is `File exists`. `-v` prints
//! `mkdir: created directory 'X'` **to stdout** (straight quotes,
//! byte-gated) for every directory it actually creates. `-m` accepts octal
//! and symbolic modes; the Windows pilot maps the owner-write bit to the
//! NTFS read-only attribute (documented deviation, gate compares stdout/rc
//! only). Failures are reported per operand and the aggregate status is 1.

use std::ffi::OsString;
use std::io::{self};
use std::path::{Path, PathBuf};

use crate::argopt::{self, OptSpec};
use crate::fsutil::{reason, resolve, split_slashes};
use crate::modemap;
use winux_core::{q, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('p'),
        long: "parents",
        takes_value: false,
    },
    OptSpec {
        short: Some('m'),
        long: "mode",
        takes_value: true,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    // [GNU] -Z/--context: SELinux contexts do not exist on Windows; the
    // flag is accepted as a no-op with the value dropped. getopt gives the
    // long form an OPTIONAL value (--context=CTX, bare -Z), which this
    // flag-shaped entry reproduces: `mkdir -Z ctx_dir` creates ctx_dir as
    // an operand, exactly the shape the 1.x applet and its unit test pin.
    OptSpec {
        short: Some('Z'),
        long: "context",
        takes_value: false,
    },
];

pub fn mkdir_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("mkdir", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if p.positionals.is_empty() {
        let _ = usage_error(io, "mkdir", "missing operand");
        return Ok(1);
    }
    let parents = p.has("parents");
    let verbose = p.has("verbose");
    let mode = p.value_of("mode");
    if let Some(m) = mode {
        if let Err(bad) = modemap::validate_mode(m) {
            let msg = format!("invalid mode {}", q(&bad));
            let _ = usage_error(io, "mkdir", &msg);
            return Ok(1);
        }
    }

    let mut failed = false;
    for operand in &p.positionals {
        let name = operand.to_string_lossy().into_owned();
        let trimmed = name.trim_end_matches('/');
        let effective = if trimmed.is_empty() && name.starts_with('/') {
            "/".to_string()
        } else {
            trimmed.to_string()
        };
        let applied_mode: Option<u32> = mode.and_then(|m| modemap::parse_mode(m).ok());
        let folded = mkdir_fold(&effective);
        // [GNU] the verbose message and diagnostics keep the operand
        // spelling; the filesystem walk uses the fold.
        let fs_name = folded.as_deref().unwrap_or(&effective);
        let created = if parents {
            create_parents(ctx, &effective, fs_name, applied_mode, verbose, io)
        } else {
            create_one(ctx, &effective, fs_name, applied_mode, verbose, io)
        };
        if created.is_err() {
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// Fold the operand for the filesystem walk: the #1231 drive dialect
/// ("/e/Data") must land on the operand's drive, not the cwd's — the 1.x
/// applet folds the same spelling at its operand boundary. The displayed
/// spelling stays the original operand text.
fn mkdir_fold(effective: &str) -> Option<String> {
    let folded = crate::canon::prepare_operand(effective);
    if folded != effective {
        Some(folded)
    } else {
        None
    }
}

fn report_error(io: &mut Io, shown: &str, e: &io::Error) {
    let msg = format!("cannot create directory {}: {}", q(shown), reason(e));
    let _ = writeln!(io.stderr, "mkdir: {msg}");
}

fn verbose_created(io: &mut Io, shown: &str) {
    let _ = writeln!(*io.stdout, "mkdir: created directory {}", q(shown));
}

fn create_one(
    ctx: &Ctx,
    shown: &str,
    fs_name: &str,
    mode: Option<u32>,
    verbose: bool,
    io: &mut Io,
) -> Result<(), ()> {
    let path = resolve(ctx, fs_name);
    match std::fs::create_dir(&path) {
        Ok(()) => {
            if let Some(m) = mode {
                let _ = modemap::apply_mode(&path, m);
            }
            if verbose {
                verbose_created(io, shown);
            }
            Ok(())
        }
        Err(e) => {
            report_error(io, shown, &e);
            Err(())
        }
    }
}

fn create_parents(
    ctx: &Ctx,
    shown_name: &str,
    fs_name: &str,
    mode: Option<u32>,
    verbose: bool,
    io: &mut Io,
) -> Result<(), ()> {
    // Display components come from the operand spelling; the walk runs on
    // the folded path when the #1231 drive dialect renamed the root.
    let shown_comps = split_slashes(shown_name);
    let folded = fs_name != shown_name;
    if folded {
        // The fold turns "/e/Data" into "E:\Data": anchor at the drive
        // root and split on both separators. The fold consumes the
        // drive-letter component, so the displayed prefixes drop it.
        let mut root = PathBuf::from(fs_name);
        let mut fs_comps: Vec<String> = Vec::new();
        while let Some(parent) = root.parent().map(Path::to_path_buf) {
            if parent == root {
                break;
            }
            fs_comps.push(
                root.file_name()
                    .map(|f| f.to_string_lossy().into_owned())
                    .unwrap_or_default(),
            );
            root = parent;
        }
        fs_comps.reverse();
        return create_parents_walk(
            root,
            fs_comps,
            &shown_comps,
            shown_name.starts_with('/'),
            mode,
            verbose,
            io,
        );
    }
    let fs_comps: Vec<String> = split_slashes(fs_name).into_iter().map(String::from).collect();
    let mut acc = if fs_name.starts_with('/') {
        Path::new("/").to_path_buf()
    } else {
        ctx.cwd.clone()
    };
    let _ = &mut acc;
    create_parents_walk(
        acc,
        fs_comps,
        &shown_comps,
        shown_name.starts_with('/'),
        mode,
        verbose,
        io,
    )
}

fn create_parents_walk(
    root: PathBuf,
    fs_comps: Vec<String>,
    shown_comps: &[&str],
    absolute_shown: bool,
    mode: Option<u32>,
    verbose: bool,
    io: &mut Io,
) -> Result<(), ()> {
    if fs_comps.is_empty() {
        // Operand named an existing root ("/" or a bare drive root): -p is
        // a silent noop ([GNU] mkdir -p /e on an existing drive root).
        return Ok(());
    }
    // The folded shape consumes the operand's drive-letter component in its
    // root ("/e/Data" -> E:\ + [Data]), so the displayed prefix for fs
    // component i is the operand prefix ending at shown index i + offset.
    let offset = shown_comps.len().saturating_sub(fs_comps.len());
    let mut acc = root;
    for (i, comp) in fs_comps.iter().enumerate() {
        acc = acc.join(comp);
        let shown_end = (i + offset).min(shown_comps.len().saturating_sub(1));
        let shown = if absolute_shown {
            format!("/{}", shown_comps[..=shown_end].join("/"))
        } else {
            shown_comps[..=shown_end].join("/")
        };
        match std::fs::symlink_metadata(&acc) {
            Ok(md) => {
                if md.is_dir() {
                    continue; // existing dir: silent, not "created"
                }
                let err = io::Error::from(io::ErrorKind::AlreadyExists);
                report_error(io, &shown, &err);
                return Err(());
            }
            Err(_) => match std::fs::create_dir(&acc) {
                Ok(()) => {
                    // [GNU] mkdir -p -m MODE applies MODE to the leaf only;
                    // ancestors get the default mode (mkdir.c make_ancestor).
                    if i + 1 == fs_comps.len() {
                        if let Some(m) = mode {
                            let _ = modemap::apply_mode(&acc, m);
                        }
                    }
                    if verbose {
                        verbose_created(io, &shown);
                    }
                }
                Err(e) => {
                    report_error(io, &shown, &e);
                    return Err(());
                }
            },
        }
    }
    Ok(())
}

#[cfg(test)]
mod tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn basic_and_verbose() {
        let sc = ScratchCtx::new("mkdir");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let args = ["-v".into(), "a".into()];
        let r = super::mkdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(sc.path("a").is_dir());
        assert_eq!(io.out(), "mkdir: created directory 'a'\n");

        // existing dir: silent success without -v message
        let mut io = MemIo::new("");
        let args = ["a".into()];
        let r = super::mkdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("File exists"));
    }

    #[test]
    fn parents_verbose_chain() {
        let sc = ScratchCtx::new("mkdir2");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let args = ["-v".into(), "-p".into(), "p/q/r".into()];
        let r = super::mkdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(
            io.out(),
            "mkdir: created directory 'p'\n\
             mkdir: created directory 'p/q'\n\
             mkdir: created directory 'p/q/r'\n"
        );

        // -p on existing file in the chain: Not a directory style failure
        std::fs::write(sc.path("f1"), "x").unwrap();
        let mut io = MemIo::new("");
        let args = ["-p".into(), "f1/z".into()];
        let r = super::mkdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
    }

    #[test]
    fn missing_parent_and_empty() {
        let sc = ScratchCtx::new("mkdir3");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let args = ["x/y".into()];
        let r = super::mkdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("No such file or directory"));

        let mut io = MemIo::new("");
        let args = ["".into()];
        let r = super::mkdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
    }

    #[test]
    fn multiple_operands() {
        let sc = ScratchCtx::new("mkdir4");
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let args = ["n1".into(), "n2".into()];
        let r = super::mkdir_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(sc.path("n1").is_dir() && sc.path("n2").is_dir());
    }
}
