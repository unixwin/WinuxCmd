//! `chroot` — GNU coreutils chroot surface on Windows, ported for WinuxCmd 2.0 (#1488).
//!
//! Validates options (`--groups`, `--userspec`, `--skip-chdir`) and `NEWROOT`
//! directory existence via `fsutil::resolve`, exiting 125 (`EXIT_CANCELED`,
//! #1375) on usage errors, missing directories, or the Windows chroot limitation.

use std::ffi::OsString;
use std::io::Write;

use crate::argopt;
use crate::fsutil;
use crate::gnuhelp;
use winux_core::{usage_error, CmdResult, Ctx, Io};

const CHROOT_HELP: &str = "\
Usage: chroot [OPTION] NEWROOT [COMMAND [ARG]...]
  or:  chroot OPTION
Run COMMAND with root directory set to NEWROOT.

      --groups=G_LIST        specify supplementary groups as g1,g2,..,gN
      --userspec=USER:GROUP  specify user and group (ID or name) to use
      --skip-chdir           do not change working directory to '/'
      --help                 display this help and exit
      --version              output version information and exit
";

pub fn chroot_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut positionals: Vec<String> = Vec::new();
    let mut no_more_opts = false;
    let mut i = 0usize;

    while i < args.len() {
        let s = args[i].to_string_lossy();
        if no_more_opts || !positionals.is_empty() {
            positionals.push(s.into_owned());
            i += 1;
            continue;
        }
        if s == "--" {
            no_more_opts = true;
            i += 1;
            continue;
        }
        if s == "--help" {
            io.stdout.write_all(CHROOT_HELP.as_bytes())?;
            return Ok(0);
        }
        if s == "--version" {
            write!(
                io.stdout,
                "{}",
                gnuhelp::coreutils_version("chroot", "Roland McGrath")
            )?;
            return Ok(0);
        }
        if s == "--skip-chdir" {
            i += 1;
            continue;
        }
        if s == "--groups" || s == "-g" || s == "--userspec" || s == "-u" {
            i += 1;
            if i >= args.len() {
                let _ = usage_error(
                    ctx,
                    io,
                    "chroot",
                    &argopt::option_requires_argument(ctx, &s),
                );
                return Ok(125);
            }
            i += 1;
            continue;
        }
        if s.starts_with("--groups=") || s.starts_with("--userspec=") {
            i += 1;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            let msg = if let Some(rest) = s.strip_prefix("--") {
                argopt::unrecognized_option(ctx, &format!("--{rest}"))
            } else {
                let c = s.chars().nth(1).unwrap();
                argopt::invalid_option(ctx, &c.to_string())
            };
            let _ = usage_error(ctx, io, "chroot", &msg);
            return Ok(125);
        }
        positionals.push(s.into_owned());
        i += 1;
    }

    let Some(newroot) = positionals.first() else {
        writeln!(io.stderr, "chroot: missing operand")?;
        writeln!(io.stderr, "Try 'chroot --help' for more information.")?;
        return Ok(125);
    };

    let resolved = fsutil::resolve(ctx, newroot);
    let is_dir = std::fs::metadata(&resolved)
        .map(|m| m.is_dir())
        .unwrap_or(false);
    if !is_dir {
        writeln!(
            io.stderr,
            "chroot: cannot change root directory to '{newroot}': no such directory"
        )?;
        return Ok(125);
    }

    writeln!(
        io.stderr,
        "chroot: changing the root directory is not supported on Windows"
    )?;
    Ok(125)
}

#[cfg(test)]
mod tests {
    use super::*;
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn missing_operand_and_option_error_exit_125() {
        let sc = ScratchCtx::new("chroot-test");
        let ctx = sc.ctx();

        let mut io = MemIo::new("");
        let rc = chroot_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(rc, 125);
        assert!(io.err().contains("missing operand"));

        let mut io_opt = MemIo::new("");
        let rc_opt = chroot_cmd(
            &[OsString::from("--zzz"), OsString::from("someroot")],
            &mut io_opt.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc_opt, 125);
    }

    #[test]
    fn directory_validation_and_windows_limitation() {
        let sc = ScratchCtx::new("chroot-dir");
        std::fs::write(sc.path("file.txt"), b"x").unwrap();
        std::fs::create_dir_all(sc.path("jail")).unwrap();
        let ctx = sc.ctx();

        let mut io_miss = MemIo::new("");
        let rc_miss = chroot_cmd(&[OsString::from("no_such")], &mut io_miss.split(), &ctx).unwrap();
        assert_eq!(rc_miss, 125);
        assert!(io_miss.err().contains("no such directory"));

        let mut io_file = MemIo::new("");
        let rc_file = chroot_cmd(&[OsString::from("file.txt")], &mut io_file.split(), &ctx).unwrap();
        assert_eq!(rc_file, 125);
        assert!(io_file.err().contains("no such directory"));

        let mut io_jail = MemIo::new("");
        let rc_jail = chroot_cmd(
            &[
                OsString::from("--userspec=u:g"),
                OsString::from("--groups=users"),
                OsString::from("--skip-chdir"),
                OsString::from("jail"),
                OsString::from("cmd.exe"),
            ],
            &mut io_jail.split(),
            &ctx,
        )
        .unwrap();
        assert_eq!(rc_jail, 125);
        assert!(io_jail.err().contains("not supported on Windows"));
    }
}
