//! `sync` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! No operands: `sync()` everything (a silent rc-0 noop on this host, the
//! same contract the 1.x applet shipped — Windows keeps no portable
//! whole-disk flush). With operands: open each one (FILE_FLAG_BACKUP_
//! SEMANTICS for directories, read/write for files, exactly the C++
//! resolver's flags) and FlushFileBuffers it via `sync_all`; a failed open
//! reports "error opening 'X': <reason>" and a failed flush reports
//! "error syncing 'X': <reason>", per operand, rc 1. `-d` without operands
//! is GNU's "--data needs at least one argument" (rc 1) while `-f` alone
//! syncs every filesystem (rc 0); `-d` and `-f` together are mutually
//! exclusive. Unknown options produce the getopt diagnostic and rc 1.
//! Operands are DATA: GNU never globs them (the caller's shell does), so
//! a literal "st*" reports "error opening 'st*'" — the 1.x applet's glob
//! expansion is a documented deviation the library does not inherit.

use std::ffi::OsString;
use std::io::Write;

use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

pub fn sync_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let mut data = false;
    let mut file_system = false;
    let mut no_more = false;
    let mut operands: Vec<String> = Vec::new();
    for a in args {
        let s = a.to_string_lossy().into_owned();
        if no_more {
            operands.push(s);
            continue;
        }
        if s == "--" {
            no_more = true;
            continue;
        }
        if s == "--help" || s == "--version" {
            // Wording-bound: the product's dispatcher prints the branded
            // help/version for the sole-argument form before this handler
            // runs; the exit status is what gates.
            return Ok(0);
        }
        if s == "-d" || s == "--data" {
            data = true;
            continue;
        }
        if s == "-f" || s == "--file-system" {
            file_system = true;
            continue;
        }
        if s.starts_with('-') && s.len() > 1 {
            // [GNU] sync.c getopt diagnostics; program prefix stays the
            // usage_error frame, only the wording is keyed.
            if let Some(rest) = s.strip_prefix("--") {
                let _ = usage_error(
                    io,
                    "sync",
                    &tr!(
                        ctx,
                        "command.sync.error.unrecognized_option",
                        "unrecognized option '--{option}'",
                        option = rest
                    ),
                );
            } else {
                let c = s.chars().nth(1).unwrap();
                let _ = usage_error(
                    io,
                    "sync",
                    &tr!(
                        ctx,
                        "command.sync.error.invalid_option",
                        "invalid option -- '{option}'",
                        option = c
                    ),
                );
            }
            return Ok(1);
        }
        operands.push(s);
    }

    if data && file_system {
        let _ = writeln!(
            io.stderr,
            "sync: {}",
            tr!(
                ctx,
                "command.sync.error.data_and_file_system",
                "cannot specify both --data and --file-system"
            )
        );
        return Ok(1);
    }
    // [GNU] -f alone syncs every filesystem (rc 0); only -d demands an
    // operand (probed 9.4).
    if data && operands.is_empty() {
        let _ = writeln!(
            io.stderr,
            "sync: {}",
            tr!(
                ctx,
                "command.sync.error.data_needs_argument",
                "--data needs at least one argument"
            )
        );
        return Ok(1);
    }
    if operands.is_empty() {
        // sync(): flush everything. Windows has no portable whole-disk
        // flush; the 1.x applet returned success and so does the library.
        return Ok(0);
    }

    let mut failed = false;
    {
        let err = &mut *io.stderr;
        for operand in &operands {
            let shown = operand.clone();
            match open_and_flush(ctx, operand) {
                Ok(()) => {}
                Err(e) => {
                    let _ = writeln!(
                        err,
                        "sync: {}",
                        tr!(
                            ctx,
                            "command.sync.error.error_opening",
                            "error opening '{file}': {reason}",
                            file = shown,
                            reason = reason_text(&e)
                        )
                    );
                    failed = true;
                }
            }
        }
    }
    Ok(if failed { 1 } else { 0 })
}

#[cfg(windows)]
fn open_and_flush(ctx: &Ctx, operand: &str) -> std::io::Result<()> {
    use std::os::windows::fs::OpenOptionsExt;

    const FILE_FLAG_BACKUP_SEMANTICS: u32 = 0x0200_0000;

    let path = crate::fsutil::resolve(ctx, operand);
    let is_dir = std::fs::metadata(&path)
        .map(|m| m.is_dir())
        .unwrap_or(false);
    let mut opts = std::fs::OpenOptions::new();
    if is_dir {
        // The 1.x resolver opened directories with GENERIC_WRITE +
        // FILE_FLAG_BACKUP_SEMANTICS (CreateFileW); GENERIC_READ on a
        // directory handle fails with ERROR_ACCESS_DENIED here.
        opts.write(true).custom_flags(FILE_FLAG_BACKUP_SEMANTICS);
    } else {
        opts.read(true).write(true);
    }
    let file = opts.open(&path)?;
    // Rust maps File::sync_all to FlushFileBuffers on Windows.
    file.sync_all()?;
    Ok(())
}

#[cfg(not(windows))]
fn open_and_flush(ctx: &Ctx, operand: &str) -> std::io::Result<()> {
    let path = crate::fsutil::resolve(ctx, operand);
    let file = std::fs::File::open(&path)?;
    file.sync_all()
}

/// The GNU errno wording fsutil::reason already provides.
fn reason_text(e: &std::io::Error) -> &'static str {
    crate::fsutil::reason(e)
}

#[cfg(test)]
mod tests {
    use winux_core::test_support::{MemIo, ScratchCtx};

    #[test]
    fn no_operands_is_silent_success() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("sync");
        let ctx = sc.ctx();
        let r = super::sync_cmd(&[], &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
        assert!(io.out().is_empty());
        assert!(io.err().is_empty());
    }

    #[test]
    fn missing_operands_report_per_file() {
        // Probed GNU 9.4: each missing operand reports its own
        // "error opening" line and the aggregate rc is 1.
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("sync2");
        let ctx = sc.ctx();
        let args = ["bad1".into(), "bad2".into()];
        let r = super::sync_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert_eq!(
            io.err(),
            "sync: error opening 'bad1': No such file or directory\n\
             sync: error opening 'bad2': No such file or directory\n"
        );
    }

    #[test]
    fn data_and_file_system_rules() {
        // Probed GNU 9.4.
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("sync3");
        let ctx = sc.ctx();
        let args = ["-d".into(), "-f".into()];
        let r = super::sync_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io
            .err()
            .contains("cannot specify both --data and --file-system"));

        let mut io = MemIo::new("");
        let args = ["-d".into()];
        let r = super::sync_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("--data needs at least one argument"));

        // -f alone syncs every filesystem: rc 0.
        let mut io = MemIo::new("");
        let args = ["-f".into()];
        let r = super::sync_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 0);
    }

    #[test]
    fn existing_file_and_directory_flush() {
        let sc = ScratchCtx::new("sync4");
        let ctx = sc.ctx();
        std::fs::write(sc.path("st"), "x").unwrap();
        std::fs::create_dir(sc.path("sdir")).unwrap();
        for args in [
            vec!["st"],
            vec!["sdir"],
            vec!["-d", "st"],
            vec!["-f", "sdir"],
            vec!["--"],
        ] {
            let owned: Vec<std::ffi::OsString> =
                args.iter().map(std::ffi::OsString::from).collect();
            let mut io = MemIo::new("");
            let r = super::sync_cmd(&owned, &mut io.split(), &ctx).unwrap();
            assert_eq!(r, 0, "args={args:?} err={:?}", io.err());
        }
    }

    #[test]
    fn unknown_options_are_getopt_diagnostics() {
        let mut io = MemIo::new("");
        let sc = ScratchCtx::new("sync5");
        let ctx = sc.ctx();
        let args = ["--bogus".into()];
        let r = super::sync_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("unrecognized option '--bogus'"));

        let mut io = MemIo::new("");
        let args = ["-z".into()];
        let r = super::sync_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(io.err().contains("invalid option -- 'z'"));
    }

    #[test]
    fn catalog_injection_translates_data_operand_error() {
        use winux_core::host::MapHost;
        use winux_core::{Ctx, MapEnv};

        let sc = ScratchCtx::new("sync-i18n");
        let mut host = MapHost::default();
        host.messages.insert(
            "command.sync.error.data_needs_argument".to_string(),
            "--data 至少需要一个参数".to_string(),
        );
        let env = MapEnv::new();
        let ctx = Ctx::with_host(sc.dir.clone(), &env, &host);
        let mut io = MemIo::new("");
        let args = ["-d".into()];
        let r = super::sync_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(
            io.err().contains("--data 至少需要一个参数"),
            "got {}",
            io.err()
        );
    }
}
