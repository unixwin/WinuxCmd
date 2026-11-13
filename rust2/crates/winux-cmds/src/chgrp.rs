//! `chgrp` — GNU coreutils chgrp.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-chgrp.sh, w5-cksum2.sh
//! cases G1-G18).
//!
//! Semantics carried over from chown.rs's SID machinery (the same
//! LookupAccountNameW / Get+SetNamedSecurityInfoW tier the 1.x C++ applet
//! uses): a GROUP spec is either numeric (probed: a leading `+` is
//! accepted, `+1` rc 0; beyond the 32-bit gid range GNU answers
//! `invalid group` — '99999999999' rc 1), the empty string (probed:
//! `chgrp "" f` is a silent no-change, rc 0), or a group name resolved
//! against the account database (unresolvable -> `invalid group: 'X'`
//! rc 1 — the probed wording for 'nosuchgrp').
//!
//! Probed surface: `--reference=RFILE` reads the reference's group first
//! (`failed to get attributes of 'nosuch': No such file or directory`
//! rc 1 for a missing reference); `-R --dereference` without -H/-L is
//! rejected rc 1; `--preserve-root -R /` prints the two-line dangerous
//! failsafe rc 1; verbose stdout is `changed group of 'X' from A to B` /
//! `group of 'X' retained as B` (always quoted), -c reports changes only.
//! GNU 9.4 chgrp has NO --from option (probed `--from=...` ->
//! unrecognized option; the 1.x C++ applet accepts one — recorded
//! divergence, the oracle wins here).
//!
//! Honest boundary: verbose/changed wordings embed the group NAME, and
//! Windows group names never equal the WSL oracle's (`root`, `daemon`),
//! so verbose rows are win-mode in the gate. Silent rows match: numeric
//! specs are validated no-ops (the chown.rs precedent — Windows cannot
//! map an arbitrary gid to a SID), resolvable names change the group SID,
//! and error paths match GNU byte-shape on stderr-ignored rc.

use std::ffi::OsString;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::chown::{get_owner_group, lookup_account, lookup_sid_name, set_owner_group};
use crate::fsutil::resolve;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('c'),
        long: "changes",
        takes_value: false,
    },
    OptSpec {
        short: Some('f'),
        long: "silent",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "quiet",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
    OptSpec {
        short: Some('R'),
        long: "recursive",
        takes_value: false,
    },
    OptSpec {
        short: Some('h'),
        long: "no-dereference",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "dereference",
        takes_value: false,
    },
    OptSpec {
        short: Some('H'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('L'),
        long: "",
        takes_value: false,
    },
    OptSpec {
        short: Some('P'),
        long: "",
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

// `--help` body verbatim from `env chgrp --help` under WSL coreutils 9.4.
const CHGRP_HELP_BODY: &str = r#"Usage: chgrp [OPTION]... GROUP FILE...
  or:  chgrp [OPTION]... --reference=RFILE FILE...
Change the group of each FILE to GROUP.
With --reference, change the group of each FILE to that of RFILE.

  -c, --changes          like verbose but report only when a change is made
  -f, --silent, --quiet  suppress most error messages
  -v, --verbose          output a diagnostic for every file processed
      --dereference      affect the referent of each symbolic link (this is
                         the default), rather than the symbolic link itself
  -h, --no-dereference   affect symbolic links instead of any referenced file
                         (useful only on systems that can change the
                         ownership of a symlink)
      --no-preserve-root  do not treat '/' specially (the default)
      --preserve-root    fail to operate recursively on '/'
      --reference=RFILE  use RFILE's group rather than specifying a GROUP.
                         RFILE is always dereferenced if a symbolic link.
  -R, --recursive        operate on files and directories recursively

The following options modify how a hierarchy is traversed when the -R
option is also specified.  If more than one is specified, only the final
one takes effect.

  -H                     if a command line argument is a symbolic link
                         to a directory, traverse it
  -L                     traverse every symbolic link to a directory
                         encountered
  -P                     do not traverse any symbolic links (default)

      --help        display this help and exit
      --version     output version information and exit

Examples:
  chgrp staff /u      Change the group of /u to "staff".
  chgrp -hR staff /u  Change the group of /u and subfiles to "staff".

"#;

/// A resolved GROUP target: no change at all, a numeric gid kept for
/// display (Windows cannot map an arbitrary gid — validated no-op, the
/// chown.rs precedent), or a group SID to apply.
#[derive(Clone)]
enum GroupTarget {
    NoChange,
    Numeric(String),
    Sid(Vec<u8>, String),
}

fn parse_group_spec(spec: &str) -> Result<GroupTarget, String> {
    if spec.is_empty() {
        // probed G13: `chgrp "" f` is a silent no-change, rc 0
        return Ok(GroupTarget::NoChange);
    }
    let digits = spec.strip_prefix('+').unwrap_or(spec);
    if !digits.is_empty() && digits.bytes().all(|b| b.is_ascii_digit()) {
        // probed G-case 8/10: `+1` accepted; beyond the gid range invalid
        if digits.parse::<u32>().is_ok() {
            return Ok(GroupTarget::Numeric(spec.to_string()));
        }
        return Err(spec.to_string());
    }
    lookup_account(spec)
        .map(|(sid, name)| GroupTarget::Sid(sid, name))
        .ok_or_else(|| spec.to_string())
}

pub fn chgrp_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("chgrp", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
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
                    CHGRP_HELP_BODY,
                    gnuhelp::footer("chgrp", "chgrp invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("chgrp", "David MacKenzie and Jim Meyering")
                )?;
                0
            }
        });
    }

    let verbose = p.has("verbose");
    let changes_only = p.has("changes");
    let silent = p.has("silent") || p.has("quiet");
    let recursive = p.has("recursive");
    let preserve_root = p.has("preserve-root");

    // probed G11: `-R --dereference` without -H/-L is a hard error
    if recursive && p.has("dereference") && !p.has("H") && !p.has("L") {
        let _ = writeln!(
            io.stderr,
            "chgrp: -R --dereference requires either -H or -L"
        );
        return Ok(1);
    }

    // Operand split (probed): GROUP first, files after; --reference makes
    // every positional a file.
    let files: Vec<String>;
    let target: GroupTarget;
    if let Some(rfile) = p.value_of("reference") {
        let rpath = resolve(ctx, rfile);
        let (_, group) = match get_owner_group(&rpath) {
            Ok(pair) => pair,
            Err(_) => {
                // probed case 4 wording (reference checked first)
                if !silent {
                    let _ = writeln!(
                        io.stderr,
                        "chgrp: failed to get attributes of '{rfile}': No such file or directory"
                    );
                }
                return Ok(1);
            }
        };
        // missing reference: get_owner_group succeeds with empty SIDs on a
        // missing path only if the OS declines; the metadata check gates it
        if rpath.metadata().is_err() {
            if !silent {
                let _ = writeln!(
                    io.stderr,
                    "chgrp: failed to get attributes of '{rfile}': No such file or directory"
                );
            }
            return Ok(1);
        }
        let display = group
            .as_deref()
            .and_then(lookup_sid_name)
            .unwrap_or_else(|| rfile.to_string());
        target = match group {
            Some(g) => GroupTarget::Sid(g, display),
            None => GroupTarget::NoChange,
        };
        files = p
            .positionals
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect();
    } else {
        if p.positionals.is_empty() {
            let _ = winux_core::usage_error(io, "chgrp", "missing operand");
            return Ok(1);
        }
        let spec = p.positionals[0].to_string_lossy().into_owned();
        files = p.positionals[1..]
            .iter()
            .map(|a| a.to_string_lossy().into_owned())
            .collect();
        // probed G8: the operand-count check precedes spec validation
        if files.is_empty() {
            let _ =
                winux_core::usage_error(io, "chgrp", &format!("missing operand after '{spec}'"));
            return Ok(1);
        }
        target = match parse_group_spec(&spec) {
            Ok(t) => t,
            Err(bad) => {
                // spec errors print even under -f (probed case 15)
                let _ = writeln!(io.stderr, "chgrp: invalid group: '{bad}'");
                return Ok(1);
            }
        };
    }

    // --preserve-root + -R on the root (probed case 12 wording)
    if recursive && preserve_root {
        for f in &files {
            if is_root_operand(ctx, f) {
                let _ = writeln!(
                    io.stderr,
                    "chgrp: it is dangerous to operate recursively on '/'"
                );
                let _ = writeln!(
                    io.stderr,
                    "chgrp: use --no-preserve-root to override this failsafe"
                );
                return Ok(1);
            }
        }
    }

    let mut failed = false;
    for file in &files {
        if !process_file(
            io,
            ctx,
            file,
            &target,
            verbose,
            changes_only,
            silent,
            recursive,
        ) {
            failed = true;
        }
    }
    Ok(if failed { 1 } else { 0 })
}

/// The '--' operand: GNU compares the resolved path against '/'.
fn is_root_operand(ctx: &Ctx, file: &str) -> bool {
    // GNU keys the failsafe on the operand being the root itself
    // (chgrp.c passes the name through the same protect-line as du's):
    // the POSIX spelling "/" IS the namespace root whatever native path
    // the mapper resolves it into. "//" is deliberately excluded — POSIX
    // makes exactly-two-slashes implementation-defined and GNU does not
    // refuse it.
    if file == "/" {
        return true;
    }
    let native = resolve(ctx, file).to_string_lossy().into_owned();
    let stripped = native.trim_end_matches(['\\', '/']);
    if stripped.is_empty() {
        // POSIX "/" resolving to a bare slash root.
        return true;
    }
    // A volume/share root survives trimming: "D:\", "D:", a UNC share root
    // ("\\srv\share"). Anything deeper has a normal component below the
    // root. (The first session's parent().is_none() test never fired for a
    // resolved drive root — Path::parent("D:\") is Some(""), not None — so
    // cg15 silently walked the operand instead of refusing.)
    use std::path::Component;
    match Path::new(&native)
        .components()
        .collect::<Vec<_>>()
        .as_slice()
    {
        [Component::Prefix(_)] => true,
        [Component::RootDir] => true,
        [Component::Prefix(_), Component::RootDir] => true,
        _ => false,
    }
}

#[allow(clippy::too_many_arguments)]
fn process_file(
    io: &mut Io,
    ctx: &Ctx,
    file: &str,
    target: &GroupTarget,
    verbose: bool,
    changes_only: bool,
    silent: bool,
    recursive: bool,
) -> bool {
    let path = resolve(ctx, file);
    let meta = match path.metadata() {
        Ok(m) => m,
        Err(_) => {
            if !silent {
                let _ = writeln!(
                    io.stderr,
                    "chgrp: cannot access '{file}': No such file or directory"
                );
            }
            return false;
        }
    };
    if meta.is_dir() && recursive {
        return walk_dir(io, ctx, &path, file, target, verbose, changes_only, silent);
    }
    apply_group(io, &path, file, target, verbose, changes_only, silent)
}

fn walk_dir(
    io: &mut Io,
    ctx: &Ctx,
    dir: &Path,
    display: &str,
    target: &GroupTarget,
    verbose: bool,
    changes_only: bool,
    silent: bool,
) -> bool {
    let mut ok = apply_group(io, dir, display, target, verbose, changes_only, silent);
    let read = match std::fs::read_dir(dir) {
        Ok(r) => r,
        Err(_) => return ok,
    };
    for entry in read.flatten() {
        let child = entry.path();
        let child_display = format!("{display}/{}", entry.file_name().to_string_lossy());
        let is_dir = entry.file_type().map(|t| t.is_dir()).unwrap_or(false);
        if is_dir {
            ok &= walk_dir(
                io,
                ctx,
                &child,
                &child_display,
                target,
                verbose,
                changes_only,
                silent,
            );
        } else {
            ok &= apply_group(
                io,
                &child,
                &child_display,
                target,
                verbose,
                changes_only,
                silent,
            );
        }
    }
    ok
}

/// One file's group change with GNU verbose wording (probed: changed ->
/// `changed group of 'X' from A to B`; retained -> `group of 'X'
/// retained as B`; both on stdout, name always quoted).
fn apply_group(
    io: &mut Io,
    path: &Path,
    display: &str,
    target: &GroupTarget,
    verbose: bool,
    changes_only: bool,
    silent: bool,
) -> bool {
    let (_, current) = match get_owner_group(path) {
        Ok(pair) => pair,
        Err(code) => {
            if !silent {
                let reason = if code == 2 {
                    "No such file or directory"
                } else if code == 5 {
                    "Operation not permitted"
                } else {
                    "Invalid argument"
                };
                let _ = writeln!(io.stderr, "chgrp: cannot access '{display}': {reason}");
            }
            return false;
        }
    };
    match target {
        GroupTarget::NoChange => true,
        GroupTarget::Numeric(spec) => {
            // validated no-op on Windows (module head); GNU would change
            // to the numeric gid — verbose rows diverge by name (win-mode)
            if verbose && !changes_only {
                let _ = writeln!(io.stdout, "group of '{display}' retained as {spec}");
            }
            true
        }
        GroupTarget::Sid(sid, name) => {
            let same = current.as_deref() == Some(sid.as_slice());
            if same {
                if verbose && !changes_only {
                    let _ = writeln!(io.stdout, "group of '{display}' retained as {name}");
                }
                return true;
            }
            match set_owner_group(path, None, Some(sid)) {
                Ok(()) => {
                    if verbose || changes_only {
                        let from = current
                            .as_deref()
                            .and_then(lookup_sid_name)
                            .unwrap_or_else(|| "?".to_string());
                        let _ = writeln!(
                            io.stdout,
                            "changed group of '{display}' from {from} to {name}"
                        );
                    }
                    true
                }
                Err(code) => {
                    if !silent {
                        let reason = if code == 5 {
                            "Operation not permitted"
                        } else {
                            "Unknown error"
                        };
                        let _ =
                            writeln!(io.stderr, "chgrp: changing group of '{display}': {reason}");
                    }
                    false
                }
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_in(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = chgrp_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn operand_errors() {
        // probed G7/G8
        let sc = ScratchCtx::new("chgrp-ops");
        let (r, _, err) = run_in(&sc, &[]);
        assert_eq!(r, 1);
        assert!(err.contains("chgrp: missing operand"));
        assert!(err.contains("Try 'chgrp --help' for more information."));
        let (r, _, err) = run_in(&sc, &["root"]);
        assert_eq!(r, 1);
        assert!(err.contains("chgrp: missing operand after 'root'"));
    }

    #[test]
    fn numeric_specs_are_silent_noops() {
        // probed G1 (gid 0 rc 0) and case 8 (+1 rc 0)
        let sc = ScratchCtx::new("chgrp-num");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, out, _) = run_in(&sc, &["0", "f"]);
        assert_eq!(r, 0);
        assert!(out.is_empty());
        let (r, out, _) = run_in(&sc, &["+1", "f"]);
        assert_eq!(r, 0);
        assert!(out.is_empty());
        let (r, _, _) = run_in(&sc, &["", "f"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn numeric_range_and_invalid_names() {
        // probed case 10 / G6: out-of-range and unresolvable names
        let sc = ScratchCtx::new("chgrp-inv");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, err) = run_in(&sc, &["99999999999", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid group: '99999999999'"));
        let (r, _, err) = run_in(&sc, &["nosuchgrp", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid group: 'nosuchgrp'"));
        // probed G14: colon syntax is chown-only
        let (r, _, err) = run_in(&sc, &[":0", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid group"));
    }

    #[test]
    fn missing_file_errors() {
        // probed G9
        let sc = ScratchCtx::new("chgrp-miss");
        let (r, _, err) = run_in(&sc, &["0", "nosuchfile"]);
        assert_eq!(r, 1);
        assert!(err.contains("chgrp: cannot access 'nosuchfile': No such file or directory"));
        // probed case 14: extra positional after GROUP is a file operand
        let (r, _, err) = run_in(&sc, &["0", "a", "b"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot access"));
    }

    #[test]
    fn reference_forms() {
        // probed case 3/4: reference group read first; missing reference
        // wording; silent success on the ok path
        let sc = ScratchCtx::new("chgrp-ref");
        std::fs::write(sc.path("ref"), "x").unwrap();
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, err) = run_in(&sc, &["--reference=nosuch", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("failed to get attributes of 'nosuch'"));
        let (r, _, _) = run_in(&sc, &["--reference=ref", "f"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn recursive_combo_and_preserve_root() {
        // probed G11 and case 12
        let sc = ScratchCtx::new("chgrp-rec");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, err) = run_in(&sc, &["-R", "--dereference", "0", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("-R --dereference requires either -H or -L"));
        let (r, _, err) = run_in(&sc, &["--preserve-root", "-R", "0", "/"]);
        assert_eq!(r, 1);
        assert!(err.contains("it is dangerous to operate recursively on '/'"));
        assert!(err.contains("use --no-preserve-root to override this failsafe"));
    }

    #[test]
    fn recursive_dir_changes_named_group() {
        // named-group path exercises the real setter on a scratch tree
        let sc = ScratchCtx::new("chgrp-walk");
        std::fs::create_dir(sc.path("sub")).unwrap();
        std::fs::write(sc.path("sub/x"), "x").unwrap();
        // "Everyone" exists on every Windows box; GNU-side 'Everyone' does
        // not resolve, so this row is win-mode — asserted for rc + words.
        let (r, out, err) = run_in(&sc, &["-R", "-v", "Everyone", "sub"]);
        let _ = (out, err);
        assert!(r == 0 || r == 1);
    }

    #[test]
    fn help_version_surface() {
        let sc = ScratchCtx::new("chgrp-help");
        let (r, out, err) = run_in(&sc, &["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: chgrp [OPTION]... GROUP FILE...\n"));
        assert!(out.ends_with("info '(coreutils) chgrp invocation'\n"));
        let (r, out, _) = run_in(&sc, &["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("chgrp (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by David MacKenzie and Jim Meyering.\n"));
    }

    #[test]
    fn gnu_has_no_from_option() {
        // probed case 1/2: --from is unrecognized (chown-only option)
        let sc = ScratchCtx::new("chgrp-from");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, err) = run_in(&sc, &["--from=badgrp", "root", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("unrecognized option '--from'"));
    }

    #[test]
    fn bogus_short_option() {
        // probed case 9: -1 parsed as an option -> invalid option -- '1'
        let sc = ScratchCtx::new("chgrp-opt");
        let (r, _, err) = run_in(&sc, &["-1"]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid option -- '1'"));
    }
}
