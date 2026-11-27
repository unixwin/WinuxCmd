//! `yes` — GNU coreutils semantics, ported for WinuxCmd 2.0 (#1488).
//!
//! Repeatedly writes `<STRING(s)>` (space-joined, or `"y"` if none) followed
//! by `\n` to `io.stdout`. Honors `WINUXCMD_YES_REPEAT_LIMIT` from `ctx.env`
//! for bounded tests, and maps broken-pipe write errors to exit status 141
//! (`128 + SIGPIPE`, #1142).

use std::ffi::OsString;
use std::io::{self, Write};

use crate::argopt;
use crate::fsutil;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const SIGPIPE_DEATH_STATUS: u32 = 128 + 13;

const YES_HELP_BODY: &str = r#"Usage: yes [STRING]...
  or:  yes OPTION
Repeatedly output a line with all specified STRING(s), or 'y'.

      --help        display this help and exit
      --version     output version information and exit

"#;

fn repeated_block(line: &str) -> Vec<u8> {
    let mut unit = Vec::with_capacity(line.len() + 1);
    unit.extend_from_slice(line.as_bytes());
    unit.push(b'\n');

    const TARGET_SIZE: usize = 8192;
    let mut block = unit.clone();
    while block.len() * 2 <= TARGET_SIZE {
        let cur_len = block.len();
        block.extend_from_within(..cur_len);
    }
    while block.len() + unit.len() <= TARGET_SIZE {
        block.extend_from_slice(&unit);
    }
    block
}

fn is_broken_pipe(e: &io::Error) -> bool {
    e.kind() == io::ErrorKind::BrokenPipe || matches!(e.raw_os_error(), Some(32) | Some(109) | Some(232))
}

fn handle_write_error(e: io::Error, io: &mut Io) -> u32 {
    if is_broken_pipe(&e) {
        SIGPIPE_DEATH_STATUS
    } else {
        let _ = writeln!(io.stderr, "yes: standard output: {}", fsutil::reason(&e));
        1
    }
}

pub fn yes_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;

    for a in args {
        let s = a.to_string_lossy();
        if no_more_opts || !positionals.is_empty() {
            positionals.push(s.into_owned());
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            continue;
        }
        if s == "--help" {
            write!(
                io.stdout,
                "{}{}",
                YES_HELP_BODY,
                gnuhelp::footer("yes", "yes invocation")
            )?;
            return Ok(0);
        }
        if s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("yes", "David MacKenzie")
            )?;
            return Ok(0);
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "yes", &msg);
            return Ok(1);
        }
        positionals.push(s.into_owned());
    }

    let output = if positionals.is_empty() {
        "y".to_owned()
    } else {
        positionals.join(" ")
    };

    if let Some(raw_limit) = ctx.env.get("WINUXCMD_YES_REPEAT_LIMIT") {
        let text = raw_limit.to_string_lossy();
        if !text.is_empty() {
            if let Ok(limit) = text.parse::<usize>() {
                let mut line = Vec::with_capacity(output.len() + 1);
                line.extend_from_slice(output.as_bytes());
                line.push(b'\n');
                for _ in 0..limit {
                    if let Err(e) = io.stdout.write_all(&line) {
                        return Ok(handle_write_error(e, io));
                    }
                }
                if let Err(e) = io.stdout.flush() {
                    return Ok(handle_write_error(e, io));
                }
                return Ok(0);
            }
        }
    }

    let block = repeated_block(&output);
    loop {
        if let Err(e) = io.stdout.write_all(&block) {
            return Ok(handle_write_error(e, io));
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run_yes(args: &[&str], limit: Option<&str>) -> (u32, String, String) {
        let mut sc = ScratchCtx::new("yes-test");
        if let Some(lim) = limit {
            sc.env.set("WINUXCMD_YES_REPEAT_LIMIT", lim);
        }
        let ctx = sc.ctx();
        let mut io = MemIo::new("");
        let os_args: Vec<OsString> = args.iter().map(OsString::from).collect();
        let rc = yes_cmd(&os_args, &mut io.split(), &ctx).unwrap();
        (rc, io.out(), io.err())
    }

    #[test]
    fn default_and_custom_strings_with_limit() {
        let (rc, out, err) = run_yes(&[], Some("4"));
        assert_eq!(rc, 0);
        assert!(err.is_empty());
        assert_eq!(out, "y\ny\ny\ny\n");

        let (rc, out, _) = run_yes(&["a", "bar", "c"], Some("2"));
        assert_eq!(rc, 0);
        assert_eq!(out, "a bar c\na bar c\n");

        let (rc, out, _) = run_yes(&["--", "--version"], Some("2"));
        assert_eq!(rc, 0);
        assert_eq!(out, "--version\n--version\n");
    }

    #[test]
    fn help_version_and_invalid_option() {
        let (rc, out, _) = run_yes(&["--help"], None);
        assert_eq!(rc, 0);
        assert!(out.starts_with("Usage: yes [STRING]...\n"));

        let (rc, out, _) = run_yes(&["--version"], None);
        assert_eq!(rc, 0);
        assert!(out.starts_with("yes "));

        let (rc, _, err) = run_yes(&["--bogus"], None);
        assert_eq!(rc, 1);
        assert!(err.contains("unrecognized option '--bogus'"));
    }
}
