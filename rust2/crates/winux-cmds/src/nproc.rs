//! `nproc` — GNU coreutils semantics (probed against coreutils 9.4; the
//! OpenMP rules mirror lib/nproc.c parse_omp_threads_env and nproc.c's
//! xnumtoumax --ignore parsing, which the 1.x C++ applet already models on
//! main).
//!
//! * Default: processors AVAILABLE to this process (its affinity set) via
//!   GetProcessAffinityMask popcount; `--all`: every installed processor.
//! * A valid OMP_NUM_THREADS replaces the count outright (NOT clamped to
//!   the processor count — `OMP_NUM_THREADS=99999 nproc` prints 99999);
//!   OMP_THREAD_LIMIT caps the result; an unparsable value is ignored as
//!   if unset (probed: `OMP_NUM_THREADS=bad nproc` prints the affinity
//!   count). Values are parsed like parse_omp_threads_env: optional leading
//!   blanks, positive decimal digits, trailing blanks, then either end or
//!   a ','-separated nesting list (first value wins, `3,1` -> 3).
//! * `--ignore N` applies after that selection; the result is at least 1
//!   (`nproc --ignore=99999` prints 1). N parses with an optional leading
//!   '+'; junk is `nproc: invalid number: 'X'`, rc 1.
//! * Any operand is `extra operand 'X'`, rc 1.
//!
//! Windows/WSL affinity counts differ, so plain/--all/--ignore-invalid-env
//! outputs are platform-inherent (gate `win`), while OMP-driven cases
//! (OMP_NUM_THREADS=3 -> "3") are byte-identical to GNU (gate `strict`).

use std::ffi::OsString;

use crate::argopt::{self, OptSpec};
use crate::gnuhelp;
use crate::winsys;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[
    OptSpec {
        short: None,
        long: "all",
        takes_value: false,
    },
    OptSpec {
        short: None,
        long: "ignore",
        takes_value: true,
    },
];

/// [GNU] lib/nproc.c parse_omp_threads_env(): skip leading blanks, parse a
/// positive decimal integer, accept trailing blanks and a ','-separated
/// nesting list; anything else behaves as if the variable were unset (0).
pub fn parse_omp_threads(value: Option<&str>) -> u64 {
    let Some(s) = value else { return 0 };
    let s = s.trim_start_matches([' ', '\t', '\n', '\u{0b}', '\u{0c}', '\r']);
    let digits: &str = {
        let end = s
            .bytes()
            .position(|b| !b.is_ascii_digit())
            .unwrap_or(s.len());
        &s[..end]
    };
    if digits.is_empty() {
        return 0;
    }
    let rest = &s[digits.len()..];
    let rest = rest.trim_start_matches([' ', '\t', '\n', '\u{0b}', '\u{0c}', '\r']);
    if !(rest.is_empty() || rest.starts_with(',')) {
        return 0;
    }
    digits.parse::<u64>().unwrap_or(u64::MAX)
}

/// [GNU] nproc.c parses --ignore with xnumtoumax(base 10, minimum 0): a
/// leading '+' is allowed; '-' or any junk is an invalid number.
pub fn parse_ignore(text: &str) -> Option<u64> {
    let s = text.strip_prefix('+').unwrap_or(text);
    if s.is_empty() || !s.bytes().all(|b| b.is_ascii_digit()) {
        return None;
    }
    s.parse::<u64>().ok()
}

// `--help` body verbatim from `env nproc --help` under WSL coreutils 9.4,
// minus the shared gnuhelp::footer tail.
const NPROC_HELP_BODY: &str = r#"Usage: nproc [OPTION]...
Print the number of processing units available to the current process,
which may be less than the number of online processors

      --all      print the number of installed processors
      --ignore=N  if possible, exclude N processing units
      --help        display this help and exit
      --version     output version information and exit

"#;

pub fn nproc_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("nproc", args, OPTS, io, ctx, 1) {
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
                write!(
                    io.stdout,
                    "{}{}",
                    NPROC_HELP_BODY,
                    gnuhelp::footer("nproc", "nproc invocation")
                )?;
                0
            }
            _ => {
                write!(
                    io.stdout,
                    "{}",
                    gnuhelp::coreutils_version("nproc", "Giuseppe Scrivano")
                )?;
                0
            }
        });
    }
    if let Some(first) = p.positionals.first() {
        let shown = first.to_string_lossy().into_owned();
        let _ = usage_error(io, "nproc", &format!("extra operand '{shown}'"));
        return Ok(1);
    }

    let ignore = match p.value_of("ignore") {
        Some(raw) => match parse_ignore(raw) {
            Some(v) => Some(v),
            None => {
                let _ = writeln!(io.stderr, "nproc: invalid number: '{raw}'");
                return Ok(1);
            }
        },
        None => None,
    };

    let mut nproc = if p.has("all") {
        winsys::imp::num_processors() as u64
    } else {
        winsys::imp::available_processors().unwrap_or_else(|| winsys::imp::num_processors()) as u64
    };

    if !p.has("all") {
        let omp_limit = parse_omp_threads(
            ctx.env
                .get("OMP_THREAD_LIMIT")
                .map(|v| v.to_string_lossy().into_owned())
                .as_deref(),
        );
        let limit = if omp_limit == 0 { u64::MAX } else { omp_limit };
        let omp_threads = parse_omp_threads(
            ctx.env
                .get("OMP_NUM_THREADS")
                .map(|v| v.to_string_lossy().into_owned())
                .as_deref(),
        );
        nproc = if omp_threads != 0 {
            omp_threads.min(limit)
        } else {
            nproc.min(limit)
        };
    }

    if let Some(n) = ignore {
        nproc = if n < nproc { nproc - n } else { 1 };
    }

    writeln!(io.stdout, "{nproc}")?;
    Ok(0)
}

#[cfg(test)]
mod tests {
    // [GNU] `--help`/`--version` print the probed coreutils text, rc 0
    // (strict rows np10/np21).
    #[test]
    fn gnu_help_version_surface() {
        let (r, out, err) = run(&["--help"], &[]);
        assert_eq!(r, 0);
        assert!(err.is_empty());
        assert!(out.starts_with("Usage: nproc [OPTION]...\n"));
        assert!(out.ends_with("info '(coreutils) nproc invocation'\n"));
        let (r, out, _) = run(&["--version"], &[]);
        assert_eq!(r, 0);
        assert!(out.starts_with("nproc (GNU coreutils) 9.4\n"));
        assert!(out.ends_with("Written by Giuseppe Scrivano.\n"));
    }

    use winux_core::test_support::{MemIo, ScratchCtx};

    use super::parse_omp_threads;

    fn run(args: &[&str], env: &[(&str, &str)]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let mut sc = ScratchCtx::new("nproc");
        for (k, v) in env {
            sc.env.set(k, v);
        }
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::nproc_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    use std::ffi::OsString;

    #[test]
    fn omp_thread_parsing() {
        assert_eq!(parse_omp_threads(Some("3")), 3);
        assert_eq!(parse_omp_threads(Some(" 5 ")), 5);
        assert_eq!(parse_omp_threads(Some("3,1")), 3); // nesting list
        assert_eq!(parse_omp_threads(Some("bad")), 0); // unset
        assert_eq!(parse_omp_threads(Some("")), 0);
        assert_eq!(parse_omp_threads(Some("3x")), 0);
        assert_eq!(parse_omp_threads(None), 0);
        assert_eq!(parse_omp_threads(Some("99999999999999999999")), u64::MAX);
    }

    #[test]
    fn valid_omp_env_is_gnu_identical() {
        // OMP-driven counts are platform-independent.
        let (r, out, _) = run(&[], &[("OMP_NUM_THREADS", "3")]);
        assert_eq!(r, 0);
        assert_eq!(out, "3\n");
        let (r, out, _) = run(&["--ignore=1"], &[("OMP_NUM_THREADS", "3")]);
        assert_eq!(r, 0);
        assert_eq!(out, "2\n");
        let (r, out, _) = run(&[], &[("OMP_NUM_THREADS", "99999")]);
        assert_eq!(r, 0);
        assert_eq!(out, "99999\n"); // not clamped to the processor count
        let (r, out, _) = run(&[], &[("OMP_NUM_THREADS", "3,1")]);
        assert_eq!(out, "3\n");
        assert_eq!(r, 0);
        let (r, out, _) = run(&[], &[("OMP_NUM_THREADS", "99"), ("OMP_THREAD_LIMIT", "5")]);
        assert_eq!(out, "5\n");
        assert_eq!(r, 0);
        let (r, out, _) = run(&[], &[("OMP_NUM_THREADS", "0")]);
        assert_eq!(r, 0); // 0 = unset -> platform count, rc 0
        assert!(out.trim().parse::<u64>().is_ok());
    }

    #[test]
    fn invalid_env_ignored_but_rc0() {
        let (r, out, _) = run(&[], &[("OMP_NUM_THREADS", "bad")]);
        assert_eq!(r, 0);
        assert!(out.trim().parse::<u64>().is_ok());
        let (r, out, _) = run(&[], &[("OMP_THREAD_LIMIT", "abc")]);
        assert_eq!(r, 0);
        assert!(out.trim().parse::<u64>().is_ok());
    }

    #[test]
    fn ignore_rules() {
        // --ignore=99999 clamps at 1 (GNU probed).
        let (r, out, _) = run(&["--ignore=99999"], &[]);
        assert_eq!(r, 0);
        assert_eq!(out, "1\n");
        let (r, _, err) = run(&["--ignore", "bad"], &[]);
        assert_eq!(r, 1);
        assert!(err.contains("invalid number: 'bad'"));
        let (r, _, _) = run(&["--ignore=+3"], &[]);
        assert_eq!(r, 0); // leading '+' allowed
    }

    #[test]
    fn extra_operand_and_flags() {
        let (r, _, err) = run(&["extra"], &[]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand 'extra'"));
        let (r, _, _) = run(&["--bogus"], &[]);
        assert_eq!(r, 1);
        let (r, _, _) = run(&["--help"], &[]);
        assert_eq!(r, 0);
    }
}
