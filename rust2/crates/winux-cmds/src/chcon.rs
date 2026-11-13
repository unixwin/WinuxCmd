//! `chcon` — GNU coreutils chcon.c (9.4) semantics, probed byte-for-byte
//! against WSL coreutils 9.4 (rust2/target/probe/w5-runcon.sh, cases F-L).
//!
//! Honest boundary, documented up front: Windows has no SELinux file
//! contexts. The probed GNU non-SELinux host behavior is the oracle:
//! * operand validation matches GNU exactly — `cannot access 'X': No
//!   such file or directory` rc 1 (probed case H), `missing operand` /
//!   `missing operand after 'X'` + try-help rc 1 (probed I, J), option
//!   errors rc 1 (probed K);
//! * a PARTIAL context (-u/-r/-t/-l without a full `user:role:type:range`)
//!   applied to an unlabeled file fails with GNU's wording: `can't apply
//!   partial context to unlabeled file 'X'` rc 1 (probed case G). Every
//!   Windows file is unlabeled in this sense, so the partial branch is
//!   the honest terminal state and byte-matches GNU's unlabeled-file
//!   behavior (stdout empty, rc 1);
//! * a FULL context applied to a file succeeds on the gate's WSL host
//!   (rc 0 silent — that kernel accepts the security.selinux xattr).
//!   Windows cannot set the xattr; the port reports not-supported rc 1
//!   (the 1.x applet's diagnostic). Those rows are win-mode in the gate;
//!   GNU's rc-0 success is recorded in the probe file.
//! * -v prints GNU's verbose line `changing security context of 'X'` to
//!   stdout before the operation (probed case L).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use crate::gnuhelp;
use winux_core::{CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: None,
        long: "dereference",
        takes_value: false,
    },
    OptSpec {
        short: Some('h'),
        long: "no-dereference",
        takes_value: false,
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
    OptSpec {
        short: None,
        long: "reference",
        takes_value: true,
    },
    OptSpec {
        short: Some('u'),
        long: "user",
        takes_value: true,
    },
    OptSpec {
        short: Some('r'),
        long: "role",
        takes_value: true,
    },
    OptSpec {
        short: Some('t'),
        long: "type",
        takes_value: true,
    },
    OptSpec {
        short: Some('l'),
        long: "range",
        takes_value: true,
    },
    OptSpec {
        short: Some('R'),
        long: "recursive",
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
        short: Some('v'),
        long: "verbose",
        takes_value: false,
    },
];

// `--help` body verbatim from `env chcon --help` under WSL coreutils 9.4.
const CHCON_HELP_BODY: &str = r#"Usage: chcon [OPTION]... CONTEXT FILE...
  or:  chcon [OPTION]... [-u USER] [-r ROLE] [-l RANGE] [-t TYPE] FILE...
  or:  chcon [OPTION]... --reference=RFILE FILE...
Change the SELinux security context of each FILE.

Mandatory arguments to long options are mandatory for short options too.
      --dereference      affect the referent of each symbolic link (this is
                         the default), rather than the symbolic link itself
  -h, --no-dereference   affect each symbolic link instead of any referenced
                         file (useful only on systems that can change the
                         type of a symbolic link)
      --preserve-root    fail to operate recursively on '/'
      --no-preserve-root do not treat '/' specially (the default)
      --reference=RFILE  use RFILE's security context rather than specifying
                         a CONTEXT value
  -R, --recursive        operate on files and directories recursively
  -v, --verbose          output a diagnostic for every file processed

The following options modify how a hierarchy is traversed when the -R
option is also specified.  If more than one is specified, only the final
one takes effect.

  -H   if a command line argument is a symbolic link to a directory, traverse it
  -L   traverse every symbolic link to a directory encountered
  -P   do not traverse any symbolic links (default)

"#;

/// A full SELinux context has four colon-separated components
/// (user:role:type:range; the range may itself contain colons, so the
/// count is "at least 4"? chcon.c uses context_split on exactly the
/// colon runs — the probed oracle distinguishes full vs partial: `-t
/// user_t` is partial, `u:r:t:s0` is full). The kernel-side definition
/// this port mirrors: all of user, role, type given as ONE positional.
fn looks_like_full_context(ctx: &str) -> bool {
    ctx.matches(':').count() >= 3
}

pub fn chcon_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("chcon", args, OPTS, io, ctx, 1) {
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
                    CHCON_HELP_BODY,
                    gnuhelp::footer("chcon", "chcon invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("chcon", "Russell Coker and Jim Meyering")
                )?;
                0
            }
        });
    }

    let verbose = p.has("verbose");
    let using_reference = p.has("reference");
    let using_components = p.has("user") || p.has("role") || p.has("type") || p.has("range");

    // chcon.c operand order (probed): without --reference and without
    // component options the FIRST positional is the CONTEXT and the rest
    // are files; missing context/file operands are usage errors.
    let (full_context, files): (Option<String>, Vec<String>) =
        if using_reference || using_components {
            (
                None,
                p.positionals
                    .iter()
                    .map(|a| a.to_string_lossy().into_owned())
                    .collect(),
            )
        } else {
            let mut it = p
                .positionals
                .iter()
                .map(|a| a.to_string_lossy().into_owned());
            let ctx0 = it.next();
            (ctx0, it.collect())
        };

    if p.positionals.is_empty() {
        let _ = winux_core::usage_error(io, "chcon", "missing operand");
        return Ok(1);
    }
    if !using_reference && !using_components && files.is_empty() {
        // probed J: `chcon plain.txt` -> missing operand after 'plain.txt'
        let _ = winux_core::usage_error(
            io,
            "chcon",
            &format!(
                "missing operand after '{}'",
                full_context.as_deref().unwrap_or("")
            ),
        );
        return Ok(1);
    }
    if files.is_empty() {
        // probed (reference/components with no file operand)
        let _ = winux_core::usage_error(io, "chcon", "missing file operand");
        return Ok(1);
    }

    let partial = if using_reference {
        false
    } else if using_components {
        // probed case 5: -u/-r/-t/-l component forms are partial contexts
        true
    } else {
        !full_context
            .as_deref()
            .map(looks_like_full_context)
            .unwrap_or(false)
    };

    // --reference form (probed cases 1-3): GNU reads RFILE's context with
    // getfilecon BEFORE touching any file. On Windows every file is
    // unlabeled, so an accessible reference answers GNU's unlabeled wording
    // ("No data available" — byte-equal to GNU on a non-SELinux host) and a
    // missing reference answers the ENOENT wording; either way rc 1 and no
    // file is processed.
    if let Some(rfile) = p.value_of("reference") {
        if resolve(ctx, rfile).metadata().is_err() {
            let _ = writeln!(
                io.stderr,
                "chcon: failed to get security context of '{rfile}': No such file or directory"
            );
        } else {
            let _ = writeln!(
                io.stderr,
                "chcon: failed to get security context of '{rfile}': No data available"
            );
        }
        return Ok(1);
    }

    let mut failed = false;
    for file in &files {
        let native = resolve(ctx, file);
        if verbose {
            println_to(io, &format!("changing security context of '{file}'"));
        }
        // probed H/case 7: the access check precedes the context work
        if native.metadata().is_err() {
            let _ = writeln!(
                io.stderr,
                "chcon: cannot access '{file}': No such file or directory"
            );
            failed = true;
            continue;
        }
        if partial {
            // probed G: every Windows file is unlabeled
            let _ = writeln!(
                io.stderr,
                "chcon: can't apply partial context to unlabeled file '{file}'"
            );
            failed = true;
            continue;
        }
        // Full context on a real file: GNU on the gate host succeeds
        // (security.selinux xattr accepted, rc 0 silent). Windows has no
        // SELinux: honest not-supported diagnostic, rc 1 (win-mode rows).
        let _ = writeln!(
            io.stderr,
            "chcon: {file}: SELinux file contexts are not supported on Windows"
        );
        failed = true;
    }
    Ok(if failed { 1 } else { 0 })
}

/// GNU's chcon -v line goes to stdout (probed case L: OUT captured it).
fn println_to(io: &mut Io, line: &str) {
    let _ = writeln!(io.stdout, "{line}");
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_in(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = chcon_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn missing_operand_forms() {
        // probed I/J
        let sc = ScratchCtx::new("chcon-ops");
        std::fs::write(sc.path("plain.txt"), "x").unwrap();
        let (r, out, err) = run_in(&sc, &[]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("chcon: missing operand"));
        assert!(err.contains("Try 'chcon --help' for more information."));
        let (r, _, err) = run_in(&sc, &["plain.txt"]);
        assert_eq!(r, 1);
        assert!(err.contains("chcon: missing operand after 'plain.txt'"));
    }

    #[test]
    fn missing_file_operand_with_component() {
        let sc = ScratchCtx::new("chcon-ops2");
        let (r, _, err) = run_in(&sc, &["-t", "type_t"]);
        assert_eq!(r, 1);
        assert!(err.contains("missing file operand") || err.contains("missing operand"));
    }

    #[test]
    fn inaccessible_file_is_enoent() {
        // probed H
        let sc = ScratchCtx::new("chcon-ops3");
        let (r, _, err) = run_in(&sc, &["-t", "user_t", "nosuch"]);
        assert_eq!(r, 1);
        assert!(err.contains("chcon: cannot access 'nosuch': No such file or directory"));
    }

    #[test]
    fn partial_context_on_unlabeled() {
        // probed G/case 5: component forms are partial; every Windows file
        // is unlabeled; wording byte-shape
        let sc = ScratchCtx::new("chcon-partial");
        std::fs::write(sc.path("p2.txt"), "x").unwrap();
        let (r, out, err) = run_in(&sc, &["-t", "type_t", "p2.txt"]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("chcon: can't apply partial context to unlabeled file 'p2.txt'"));
        // -u form same branch (probed case 5)
        let (r, _, err) = run_in(&sc, &["-u", "user_u", "p2.txt"]);
        assert_eq!(r, 1);
        assert!(err.contains("can't apply partial context"));
        // access check precedes the partial branch (probed case 4)
        let (r, _, err) = run_in(&sc, &["-t", "type_t", "nosuch"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot access 'nosuch'"));
    }

    #[test]
    fn positional_context_is_permuted_operand_with_components() {
        // probed case 6: `chcon ctx:user:role:s0 -t type_t f` — GNU
        // permutes, the context-looking string becomes a FILE operand
        // (access error) and the component option still applies partially.
        let sc = ScratchCtx::new("chcon-perm");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, err) = run_in(&sc, &["ctx:user:role:s0", "-t", "type_t", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot access 'ctx:user:role:s0'"));
        assert!(err.contains("can't apply partial context to unlabeled file 'f'"));
    }

    #[test]
    fn full_context_boundary_win() {
        // win-mode row: GNU rc 0 silent on the gate host; the port is
        // honest about the missing SELinux support (rc 1)
        let sc = ScratchCtx::new("chcon-full");
        std::fs::write(sc.path("plain.txt"), "x").unwrap();
        let (r, out, err) = run_in(&sc, &["sys_u:role_r:type_t:s0", "plain.txt"]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("SELinux file contexts are not supported"));
    }

    #[test]
    fn verbose_line_goes_to_stdout() {
        // probed L: `changing security context of 'X'` on stdout; the
        // failing operation then keeps rc 1
        let sc = ScratchCtx::new("chcon-v");
        std::fs::write(sc.path("plain.txt"), "x").unwrap();
        let (r, out, _) = run_in(&sc, &["-v", "sys_u:role_r:type_t:s0", "plain.txt"]);
        assert_eq!(r, 1);
        assert_eq!(out, "changing security context of 'plain.txt'\n");
    }

    #[test]
    fn reference_form_missing_rfile() {
        // probed case 2: ENOENT wording for a missing reference
        let sc = ScratchCtx::new("chcon-ref");
        let (r, _, err) = run_in(&sc, &["--reference=nosuch", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains(
            "chcon: failed to get security context of 'nosuch': No such file or directory"
        ));
    }

    #[test]
    fn reference_form_unlabeled_rfile() {
        // probed case 1: an accessible reference is unlabeled on Windows,
        // matching GNU's non-SELinux "No data available" wording; no file
        // is processed (probed case 3: reference error preempts files)
        let sc = ScratchCtx::new("chcon-ref2");
        std::fs::write(sc.path("ref"), "x").unwrap();
        let (r, _, err) = run_in(&sc, &["--reference=ref", "f"]);
        assert_eq!(r, 1);
        assert!(err.contains("chcon: failed to get security context of 'ref': No data available"));
        let (r, _, err) = run_in(&sc, &["--reference=ref", "nosuchfile"]);
        assert_eq!(r, 1);
        assert!(!err.contains("nosuchfile"));
    }

    #[test]
    fn bogus_option_rc1() {
        // probed K
        let sc = ScratchCtx::new("chcon-bogus");
        std::fs::write(sc.path("plain.txt"), "x").unwrap();
        let (r, _, err) = run_in(&sc, &["--bogus", "plain.txt"]);
        assert_eq!(r, 1);
        assert!(err.contains("unrecognized option '--bogus'"));
    }

    #[test]
    fn help_version_surface() {
        let sc = ScratchCtx::new("chcon-help");
        let (r, out, err) = run_in(&sc, &["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: chcon [OPTION]... CONTEXT FILE...\n"));
        assert!(out.ends_with("info '(coreutils) chcon invocation'\n"));
        let (r, out, _) = run_in(&sc, &["--version"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("chcon (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Russell Coker and Jim Meyering.\n"));
    }

    #[test]
    fn partial_detection_shapes() {
        assert!(looks_like_full_context("u:r:t:s0"));
        assert!(looks_like_full_context("u:r:t:s0:s1:c0"));
        assert!(!looks_like_full_context("user_t"));
        assert!(!looks_like_full_context("u:r"));
    }
}
