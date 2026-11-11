//! `unlink` — GNU coreutils semantics (probed against coreutils 9.4).
//!
//! Removes exactly one literal file operand. Failure text is byte-matched
//! to the 1.x C++ body it replaces (#1488 wiring, gate c) while staying in
//! the GNU errno classes (gate a): a directory target is `Is a directory`,
//! glob metachars in the name mean the literal file does not exist
//! (`No such file or directory`), a trailing-separator operand naming a
//! non-directory is `Not a directory`, and remaining Win32 codes map
//! through the 1.x `win32_posix_error_text(invalid_name_as_missing)` table
//! (src/utils/win32.cppm). Zero operands, `--` with nothing after it, or
//! two operands are usage errors rc 1 (`missing operand` / `extra operand
//! 'b'`); unknown options rc 1. A missing file is
//! `cannot unlink 'X': No such file or directory`, rc 1. stdout is always
//! empty, so GNU-side cases gate strictly on rc.

use std::ffi::OsString;
use std::io;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::fsutil::{reason, resolve};
use winux_core::{usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[];

#[cfg(windows)]
const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;

/// Trailing-separator-stripped probe path: the 1.x body stripped the
/// separators before every attribute probe (`operand_target_attributes_w`,
/// #1052) because the raw Win32 layer rejects them on non-directories;
/// Rust's symlink_metadata rejects them everywhere, so the strip happens
/// here instead. The reported operand keeps its original spelling.
#[cfg(windows)]
fn probe_path(resolved: &Path) -> std::path::PathBuf {
    let text = resolved.to_string_lossy();
    Path::new(text.trim_end_matches('/')).to_path_buf()
}

/// Byte-for-byte the 1.x C++ failure text chain
/// (`describe_unlink_failure` + `win32_posix_error_text` with
/// `invalid_name_as_missing`, src/commands/unlink.cpp +
/// src/utils/win32.cppm): the directory bit wins first, then glob
/// metachars mean the literal name does not exist, then the raw Win32
/// code maps through the shared table.
fn describe_unlink_failure(resolved: &Path, name: &str, e: &io::Error) -> String {
    #[cfg(windows)]
    {
        use std::os::windows::fs::MetadataExt;
        // GetFileAttributesW does not follow reparse points;
        // symlink_metadata reads the same attributes.
        if let Ok(md) = std::fs::symlink_metadata(&probe_path(resolved)) {
            if md.file_attributes() & FILE_ATTRIBUTE_DIRECTORY != 0 {
                return "Is a directory".to_string();
            }
        }
        if name.contains(['*', '?', '[']) {
            return "No such file or directory".to_string();
        }
        match e.raw_os_error() {
            // ERROR_FILE_NOT_FOUND / ERROR_PATH_NOT_FOUND /
            // ERROR_INVALID_NAME (invalid_name_as_missing)
            Some(2) | Some(3) | Some(123) => "No such file or directory".to_string(),
            // ERROR_ACCESS_DENIED
            Some(5) => "Permission denied".to_string(),
            // ERROR_DIRECTORY
            Some(267) => "Not a directory".to_string(),
            // ERROR_INVALID_PARAMETER
            Some(87) => "Invalid argument".to_string(),
            _ => reason(e).to_string(),
        }
    }
    #[cfg(not(windows))]
    {
        let _ = (resolved, name);
        reason(e).to_string()
    }
}

pub fn unlink_cmd(args: &[OsString], io: &mut Io, ctx: &Ctx) -> CmdResult {
    let p = match argopt::parse("unlink", args, OPTS, io, ctx, 1) {
        Ok(p) => p,
        Err(s) => return Ok(s),
    };
    if p.has("help") || p.has("version") {
        return Ok(0);
    }
    if p.positionals.is_empty() {
        let _ = usage_error(io, "unlink", "missing operand");
        return Ok(1);
    }
    if p.positionals.len() > 1 {
        let shown = p.positionals[1].to_string_lossy().into_owned();
        let _ = usage_error(io, "unlink", &format!("extra operand '{shown}'"));
        return Ok(1);
    }
    let name = p.positionals[0].to_string_lossy().into_owned();
    // unlink("") fails ENOENT without touching the cwd (GNU probed); the
    // 1.x body reached the same text through DeleteFileW(""). resolve()
    // would anchor the empty operand at ctx.cwd, so special-case it first.
    if name.is_empty() {
        let _ = writeln!(
            io.stderr,
            "unlink: cannot unlink '': No such file or directory"
        );
        return Ok(1);
    }
    let resolved = resolve(ctx, &name);
    // A trailing-separator operand naming a non-directory fails ENOTDIR
    // before any removal (the 1.x had_trailing_separator pre-check; GNU
    // `unlink f//` is ENOTDIR too). On non-Windows the kernel reports the
    // same class natively, so no pre-check is needed.
    #[cfg(windows)]
    if name.ends_with('/') {
        use std::os::windows::fs::MetadataExt;
        if let Ok(md) = std::fs::symlink_metadata(&probe_path(&resolved)) {
            if md.file_attributes() & FILE_ATTRIBUTE_DIRECTORY == 0 {
                let _ = writeln!(io.stderr, "unlink: cannot unlink '{name}': Not a directory");
                return Ok(1);
            }
        }
    }
    match std::fs::remove_file(&resolved) {
        Ok(()) => Ok(0),
        Err(e) => {
            let why = describe_unlink_failure(&resolved, &name, &e);
            let _ = writeln!(io.stderr, "unlink: cannot unlink '{name}': {why}");
            Ok(1)
        }
    }
}

#[cfg(test)]
mod tests {
    use std::ffi::OsString;
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn run(sc: &ScratchCtx, args: &[&str]) -> (u32, String, String) {
        let mut io = MemIo::new("");
        let ctx = sc.ctx();
        let owned: Vec<OsString> = args.iter().map(Into::into).collect();
        let r = super::unlink_cmd(&owned, &mut io.split(), &ctx).unwrap();
        (r, io.out(), io.err())
    }

    #[test]
    fn removes_a_file() {
        let sc = ScratchCtx::new("unlink");
        std::fs::write(sc.path("f"), "data").unwrap();
        let (r, out, _) = run(&sc, &["f"]);
        assert_eq!(r, 0);
        assert!(out.is_empty());
        assert!(!sc.path("f").exists());
    }

    #[test]
    fn missing_operand_forms() {
        let sc = ScratchCtx::new("unlink2");
        let (r, _, err) = run(&sc, &[]);
        assert_eq!(r, 1);
        assert!(err.contains("missing operand"));
        // `--` alone is still zero operands (GNU probed rc 1)
        let (r, _, _) = run(&sc, &["--"]);
        assert_eq!(r, 1);
        // two operands -> extra operand
        std::fs::write(sc.path("g"), "x").unwrap();
        let (r, _, err) = run(&sc, &["g", "second"]);
        assert_eq!(r, 1);
        assert!(err.contains("extra operand 'second'"));
    }

    #[test]
    fn missing_file_and_directory() {
        let sc = ScratchCtx::new("unlink3");
        let (r, _, err) = run(&sc, &["missing"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot unlink 'missing'"));
        assert!(err.contains("No such file or directory"));
        std::fs::create_dir(sc.path("d")).unwrap();
        let (r, _, err) = run(&sc, &["d"]);
        assert_eq!(r, 1);
        // 1.x/GNU byte-equal: a directory target is refused as EISDIR
        assert!(err.contains("cannot unlink 'd': Is a directory"));
    }

    #[test]
    fn windows_failure_classes() {
        let sc = ScratchCtx::new("unlink5");
        // empty operand: ENOENT without touching the cwd (GNU probed)
        let (r, _, err) = run(&sc, &[""]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot unlink '': No such file or directory"));
        // trailing separators on a regular file: ENOTDIR (GNU probed)
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, err) = run(&sc, &["f//"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot unlink 'f//': Not a directory"));
        // trailing separators on a missing path: still ENOENT
        let (r, _, err) = run(&sc, &["missing//"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot unlink 'missing//': No such file or directory"));
        // glob metachars name a literal file that does not exist: ENOENT
        let (r, _, err) = run(&sc, &["no*match"]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot unlink 'no*match': No such file or directory"));
        // dot forms are directories (GNU probed: EISDIR for '.' and '..')
        let (r, _, err) = run(&sc, &["."]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot unlink '.': Is a directory"));
        let (r, _, err) = run(&sc, &[".."]);
        assert_eq!(r, 1);
        assert!(err.contains("cannot unlink '..': Is a directory"));
    }

    #[test]
    fn dashdash_and_options() {
        let sc = ScratchCtx::new("unlink4");
        std::fs::write(sc.path("f"), "x").unwrap();
        let (r, _, _) = run(&sc, &["--", "f"]);
        assert_eq!(r, 0);
        let (r, _, _) = run(&sc, &["--bogus"]);
        assert_eq!(r, 1);
        let (r, _, _) = run(&sc, &["--help"]);
        assert_eq!(r, 0);
    }
}
