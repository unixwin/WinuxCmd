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
use crate::fsutil::{
    is_native_root_operand, is_preserve_root_operand, link_target_is_root,
    preserve_root_refusal_lines, resolve,
};
use crate::modemap::{
    apply_mode, current_mode, eval_spec, mode_string, parse_mode_spec, process_umask, ModeSpec,
};
use winux_core::{usage_error, CmdResult, Ctx, Io};

/// Registration order mirrors the upstream chmod.c long_options table —
/// ambiguity listings print candidates in table order (probed d8: `--=v`
/// lists changes, recursive, no-preserve-root, preserve-root, quiet,
/// reference, silent, verbose, help, version in exactly that order). The
/// 1.x shorthand longs (--rec/--verb/--cha/--sil) are gone: GNU resolves
/// them as unambiguous abbreviations, and registering them as canonical
/// names polluted the possibility lists (m4-rust d3 listed '--rec').
const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('c'),
        long: "changes",
        takes_value: false,
    },
    OptSpec {
        short: Some('R'),
        long: "recursive",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "no-preserve-root",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "preserve-root",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "reference",
        takes_value: true,
    },
    OptSpec {
        short: None,
        long: "silent",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
];

/// The mode letters GNU chmod.c declares as optional-argument short
/// options (`"Rcfvr::w::x::X::s::t::u::g::o::a::,::+::=::0::1::2::3::4::
/// 5::6::7::"`). A single-dash token whose first non-{v,c,f,R} character
/// is one of these becomes the MODE word — the whole token, exactly like
/// upstream taking `argv[optind - 1]` (probed Q1/Q5/Q6: `chmod -X 755 f`
/// modes only `f`, and `chmod 755 -X f` demotes `755` to a file operand).
const MODE_OPTION_LETTERS: &[char] = &[
    'r', 'w', 'x', 'X', 's', 't', 'u', 'g', 'o', 'a', ',', '+', '=', '0', '1', '2', '3', '4', '5',
    '6', '7',
];

/// Split GNU dash-mode tokens out of `args` before generic option parsing.
/// Tokens after a bare `--` are literal operands. A token like `-Q` (an
/// invalid short before any mode letter) stays in the stream so argopt
/// still prints `invalid option -- 'Q'` (probed Q2-Q4).
fn split_dash_modes(args: &[OsString]) -> (Vec<String>, Vec<OsString>) {
    let mut modes = Vec::new();
    let mut rest = Vec::new();
    let mut no_more_options = false;
    for arg in args {
        let text = arg.to_string_lossy().into_owned();
        if no_more_options || !text.starts_with('-') || text.len() < 2 || text.starts_with("--") {
            if text == "--" {
                no_more_options = true;
            }
            rest.push(arg.clone());
            continue;
        }
        let mut is_mode = false;
        for c in text[1..].chars() {
            if MODE_OPTION_LETTERS.contains(&c) {
                is_mode = true;
                break;
            }
            if !matches!(c, 'v' | 'c' | 'f' | 'R') {
                break;
            }
        }
        if is_mode {
            modes.push(text);
        } else {
            rest.push(arg.clone());
        }
    }
    (modes, rest)
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
    // GNU dash-mode tokens ("-w", "-X", "-wX", "-R755") are mode words, not
    // options (chmod.c optional-argument short letters); extract them before
    // generic parsing so `-X` is not misread as an invalid option.
    let (dash_modes, rest_args) = split_dash_modes(args);
    let p = match argopt::parse("chmod", &rest_args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    let recursive = p.has("recursive");
    let verbose = p.has("verbose");
    let changes_only = p.has("changes") && !verbose;
    let quiet = p.has("quiet") || p.has("silent");
    // GNU getopt assignment order: the LAST of --preserve-root /
    // --no-preserve-root wins (probed P15/P16).
    let preserve_root =
        p.flag_last(&["preserve-root", "no-preserve-root"]) == Some("preserve-root");
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

    // Mode operand: dash-mode tokens win the mode slot outright and demote
    // every positional to a file operand (probed Q5); otherwise the first
    // positional is the mode.
    let mode_word: Option<String> = if reference.is_some() {
        None
    } else if !dash_modes.is_empty() {
        Some(dash_modes.join(","))
    } else {
        None
    };

    let spec: Option<ModeSpec> = if reference.is_some() {
        None
    } else {
        let mode = match &mode_word {
            Some(m) => m.clone(),
            None => {
                if p.positionals.is_empty() {
                    let _ = usage_error(io, "chmod", "missing operand");
                    return Ok(1);
                }
                let m = p.positionals[0].to_string_lossy().into_owned();
                if p.positionals.len() == 1 {
                    let msg = format!("missing operand after {}", winux_core::q(&m));
                    let _ = usage_error(io, "chmod", &msg);
                    return Ok(1);
                }
                m
            }
        };
        // validate BEFORE touching files (GNU aborts up front)
        match parse_mode_spec(&mode) {
            Ok(s) => Some(s),
            Err(()) => {
                let msg = format!("invalid mode: {}", winux_core::q(&mode));
                let _ = usage_error(io, "chmod", &msg);
                return Ok(1);
            }
        }
    };

    let files: Vec<String> = if reference.is_some() || mode_word.is_some() {
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
        // Explicit GNU failsafe (upstream fires per-traversed-directory via
        // ROOT_DEV_INO_CHECK; refusal order is operand order — probed g7).
        if recursive && preserve_root && is_preserve_root_operand(ctx, name, &path) {
            for line in preserve_root_refusal_lines("chmod", name) {
                let _ = writeln!(ap.io.stderr, "{line}");
            }
            ap.failed = true;
            continue;
        }
        // Default-walk safety floor (A1 adjudication): a real native volume
        // root, a raw separator spelling, or a reparse point resolving onto
        // one is refused even without --preserve-root. GNU would walk here;
        // a Windows volume walk is unrecoverable. Flag-independent.
        let link_carries_root = recursive
            && std::fs::symlink_metadata(&path)
                .map(|m| m.file_type().is_symlink())
                .unwrap_or(false)
            && link_target_is_root(ctx, &path);
        if recursive && (is_native_root_operand(ctx, name, &path) || link_carries_root) {
            for line in preserve_root_refusal_lines("chmod", name) {
                let _ = writeln!(ap.io.stderr, "{line}");
            }
            ap.failed = true;
            continue;
        }
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
        let child_display = format!(
            "{}/{}",
            display.trim_end_matches('/'),
            entry.file_name().to_string_lossy()
        );
        if ft.is_symlink() {
            // GNU FTS_PHYSICAL: symlink entries are never chmodded during
            // traversal; -v prints the skip diagnostic (probed C1).
            if ap.verbose {
                let _ = writeln!(
                    *ap.io.stdout,
                    "neither symbolic link '{child_display}' nor referent has been changed"
                );
            }
            continue;
        }
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
        // GNU coreutils 9.4 refusal is OPT-IN: `bool preserve_root = false`
        // in main (chmod.c), the two-line ROOT_DEV_INO_WARN fires only with
        // explicit --preserve-root (probed M1 A/B cells, M2 P-cells), the
        // last of --preserve-root/--no-preserve-root wins (P15/P16), and a
        // non-"/" spelling prints the "(same as '/')" suffix (P3-P5). The
        // sandbox shell root is the namespace "/" and stays walkable by
        // default exactly like GNU.
        let mut sc = ScratchCtx::new("chmod9");
        sc.env.set("__RUBASH_SHELL_ROOT", &sc.dir.to_string_lossy());
        std::fs::write(sc.path("f"), "x").unwrap();
        // default: GNU walks "/" (M1 B1) — the sandbox walk succeeds
        assert_eq!(run(&["-R", "755", "."], &sc).0, 0);
        // explicit --preserve-root refuses with the suffix form (P4 `/.`)
        let (r, _, e) = run(&["-R", "--preserve-root", "755", "."], &sc);
        assert_eq!(r, 1);
        assert!(
            e.contains("chmod: it is dangerous to operate recursively on '.' (same as '/')"),
            "{e}"
        );
        assert!(e.contains("chmod: use --no-preserve-root to override this failsafe"));
        // last flag wins (P15: --preserve-root --no-preserve-root walks)
        assert_eq!(
            run(
                &["-R", "--preserve-root", "--no-preserve-root", "755", "."],
                &sc
            )
            .0,
            0
        );
        let (r, _, e) = run(
            &["-R", "--no-preserve-root", "--preserve-root", "755", "."],
            &sc,
        );
        assert_eq!(r, 1);
        assert!(e.contains("it is dangerous to operate recursively on '.'"));
        // the failsafe is recursion-scoped: non-recursive on the root is
        // GNU E1/E3 (no refusal)
        assert_eq!(run(&["755", "."], &sc).0, 0);
        // per-operand order: the refusal fires at the operand's position,
        // later operands still processed (g7)
        std::fs::create_dir_all(sc.path("sub")).unwrap();
        std::fs::write(sc.path("sub/x"), "x").unwrap();
        let (r, _, e) = run(&["-R", "--preserve-root", "755", "nosuch", "."], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access 'nosuch'"));
        // deep operand is fine under explicit --preserve-root
        let (r, out, _) = run(&["-R", "--preserve-root", "755", "sub"], &sc);
        assert_eq!(r, 0, "{out}");
    }

    #[test]
    fn native_root_floor_refusal() {
        // A1 adjudication (blocker 1 matrix): GNU walks "/" by default, but
        // a recursive walk of a REAL native volume root is refused even
        // without --preserve-root and even with --no-preserve-root. The
        // end-to-end cells below use only spellings that are lexically
        // volume roots but NOT real walkable trees on this host, so a
        // guard regression fails the test without touching anything; the
        // real-root identities are covered by the pure-predicate unit
        // (is_native_root_operand) in fsutil, which never touches the
        // filesystem by contract.
        let sc = ScratchCtx::new("chmod-floor");
        for op in ["Q:\\", "Q:/", "//nosuchsrv-accept/share"] {
            let (r, _, e) = run(&["-R", "755", op], &sc);
            assert_eq!(r, 1, "floor {op}: {e}");
            assert!(
                e.contains("it is dangerous to operate recursively on"),
                "floor {op}: {e}"
            );
            // flag-independent: the floor survives --no-preserve-root
            let (r, _, e) = run(
                &["-R", "--no-preserve-root", "--preserve-root", "755", op],
                &sc,
            );
            assert_eq!(r, 1, "floor+flag {op}");
            assert!(e.contains("it is dangerous to operate recursively on"));
        }
        // non-recursive on a root spelling is not the failsafe's business
        // (GNU E1/E3): plain access failure, no refusal
        let (r, _, e) = run(&["755", "Q:\\nosuch"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access"));
        assert!(!e.contains("dangerous"));
    }

    #[test]
    fn dash_mode_words() {
        // GNU chmod.c optstring declares the mode letters as optional-
        // argument short options; a dash token whose first non-{v,c,f,R}
        // char is a mode letter is the MODE word (probed Q1/Q2/Q5/Q6).
        let sc = ScratchCtx::new("chmod-dash");
        std::fs::write(sc.path("f"), "x").unwrap();
        // `-X` modes the file (Q1: rc 0, silent)
        assert_eq!(run(&["-X", "f"], &sc).0, 0);
        // `-wX 755 f` -> mode "-wX", "755" demoted to a file operand (Q6)
        let (r, _, e) = run(&["-wX", "755", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access '755'"), "{e}");
        // `755 -X f` same demotion (Q5)
        let (r, _, e) = run(&["755", "-X", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("cannot access '755'"), "{e}");
        // `-Q` is still an invalid option (Q2)
        let (r, _, e) = run(&["-Q", "755", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid option -- 'Q'"));
        assert!(e.contains("Try 'chmod --help' for more information."));
        // `-q` too (Q3)
        let (r, _, e) = run(&["-q", "755", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid option -- 'q'"));
        // `-R755` becomes the invalid mode "-R755" (getopt grabs the token)
        let (r, _, e) = run(&["-R755", "f"], &sc);
        assert_eq!(r, 1);
        assert!(e.contains("invalid mode: '-R755'"), "{e}");
        // real dash modes apply: -r clears every read bit
        assert_eq!(run(&["-r", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 == 0);
        // adding w back goes through the positional mode word ("+w" is a
        // non-dash token -> plain mode operand, GNU `chmod +w f`)
        assert_eq!(run(&["+w", "f"], &sc).0, 0);
        assert!(mode_of(&sc, "f") & 0o200 != 0);
    }

    #[test]
    fn junction_to_volume_root_is_refused() {
        // Blocker 3 e2e: a junction whose TARGET is a volume root carries
        // the root identity through the link (GNU refuses
        // `chmod -RH --preserve-root <symlink-to-root>` via stat(dev,ino);
        // the Windows analog refuses before any traversal). The refusal is
        // lexical (read_link + normalize), so nothing is walked even if
        // the guard were broken is impossible here: the refusal precedes
        // every filesystem call. Junctions need no privilege (mklink /J).
        let sc = ScratchCtx::new("chmod-junc");
        let out = std::process::Command::new("cmd")
            .args(["/C", "mklink", "/J"])
            .arg(sc.path("junc-root"))
            .arg("C:\\")
            .output()
            .expect("mklink /J");
        if !out.status.success() {
            // junction creation unavailable on this host: skip safely
            return;
        }
        for flags in [vec!["-R", "755"], vec!["-R", "--preserve-root", "755"]] {
            let mut argv = flags;
            argv.push("junc-root");
            let (r, _, e) = run(&argv, &sc);
            assert_eq!(r, 1, "junction refusal: {e}");
            assert!(
                e.contains("it is dangerous to operate recursively on"),
                "{e}"
            );
            assert!(e.contains("use --no-preserve-root to override this failsafe"));
        }
        // a junction onto a SUBDIRECTORY of the sandbox is not a root:
        // GNU COMFOLLOW traverses the referent (probed C3)
        std::fs::create_dir_all(sc.path("sub/deep")).unwrap();
        std::fs::write(sc.path("sub/deep/x"), "x").unwrap();
        let out = std::process::Command::new("cmd")
            .args(["/C", "mklink", "/J"])
            .arg(sc.path("junc-sub"))
            .arg(sc.path("sub"))
            .output()
            .expect("mklink /J");
        if out.status.success() {
            let (r, _, e) = run(&["-R", "755", "junc-sub"], &sc);
            assert_eq!(r, 0, "{e}");
        }
    }

    #[test]
    fn traversal_skips_symlinks_with_verbose_line() {
        // GNU FTS_PHYSICAL: -R never chmods symlink entries; -v prints the
        // skip line with the entry path (probed C1), non-verbose is silent.
        let sc = ScratchCtx::new("chmod-sym");
        std::fs::create_dir(sc.path("dir")).unwrap();
        std::fs::write(sc.path("dir/f"), "x").unwrap();
        let target = sc.path("dir/f");
        let link = sc.path("dir/lf");
        #[cfg(windows)]
        {
            // symlinks need privilege; a plain file stand-in would not be
            // a symlink — only assert when creation succeeds
            if std::os::windows::fs::symlink_file(&target, &link).is_err() {
                return;
            }
        }
        let (r, out, _) = run(&["-vR", "700", "dir"], &sc);
        assert_eq!(r, 0);
        assert!(
            out.contains("neither symbolic link 'dir/lf' nor referent has been changed"),
            "{out}"
        );
        // target untouched by the skip
        assert!(mode_of(&sc, "dir/f") & 0o200 != 0);
        // non-verbose: silent skip
        std::fs::write(sc.path("dir/g"), "x").unwrap();
        let (r, out, _) = run(&["-R", "700", "dir"], &sc);
        assert_eq!(r, 0);
        assert!(!out.contains("neither symbolic link"), "{out}");
    }
}
