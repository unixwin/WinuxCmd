//! `chmod` — GNU coreutils semantics, Windows-permission subset.
//!
//! Argument grammar matches GNU (probed against coreutils 9.4): MODE then
//! files; missing files are reported per file (`cannot access 'X': ...`)
//! and processing continues with aggregate status 1; an invalid mode aborts
//! before any file is touched (`invalid mode: 'X'`, status 1); no operand
//! after MODE is `missing operand after 'MODE'`.
//!
//! Windows pilot subset (documented deviation): NTFS keeps no POSIX bits,
//! so only the owner-write bit is persisted as the read-only attribute;
//! symbolic modes are evaluated against a reconstructed mode
//! (`current_mode`). `-v`/`-c` messages are wording-bound (recorded).
//! `--reference=` reads the reference file's reconstructed mode.

use std::ffi::OsString;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::fsutil::resolve;
use crate::modemap::{apply_mode, apply_spec, current_mode};
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

pub fn chmod_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("chmod", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    let recursive = p.has("recursive");
    let verbose = p.has("verbose") || p.has("changes");
    let reference = p.value_of("reference").map(|s| s.to_string());
    // GNU grammar: with --reference=RFILE there is NO mode operand; every
    // positional is a file.
    let reference_bits = match &reference {
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

    // mode operand handling: absent entirely with --reference
    let (files, mode): (Vec<String>, String) = if reference.is_some() {
        if p.positionals.is_empty() {
            let _ = usage_error(io, "chmod", "missing operand");
            return Ok(1);
        }
        (
            p.positionals
                .iter()
                .map(|a| a.to_string_lossy().into_owned())
                .collect(),
            String::new(),
        )
    } else {
        if p.positionals.is_empty() {
            let _ = usage_error(io, "chmod", "missing operand");
            return Ok(1);
        }
        let mode = p.positionals[0].to_string_lossy().into_owned();
        if p.positionals.len() == 1 {
            let msg = format!("missing operand after {}", winux_core::q(&mode));
            let _ = usage_error(io, "chmod", &msg);
            return Ok(1);
        }
        // validate BEFORE touching files (GNU aborts up front)
        if let Err(bad) = crate::modemap::validate_mode(&mode) {
            let msg = format!("invalid mode {}", winux_core::q(&bad));
            let _ = usage_error(io, "chmod", &msg);
            return Ok(1);
        }
        (
            p.positionals[1..]
                .iter()
                .map(|a| a.to_string_lossy().into_owned())
                .collect(),
            mode,
        )
    };

    let mut failed = false;
    for name in &files {
        let path = resolve(ctx, Path::new(name));
        if !path.exists() {
            let _ = writeln!(
                io.stderr,
                "chmod: cannot access {}: {}",
                winux_core::q(name),
                "No such file or directory"
            );
            failed = true;
            continue;
        }
        if recursive && path.is_dir() {
            if walk_chmod(&path, reference_bits, &mode) == Err(()) {
                failed = true;
            }
        } else if apply_to(&path, reference_bits, &mode).is_err() {
            let _ = writeln!(
                io.stderr,
                "chmod: changing permissions of {}: {}",
                winux_core::q(&name),
                "Permission denied"
            );
            failed = true;
        } else if verbose {
            let _ = writeln!(*io.stdout, "mode of {} changed", winux_core::q(&name));
        }
    }
    Ok(if failed { 1 } else { 0 })
}

fn apply_to(path: &Path, reference_bits: Option<u32>, mode: &str) -> std::io::Result<()> {
    match reference_bits {
        Some(bits) => apply_mode(path, bits),
        None => apply_spec(path, mode),
    }
}

fn walk_chmod(dir: &Path, reference_bits: Option<u32>, mode: &str) -> Result<(), ()> {
    if apply_to(dir, reference_bits, mode).is_err() {
        return Err(());
    }
    let rd = match std::fs::read_dir(dir) {
        Ok(r) => r,
        Err(_) => return Err(()),
    };
    for entry in rd.flatten() {
        let p = entry.path();
        let is_dir = entry.file_type().map(|t| t.is_dir()).unwrap_or(false);
        if is_dir {
            if walk_chmod(&p, reference_bits, mode).is_err() {
                return Err(());
            }
        } else if apply_to(&p, reference_bits, mode).is_err() {
            return Err(());
        }
    }
    Ok(())
}
