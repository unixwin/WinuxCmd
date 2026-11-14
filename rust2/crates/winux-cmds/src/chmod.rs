//! `chmod` — GNU coreutils semantics, Windows-permission subset.
//!
//! Argument grammar matches GNU (probed against coreutils 9.4): MODE then
//! files; missing files are reported per file (`cannot access 'X': ...`)
//! and processing continues with aggregate status 1; an invalid mode aborts
//! before any file is touched (`invalid mode: 'X'`, status 1); no operand
//! after MODE is `missing operand after 'MODE'`; with `--reference=RFILE`
//! every positional is a file (`chmod 644 --reference=rf f` reports
//! `cannot access '644'` and still applies the reference mode to `f`).
//!
//! Mode semantics live in `modemap` (full GNU modechange grammar: multi-
//! clause symbolic modes, `X`, copy forms, special bits, umask-excluded
//! omitted-who). NTFS persistence is the documented subset: the read-only
//! attribute tracks owner-write; special/execute bits exist only in the
//! in-memory model that drives `-v` wording.
//!
//! `-v`/`-c` messages follow GNU's shape: `mode of 'f' changed from 0644
//! (rw-r--r--) to 0755 (rwxr-xr-x)` / `mode of 'f' retained as 0755
//! (rwxr-xr-x)` (values come from the reconstructed model; bytes are
//! platform-bound, the gate classifies them exit-mode). `-f` suppresses
//! error diagnostics but not the aggregate status. `-R` walks preorder
//! (directory first — GNU fts order, verified), skips symlink entries, and
//! refuses filesystem roots unless `--no-preserve-root`
//! (`it is dangerous to operate recursively on '/'` / `use
//! --no-preserve-root to override this failsafe`, verified wording).

use std::ffi::OsString;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use crate::modemap::{
    apply_mode, current_mode, eval_spec, mode_string, parse_mode_spec, process_umask, ModeSpec,
};
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('R'),
        long: "recursive",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "rec",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "verb",
        takes_value: false,
    },
    OptSpec {
        short: Some('c'),
        long: "changes",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "cha",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "sil",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "reference",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "preserve-root",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "no-preserve-root",
        takes_value: false,
    },
];

/// True for a filesystem root: plain root, drive root (`C:`), or UNC share
/// root (`\\srv\share`) — the Windows analogs of GNU's root-dev check
/// (chmod.c: operand whose parent is itself).
pub(crate) fn is_fs_root(p: &Path) -> bool {
    let s = winux_core::strip_verbatim(p.to_path_buf())
        .to_string_lossy()
        .into_owned();
    let t = s.trim_end_matches(['/', '\\']);
    if t.is_empty() {
        return true;
    }
    let b = t.as_bytes();
    if b.len() == 2 && b[1] == b':' && b[0].is_ascii_alphabetic() {
        return true;
    }
    if let Some(rest) = t.strip_prefix(r"\\") {
        // \\server\share is the root of a Windows network file system
        return !rest.is_empty() && rest.matches(['\\', '/']).count() <= 1;
    }
    false
}

struct Apply<'i, 'o> {
    /// display prefix as typed by the user (for -v paths)
    verbose: bool,
    changes_only: bool,
    quiet: bool,
    ref_bits: Option<u32>,
    spec: Option<ModeSpec>,
    io: &'i mut Io<'o>,
    failed: bool,
}

impl Apply<'_, '_> {
    fn report(&mut self, name: &str, before: u32, after: u32) {
        let changed = after != before;
        // -v prints every outcome; -c prints changes only
        if !self.verbose && !(self.changes_only && changed) {
            return;
        }
        if changed {
            let _ = writeln!(
                *self.io.stdout,
                "mode of {} changed from {} to {}",
                winux_core::q(name),
                mode_string(before),
                mode_string(after)
            );
        } else {
            let _ = writeln!(
                *self.io.stdout,
                "mode of {} retained as {}",
                winux_core::q(name),
                mode_string(after)
            );
        }
    }

    fn apply_path(&mut self, display: &str, path: &Path) {
        let before = current_mode(path);
        let is_dir = std::fs::symlink_metadata(path)
            .map(|m| m.is_dir())
            .unwrap_or(false);
        let after = match &self.ref_bits {
            Some(bits) => bits & crate::modemap::ALL_BITS,
            None => eval_spec(
                before,
                is_dir,
                self.spec.as_ref().expect("spec or ref_bits"),
                process_umask(),
            ),
        };
        match apply_mode(path, after) {
            Ok(()) => self.report(display, before, after),
            Err(_) => {
                if !self.quiet {
                    let _ = writeln!(
                        self.io.stderr,
                        "chmod: changing permissions of {}: Permission denied",
                        winux_core::q(display)
                    );
                }
                self.failed = true;
            }
        }
    }
}

pub fn chmod_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("chmod", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    let recursive = p.has("recursive");
    let verbose = p.has("verbose");
    let changes_only = p.has("changes") && !verbose;
    let quiet = p.has("quiet");
    let no_preserve_root = p.has("no-preserve-root");
    let reference = p.value_of("reference").map(|s| s.to_string());

    // GNU grammar: with --reference=RFILE there is NO mode operand; every
    // positional is a file (verified: `chmod 644 --reference=rf f` treats
    // 644 as a missing file and still chmods f).
    let ref_bits = match &reference {
        Some(r) => {
            let rp = resolve(ctx, Path::new(r));
            if rp.exists() {
                Some(current_mode(&rp))
            } else {
                let _ = writeln!(
                    io.stderr,
                    "chmod: failed to get attributes of {}: {}",
                    winux_core::q(r),
                    "No such file or directory"
                );
                return Ok(1);
            }
        }
        None => None,
    };

    let spec: Option<ModeSpec> = if reference.is_some() {
        None
    } else {
        if p.positionals.is_empty() {
            let _ = usage_error(ctx, io, "chmod", &crate::messages::missing_operand(ctx));
            return Ok(1);
        }
        let mode = p.positionals[0].to_string_lossy().into_owned();
        if p.positionals.len() == 1 {
            let msg = crate::messages::missing_after(ctx, &mode);
            let _ = usage_error(ctx, io, "chmod", &msg);
            return Ok(1);
        }
        // validate BEFORE touching files (GNU aborts up front)
        match parse_mode_spec(&mode) {
            Ok(s) => Some(s),
            Err(()) => {
                let msg = format!("invalid mode: {}", winux_core::q(&mode));
                let _ = usage_error(ctx, io, "chmod", &msg);
                return Ok(1);
            }
        }
    };

    let files: Vec<String> = if reference.is_some() {
        p.positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    } else {
        p.positionals[1..]
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect()
    };

    let mut ap = Apply {
        verbose,
        changes_only,
        quiet,
        ref_bits,
        spec,
        io,
        failed: false,
    };

    for name in &files {
        let path = resolve(ctx, Path::new(name));
        if !path.exists() {
            if !quiet {
                let _ = writeln!(
                    ap.io.stderr,
                    "chmod: cannot access {}: {}",
                    winux_core::q(name),
                    "No such file or directory"
                );
            }
            ap.failed = true;
            continue;
        }
        if recursive && !no_preserve_root && is_fs_root(&path) {
            let _ = writeln!(
                ap.io.stderr,
                "chmod: it is dangerous to operate recursively on {}",
                winux_core::q(name)
            );
            let _ = writeln!(
                ap.io.stderr,
                "chmod: use --no-preserve-root to override this failsafe"
            );
            ap.failed = true;
            continue;
        }
        if recursive && path.is_dir() {
            walk_chmod(&mut ap, name, &path);
        } else {
            ap.apply_path(name, &path);
        }
    }
    Ok(if ap.failed { 1 } else { 0 })
}

/// GNU fts preorder: the directory itself first, then entries (verified
/// `chmod -vR 755 sub`: sub, sub/aa, sub/deep, sub/deep/x). Symlink
/// entries are ignored (GNU chmod ignores symlinks found in traversal).
fn walk_chmod(ap: &mut Apply, display: &str, dir: &Path) {
    ap.apply_path(display, dir);
    let rd = match std::fs::read_dir(dir) {
        Ok(r) => r,
        Err(_) => {
            if !ap.quiet {
                let _ = writeln!(
                    ap.io.stderr,
                    "chmod: cannot read directory {}: Permission denied",
                    winux_core::q(display)
                );
            }
            ap.failed = true;
            return;
        }
    };
    for entry in rd.flatten() {
        let p = entry.path();
        let ft = match entry.file_type() {
            Ok(t) => t,
            Err(_) => continue,
        };
        if ft.is_symlink() {
            continue;
        }
        let child_display = format!(
            "{}/{}",
            display.trim_end_matches('/'),
            entry.file_name().to_string_lossy()
        );
        if ft.is_dir() {
            walk_chmod(ap, &child_display, &p);
        } else {
            ap.apply_path(&child_display, &p);
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str], sc: &ScratchCtx) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(|s| s.into()).collect();
        let r = chmod_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    fn mode_of(sc: &ScratchCtx, name: &str) -> u32 {
        current_mode(&sc.path(name))
    }

    #[test]
    fn octal_drives_readonly_attribute() {
        let sc = ScratchCtx::new("chmod");
        std::fs::write(sc.path("f"), "x").unwrap();
        assert_eq!(run(&["444", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 == 0); // readonly persisted
        assert_eq!(run(&["644", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 != 0);
        // 0000 clears owner-write too
        assert_eq!(run(&["0000", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 == 0);
    }

    #[test]
    fn symbolic_multi_clause() {
        let sc = ScratchCtx::new("chmod2");
        std::fs::write(sc.path("f"), "x").unwrap();
        // u+rwx,go+rx from 644 -> 755: owner-w stays on -> writable
        assert_eq!(run(&["u+rwx,go+rx", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 != 0);
        // a=r,u+w: owner keeps w
        assert_eq!(run(&["a=r,u+w", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 != 0);
        // a=r only -> readonly
        assert_eq!(run(&["a=r", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 == 0);
    }

    #[test]
    fn x_semantics_and_specials_parse() {
        let sc = ScratchCtx::new("chmod3");
        std::fs::write(sc.path("f"), "x").unwrap();
        std::fs::create_dir(sc.path("d")).unwrap();
        // u+X on a plain file: no-op (no x anywhere pre-change)
        assert_eq!(run(&["u+X", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 != 0);
        // u+X on a dir: dirs are traversable; readonly untouched
        assert_eq!(run(&["u+X", "d"], &sc).0, 0);
        assert!(mode_of(&sc, "d") & 0o200 != 0);
        // special-bit modes parse and persist via owner-w only
        assert_eq!(run(&["4755", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 != 0);
        assert_eq!(run(&["1755", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 != 0);
    }

    #[test]
    fn error_paths() {
        let sc = ScratchCtx::new("chmod4");
        std::fs::write(sc.path("f"), "x").unwrap();
        // invalid mode aborts before touching the file
        let (r, _, e) = run(&["badmode", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid mode: 'badmode'"));
        // GNU wording has the colon (verified)
        let (r, _, e) = run(&["077777", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid mode: '077777'"));
        // missing file continues to the next operand
        let (r, _, e) = run(&["644", "missingfile", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access 'missingfile'"));
        assert!(mode_of(&sc, "f") & 0o200 != 0);
        // invalid spec never touches files
        let (r, _, _) = run(&["zz", "f"], &sc);
        assert_eq!(r, 1);
        // valid no-op forms succeed (GNU verified rc 0)
        assert_eq!(run(&["u+", "f"], &sc).0, 0);
        assert_eq!(run(&["=", "f"], &sc).0, 0);
        assert_eq!(run(&["+", "f"], &sc).0, 0);
        // multiple ops in one clause
        assert_eq!(run(&["u+r-w", "f"], &sc).0, 0);
    }

    #[test]
    fn operand_grammar() {
        let sc = ScratchCtx::new("chmod5");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, e) = run(&["644"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("missing operand after '644'"));
        let (r, _, e) = run(&[], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("missing operand"));
        // `-` is a literal filename operand (GNU verified)
        let (r, _, e) = run(&["644", "-"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access '-'"));
    }

    #[test]
    fn reference_forms() {
        let sc = ScratchCtx::new("chmod6");
        std::fs::write(sc.path("f"), "x").unwrap();
        std::fs::write(sc.path("rf"), "x").unwrap();
        assert_eq!(run(&["444", "rf"], &sc).0, 0);
        // plain --reference applies the reference mode
        assert_eq!(run(&["--reference=rf", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 == 0);
        // --reference + MODE operand: the mode word is a FILE operand (GNU
        // verified: cannot access '644', rc 1, other file still changed).
        // f is read-only now, so use a fresh target file.
        std::fs::write(sc.path("g"), "x").unwrap();
        let (r, _, e) = run(&["644", "--reference=rf", "g"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access '644'"));
        assert!(mode_of(&sc, "f") & 0o200 == 0);
        // missing reference file
        let (r, _, e) = run(&["--reference=nonexist", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("failed to get attributes of 'nonexist'"));
    }

    #[test]
    fn verbose_wording() {
        let sc = ScratchCtx::new("chmod7");
        std::fs::write(sc.path("f"), "x").unwrap();
        // fresh file reconstructs 0644; 644 -> retained (exit-mode in gate)
        let (r, out, _) = run(&["-v", "644", "f"], &sc);
        assert_eq!(r, 0);
        assert!(out.contains("mode of 'f' retained as 0644 (rw-r--r--)"));
        let (r, out, _) = run(&["-v", "755", "f"], &sc);
        assert_eq!(r, 0);
        assert!(out.contains("mode of 'f' changed from 0644 (rw-r--r--) to 0755 (rwxr-xr-x)"));
        // -c prints only on change. Execute bits live in the model only,
        // so the persisted file reads back 0644 after the 755 above: use
        // fresh files for the -c cases (w-bit persistence is real).
        std::fs::write(sc.path("g"), "x").unwrap();
        let (r, out, _) = run(&["-c", "644", "g"], &sc);
        assert_eq!(r, 0);
        assert!(out.is_empty());
        let (r, out, _) = run(&["-c", "444", "g"], &sc);
        assert_eq!(r, 0);
        assert!(out.contains("changed from 0644 (rw-r--r--) to 0444 (r--r--r--)"));
        // -f suppresses diagnostics, keeps status
        let (r, _, e) = run(&["-f", "644", "missing"], &sc);
        assert_eq!(r, 1);
        assert!(e.is_empty());
    }

    #[test]
    fn recursive_preorder() {
        let sc = ScratchCtx::new("chmod8");
        std::fs::create_dir_all(sc.path("sub/deep")).unwrap();
        std::fs::write(sc.path("sub/aa"), "x").unwrap();
        std::fs::write(sc.path("sub/deep/x"), "x").unwrap();
        let (r, out, _) = run(&["-vR", "700", "sub"], &sc);
        assert_eq!(r, 0);
        // GNU preorder: sub, sub/aa, sub/deep, sub/deep/x (verified order)
        let i1 = out.find("mode of 'sub' changed").unwrap();
        let i2 = out.find("mode of 'sub/aa' changed").unwrap();
        let i3 = out.find("mode of 'sub/deep' changed").unwrap();
        let i4 = out.find("mode of 'sub/deep/x' changed").unwrap();
        assert!(i1 < i2 && i2 < i3 && i3 < i4);
        // 700 keeps owner-write -> files stay writable (read-only attr off);
        // a 444 pass flips them
        assert!(mode_of(&sc, "sub/deep/x") & 0o200 != 0);
        assert_eq!(run(&["-R", "444", "sub"], &sc).0, 0);
        assert!(mode_of(&sc, "sub/deep/x") & 0o200 == 0);
        // dirs never take the read-only attribute (documented NTFS rule)
        std::fs::write(sc.path("sub/bb"), "x").unwrap();
        assert_eq!(run(&["-R", "444", "sub"], &sc).0, 0);
        let md = std::fs::metadata(sc.path("sub")).unwrap();
        assert!(!md.permissions().readonly());
    }

    #[test]
    fn preserve_root_refusal() {
        let sc = ScratchCtx::new("chmod9");
        // a real drive root refused under -R (Windows analog of GNU '/')
        let root = sc
            .dir
            .components()
            .next()
            .map(|c| c.as_os_str().to_string_lossy().into_owned())
            .unwrap();
        let (r, _, e) = run(&["-R", "755", &root], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("it is dangerous to operate recursively on"));
        assert!(e.contains("use --no-preserve-root to override this failsafe"));
        // explicit --preserve-root also refuses
        let (r, _, _) = run(&["-R", "--preserve-root", "755", &root], &sc);
        assert_eq!(r, 1);
        // deep operand is fine
        std::fs::create_dir_all(sc.path("sub/deep")).unwrap();
        assert_eq!(run(&["-R", "755", "sub"], &sc).0, 0);
    }
}
