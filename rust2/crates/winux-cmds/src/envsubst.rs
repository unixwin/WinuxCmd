//! `envsubst` — GNU gettext envsubst semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Substitutes `$VAR` and `${VAR}` in stdin using `ctx.env` (explicit
//! `EnvSource`, no process-global `std::env::var`), optionally restricted
//! to variables appearing in `SHELL-FORMAT`, or lists variables with `-v`.

use std::collections::BTreeSet;
use std::ffi::OsString;
use std::io::{Read, Write};

use crate::argopt;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const ENVSUBST_HELP: &str = "\
Usage: envsubst [OPTION] [SHELL-FORMAT]

Substitutes the values of environment variables.

Operation mode:
  -v, --variables             output the variables occurring in SHELL-FORMAT

Informative output:
  -h, --help                  display this help and exit
  -V, --version               output version information and exit

In normal operation mode, standard input is copied to standard output,
with references to environment variables of the form $VARIABLE or ${VARIABLE}
being replaced with the corresponding values.  If a SHELL-FORMAT is given,
only those environment variables that are referenced in SHELL-FORMAT are
substituted; otherwise all environment variables references occurring in
standard input are substituted.
";

#[inline]
fn is_var_start(b: u8) -> bool {
    b.is_ascii_alphabetic() || b == b'_'
}

#[inline]
fn is_var_char(b: u8) -> bool {
    b.is_ascii_alphanumeric() || b == b'_'
}

fn extract_variables(text: &[u8]) -> BTreeSet<String> {
    let mut vars = BTreeSet::new();
    let mut i = 0;
    while i < text.len() {
        if text[i] != b'$' || i + 1 >= text.len() {
            i += 1;
            continue;
        }
        if text[i + 1] == b'{' {
            let name_start = i + 2;
            if name_start < text.len() && is_var_start(text[name_start]) {
                let mut pos = name_start + 1;
                while pos < text.len() && is_var_char(text[pos]) {
                    pos += 1;
                }
                if pos < text.len() && text[pos] == b'}' {
                    if let Ok(name) = std::str::from_utf8(&text[name_start..pos]) {
                        vars.insert(name.to_owned());
                    }
                    i = pos + 1;
                    continue;
                }
            }
            i += 1;
            continue;
        }
        if is_var_start(text[i + 1]) {
            let name_start = i + 1;
            let mut pos = name_start + 1;
            while pos < text.len() && is_var_char(text[pos]) {
                pos += 1;
            }
            if let Ok(name) = std::str::from_utf8(&text[name_start..pos]) {
                vars.insert(name.to_owned());
            }
            i = pos;
            continue;
        }
        i += 1;
    }
    vars
}

fn should_replace(name: &str, allowlist: Option<&BTreeSet<String>>) -> bool {
    match allowlist {
        Some(set) => set.contains(name),
        None => true,
    }
}

fn substitute(input: &[u8], allowlist: Option<&BTreeSet<String>>, ctx: &Ctx) -> Vec<u8> {
    let mut out = Vec::with_capacity(input.len());
    let mut i = 0;
    while i < input.len() {
        if input[i] != b'$' || i + 1 >= input.len() {
            out.push(input[i]);
            i += 1;
            continue;
        }
        if input[i + 1] == b'{' {
            let name_start = i + 2;
            if name_start >= input.len() || !is_var_start(input[name_start]) {
                out.push(input[i]);
                i += 1;
                continue;
            }
            let mut pos = name_start + 1;
            while pos < input.len() && is_var_char(input[pos]) {
                pos += 1;
            }
            if pos >= input.len() || input[pos] != b'}' {
                out.push(input[i]);
                i += 1;
                continue;
            }
            let name = std::str::from_utf8(&input[name_start..pos]).unwrap_or("");
            if should_replace(name, allowlist) {
                if let Some(val) = ctx.env.get(name) {
                    out.extend_from_slice(val.to_string_lossy().as_bytes());
                }
            } else {
                out.extend_from_slice(&input[i..=pos]);
            }
            i = pos + 1;
            continue;
        }
        if !is_var_start(input[i + 1]) {
            out.push(input[i]);
            i += 1;
            continue;
        }
        let name_start = i + 1;
        let mut pos = name_start + 1;
        while pos < input.len() && is_var_char(input[pos]) {
            pos += 1;
        }
        let name = std::str::from_utf8(&input[name_start..pos]).unwrap_or("");
        if should_replace(name, allowlist) {
            if let Some(val) = ctx.env.get(name) {
                out.extend_from_slice(val.to_string_lossy().as_bytes());
            }
        } else {
            out.extend_from_slice(&input[i..pos]);
        }
        i = pos;
    }
    out
}

pub fn envsubst_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut variables_only = false;
    let mut positionals: Vec<Vec<u8>> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if no_more_opts {
            positionals.push(s.as_bytes().to_vec());
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            continue;
        }
        if s == "-h" || s == "--help" {
            io.stdout.write_all(ENVSUBST_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "-V" || s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("envsubst", "Bruno Haible")
            )?;
            return Ok(0);
        }
        if s == "-v" || s == "--variables" {
            variables_only = true;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "envsubst", &msg);
            return Ok(1);
        }
        positionals.push(s.as_bytes().to_vec());
    }

    if positionals.len() > 1 {
        writeln!(io.stderr, "envsubst: too many arguments")?;
        writeln!(io.stderr, "Try 'envsubst --help' for more information.")?;
        return Ok(1);
    }

    let allowlist = positionals.first().map(|fmt| extract_variables(fmt));

    if variables_only {
        let Some(vars) = allowlist else {
            writeln!(io.stderr, "envsubst: missing SHELL-FORMAT for --variables")?;
            return Ok(1);
        };
        for name in vars {
            writeln!(io.stdout, "{name}")?;
        }
        return Ok(0);
    }

    let mut input = Vec::new();
    io.stdin.read_to_end(&mut input)?;
    let output = substitute(&input, allowlist.as_ref(), ctx);
    io.stdout.write_all(&output)?;
    Ok(0)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_mem(args: &[&str], env: &[(&str, &str)], stdin_data: &str) -> (u32, String, String) {
        let mut sc = ScratchCtx::new("envsubst-test");
        for (k, v) in env {
            sc.env.set(k, v);
        }
        let ctx = sc.ctx();
        let mut io = MemIo::with_stdin(stdin_data.as_bytes().to_vec());
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = envsubst_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn replaces_dollar_and_braced_vars() {
        let (rc, out, err) = run_mem(
            &[],
            &[("WINUX_ENV_A", "alpha"), ("WINUX_ENV_B", "beta")],
            "$WINUX_ENV_A ${WINUX_ENV_B} $MISSING $1 ${}\n",
        );
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert_eq!(out, "alpha beta  $1 ${}\n");
    }

    #[test]
    fn shell_format_restricts_substitutions() {
        let (rc, out, _) = run_mem(
            &["$WINUX_ENV_A"],
            &[("WINUX_ENV_A", "alpha"), ("WINUX_ENV_B", "beta")],
            "$WINUX_ENV_A ${WINUX_ENV_A} $WINUX_ENV_B ${WINUX_ENV_B}\n",
        );
        assert_eq!(rc, 0);
        assert_eq!(out, "alpha alpha $WINUX_ENV_B ${WINUX_ENV_B}\n");
    }

    #[test]
    fn variables_mode_lists_unique_sorted_names() {
        let (rc, out, _) = run_mem(&["--variables", "${B} $A $B $_C9"], &[], "");
        assert_eq!(rc, 0);
        assert_eq!(out, "A\nB\n_C9\n");
    }

    #[test]
    fn error_on_missing_format_for_variables_or_too_many_args() {
        let (rc, _, err) = run_mem(&["-v"], &[], "");
        assert_eq!(rc, 1);
        assert!(err.contains("missing SHELL-FORMAT"));

        let (rc, _, err) = run_mem(&["$A", "$B"], &[], "");
        assert_eq!(rc, 1);
        assert!(err.contains("too many arguments"));
    }
}
