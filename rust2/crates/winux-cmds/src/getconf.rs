//! `getconf` — the Windows variable table of the 1.x C++ applet
//! (`src/commands/getconf.cpp` on main) with glibc getconf error semantics:
//!
//! * Variable table (case-insensitive lookup, a documented superset of
//!   glibc's case-sensitive names; values are the Windows model — the
//!   gate's equivalence side for them is the C++ exe):
//!   ARG_MAX/_ARG_MAX -> 32767 (CreateProcess's command-line limit, #1134);
//!   PATH_MAX/_POSIX_PATH_MAX/NAME_MAX/_POSIX_NAME_MAX -> 260 (MAX_PATH);
//!   NPROCESSORS_ONLN/NPROCESSORS_CONF (+_ prefixed) -> GetNativeSystemInfo;
//!   PAGESIZE/PAGE_SIZE -> page size; PHYS_PAGES/AVPHYS_PAGES (+_) ->
//!   GlobalMemoryStatus pages.
//! * `-a/--all` prints the whole table as `NAME value` lines (no header).
//! * `-v SPEC` is accepted and ignored on Windows (glibc compilation
//!   environment spec).
//! * No variable (and no -a): `getconf: no variable specified` + try-help,
//!   rc 1 (glibc probed rc 1; glibc's own usage text goes to stderr).
//! * Unknown variable: `getconf: Unrecognized variable `X'` rc 2 — the
//!   glibc backtick/apostrophe quoting, byte-matched by the C++ applet.
//!   With an uppercase unknown name this gates strictly against GNU.
//! * Two operands are the pathconf form: the variable must be a path
//!   variable (else Unrecognized, rc 2) and the path must exist (else
//!   `getconf: <path>: No such file or directory` rc 1 — a deliberate C++
//!   check glibc does not make, classified `win` in the gate).
//! * More than two operands fall through to per-variable lookup like the
//!   C++ applet (glibc instead treats the extra operand as a path).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::winsys;
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: Some('a'),
        long: "all",
        takes_value: false,
    },
    OptSpec {
        short: Some('v'),
        long: "spec",
        takes_value: true,
    },
];

fn lookup(name: &str) -> Option<String> {
    let lowered = name.to_ascii_lowercase();
    match lowered.as_str() {
        "arg_max" | "_arg_max" => Some("32767".to_string()),
        "path_max" | "_posix_path_max" | "name_max" | "_posix_name_max" => Some("260".to_string()),
        "nprocessors_onln" | "_nprocessors_onln" | "nprocessors_conf" | "_nprocessors_conf" => {
            Some(winsys::imp::num_processors().to_string())
        }
        "page_size" | "pagesize" => Some(winsys::imp::page_size().to_string()),
        "phys_pages" | "_phys_pages" => {
            let (total, _) = winsys::imp::phys_pages();
            Some(total.to_string())
        }
        "avphys_pages" | "_avphys_pages" => {
            let (_, avail) = winsys::imp::phys_pages();
            Some(avail.to_string())
        }
        _ => None,
    }
}

fn is_path_var(name: &str) -> bool {
    let lowered = name.to_ascii_lowercase();
    matches!(
        lowered.as_str(),
        "path_max" | "_posix_path_max" | "name_max" | "_posix_name_max"
    )
}

fn unrecognized(io: &mut Io, ctx: &Ctx, typed: &str) {
    // glibc getconf backtick/apostrophe quoting, byte-matched (getconf.c:
    // "Unrecognized variable `%s'").
    let _ = writeln!(
        io.stderr,
        "getconf: {}",
        tr!(
            ctx,
            "command.getconf.error.unrecognized_variable",
            "Unrecognized variable `{var}'",
            var = typed
        )
    );
}

// glibc getconf --help, verbatim from the WSL oracle (Ubuntu GLIBC
// 2.39-0ubuntu8.9, including its own bug-report footer).
const GETCONF_HELP: &str = r#"Usage: getconf [-v SPEC] VAR
  or:  getconf [-v SPEC] PATH_VAR PATH

Get the configuration value for variable VAR, or for variable PATH_VAR
for path PATH.  If SPEC is given, give values for compilation
environment SPEC.

For bug reporting instructions, please see:
<https://bugs.launchpad.net/ubuntu/+source/glibc/+bugs>.
"#;

// glibc getconf --version, verbatim from the WSL oracle.
const GETCONF_VERSION: &str = r#"getconf (Ubuntu GLIBC 2.39-0ubuntu8.9) 2.39
Copyright (C) 2024 Free Software Foundation, Inc.
This is free software; see the source for copying conditions.  There is NO
warranty; not even for MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
Written by Roland McGrath.
"#;

pub fn getconf_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("getconf", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    // GNU exits at the first --help/--version getopt_long encounters; the
    // ordered find reproduces that for `--version --help` (version wins).
    if let Some(which) = p
        .flags
        .iter()
        .find(|f| f.as_str() == argopt::HELP || f.as_str() == argopt::VERSION)
    {
        return Ok(match which.as_str() {
            "help" => {
                write!(io.stdout, "{}", GETCONF_HELP)?;
                0
            }
            _ => {
                write!(io.stdout, "{}", GETCONF_VERSION)?;
                0
            }
        });
    }
    let all = p.has("all");
    // -v SPEC: accepted, ignored (not applicable on Windows).

    if !all && p.positionals.is_empty() {
        // [GNU] glibc probed rc 1 with its own usage text on stderr.
        let _ = usage_error(
            io,
            "getconf",
            &tr!(
                ctx,
                "command.getconf.error.no_variable_specified",
                "no variable specified"
            ),
        );
        return Ok(1);
    }

    if all {
        let out = &mut *io.stdout;
        let page = winsys::imp::page_size();
        let (phys, avphys) = winsys::imp::phys_pages();
        let ncpu = winsys::imp::num_processors();
        for (name, value) in [
            ("ARG_MAX", "32767".to_string()),
            ("NAME_MAX", "260".to_string()),
            ("PATH_MAX", "260".to_string()),
            ("_POSIX_NAME_MAX", "260".to_string()),
            ("_POSIX_PATH_MAX", "260".to_string()),
            ("PAGESIZE", page.to_string()),
            ("NPROCESSORS_CONF", ncpu.to_string()),
            ("NPROCESSORS_ONLN", ncpu.to_string()),
            ("PHYS_PAGES", phys.to_string()),
            ("AVPHYS_PAGES", avphys.to_string()),
        ] {
            writeln!(out, "{name} {value}")?;
        }
        return Ok(0);
    }

    // Two operands: the pathconf form (glibc getconf PATH_VAR PATH).
    if p.positionals.len() == 2 {
        let var = p.positionals[0].to_string_lossy().into_owned();
        let path = p.positionals[1].to_string_lossy().into_owned();
        if !is_path_var(&var) {
            unrecognized(io, ctx, &var);
            return Ok(2);
        }
        if !crate::fsutil::resolve(ctx, &path).exists()
            // a DOS device operand ("NUL", /dev/null) answers no metadata
            // query on this host but names a real device: GNU getconf
            // PATH_VAR /dev/null succeeds (the 1.x attribute probes saw
            // the device too)
            && winux_core::path::resolve_pseudo_device(&path).is_none()
            && !winux_core::path::is_dos_device_name(&path)
        {
            let _ = writeln!(
                io.stderr,
                "getconf: {}",
                tr!(
                    ctx,
                    "command.getconf.error.no_such_file",
                    "{path}: No such file or directory",
                    path = path
                )
            );
            return Ok(1);
        }
        match lookup(&var) {
            Some(value) => {
                writeln!(io.stdout, "{value}")?;
                Ok(0)
            }
            None => {
                unrecognized(io, ctx, &var);
                Ok(2)
            }
        }
    } else {
        // One operand (or 3+ like the C++ applet: per-variable lookup).
        let mut exit_code = 0;
        for var in &p.positionals {
            let typed = var.to_string_lossy().into_owned();
            match lookup(&typed) {
                Some(value) => {
                    writeln!(io.stdout, "{value}")?;
                }
                None => {
                    unrecognized(io, ctx, &typed);
                    exit_code = 2;
                }
            }
        }
        Ok(exit_code)
    }
}

#[cfg(test)]
mod tests {
    // [GNU] glibc getconf help/version, verbatim from the WSL oracle
    // (strict rows gc19/gc23).
    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: getconf [-v SPEC] VAR\n"));
        assert!(out.ends_with("<https://bugs.launchpad.net/ubuntu/+source/glibc/+bugs>.\n"));
        let (r, out, _) = run(&["--version"]);
        assert_eq!(r, 0);
        assert!(
            out.starts_with("getconf (Ubuntu GLIBC 2.39-0ubuntu8.9) 2.39\n"),
            "got {out}"
        );
        assert!(out.ends_with("Written by Roland McGrath.\n"));
    }

    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("getconf");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::getconf_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn known_variables() {
        let (r, out, _) = run(&["ARG_MAX"]);
        assert_eq!(r, 0);
        assert_eq!(out, "32767\n");
        let (r, out, _) = run(&["PATH_MAX"]);
        assert_eq!(r, 0);
        assert_eq!(out, "260\n");
        // case-insensitive lookup (C++ model)
        let (r, out, _) = run(&["path_max"]);
        assert_eq!(r, 0);
        assert_eq!(out, "260\n");
        let (r, out, _) = run(&["NPROCESSORS_ONLN"]);
        assert_eq!(r, 0);
        assert!(out.trim().parse::<u32>().is_ok());
        let (r, out, _) = run(&["PAGESIZE"]);
        assert_eq!(r, 0);
        assert_eq!(out.trim().parse::<u32>().unwrap(), 4096);
        let (r, out, _) = run(&["PHYS_PAGES"]);
        assert_eq!(r, 0);
        assert!(out.trim().parse::<u64>().unwrap() > 0);
    }

    #[test]
    fn unknown_variable_rc2() {
        let (r, out, err) = run(&["NOSUCHVAR"]);
        assert_eq!(r, 2);
        assert!(out.is_empty());
        assert!(err.contains("Unrecognized variable `NOSUCHVAR'"));
    }

    #[test]
    fn no_variable_rc1() {
        let (r, out, err) = run(&[]);
        assert_eq!(r, 1);
        assert!(out.is_empty());
        assert!(err.contains("no variable specified"));
        assert!(err.contains("Try 'getconf --help'"));
    }

    #[test]
    fn all_flag_dump() {
        let (r, out, _) = run(&["-a"]);
        assert_eq!(r, 0);
        assert!(out.starts_with("ARG_MAX 32767\n"));
        assert!(out.contains("PAGESIZE 4096\n"));
        assert_eq!(out.lines().count(), 10);
        let (r, _, _) = run(&["--all"]);
        assert_eq!(r, 0);
    }

    #[test]
    fn pathconf_form() {
        let sc = ScratchCtx::new("getconf2");
        std::fs::write(sc.path("f"), "x").unwrap();
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["PATH_MAX".into(), "f".into()];
        let r = super::getconf_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert_eq!(io.out(), "260\n");
        // non-path variable in the path form: Unrecognized rc 2
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["ARG_MAX".into(), "f".into()];
        let r = super::getconf_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 2);
        assert!(io.err().contains("Unrecognized variable `ARG_MAX'"));
        // missing path: rc 1 (C++ existence check)
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = vec!["PATH_MAX".into(), "missing".into()];
        let r = super::getconf_cmd(&owned, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("No such file or directory"));
    }

    #[test]
    fn three_operands_fall_through() {
        let (r, out, err) = run(&["ARG_MAX", "PATH_MAX", "NOSUCH"]);
        assert_eq!(r, 2);
        assert_eq!(out, "32767\n260\n");
        assert!(err.contains("Unrecognized variable `NOSUCH'"));
    }

    #[test]
    fn v_spec_accepted_ignored() {
        let (r, out, _) = run(&["-v", "POSIX", "ARG_MAX"]);
        assert_eq!(r, 0);
        assert_eq!(out, "32767\n");
        let (r, _, _) = run(&["-x"]);
        assert_eq!(r, 1); // unknown option via getopt (C++ framework shape)
    }
}
