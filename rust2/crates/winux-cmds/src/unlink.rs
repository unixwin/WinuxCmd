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
//! empty, so GNU-side cases gate strictly on rc. Removal goes through
//! `remove_unlink_target`: a directory symlink unlinks the link itself
//! (RemoveDirectoryW on the reparse point, like unlink(2)), and a
//! read-only file is pre-cleared because DeleteFileW refuses what
//! unlink(2) accepts (both mirroring the 1.x `remove_unlink_target`,
//! src/commands/unlink.cpp:48).

use std::ffi::OsString;
use std::io;
use std::path::Path;

use crate::argopt::{self, OptSpec};
use crate::fsutil::{reason, resolve};
use winux_core::{tr, usage_error, CmdResult, Ctx, Io};

const OPTS: &[OptSpec] = &[];

#[cfg(windows)]
const FILE_ATTRIBUTE_DIRECTORY: u32 = 0x10;

/// Windows removal path, mirroring the 1.x C++ `remove_unlink_target`
/// (src/commands/unlink.cpp:48) so the wired body keeps both the POSIX
/// unlink(2) rule and the old exe's behavior:
/// - a directory symlink (reparse point with the directory bit) is
///   unlinked with RemoveDirectoryW, which deletes the link entry
///   without following it — unlink(2) on a symlink removes the link
///   itself, never the target; DeleteFileW refuses a directory reparse
///   point, so the Rust remove_file default regressed the
///   `unlink dir-link` case (CI unit test
///   unlink_directory_symlink_removes_link_only_if_available);
/// - a read-only regular file has the attribute cleared first because
///   DeleteFileW refuses it while unlink(2) never consults the file's
///   own write permission, only the containing directory's (the 1.x
///   body cited coreutils unlink.c:82-83 and made the same rm-mirroring
///   pre-clear);
/// - everything else is DeleteFileW (remove_file).
#[cfg(windows)]
fn remove_unlink_target(resolved: &Path) -> io::Result<()> {
    use std::os::windows::fs::MetadataExt;

    const FILE_ATTRIBUTE_READONLY: u32 = 0x1;
    const FILE_ATTRIBUTE_REPARSE_POINT: u32 = 0x400;

    if let Ok(md) = std::fs::symlink_metadata(&probe_path(resolved)) {
        let attrs = md.file_attributes();
        if attrs & FILE_ATTRIBUTE_REPARSE_POINT != 0 && attrs & FILE_ATTRIBUTE_DIRECTORY != 0 {
            return std::fs::remove_dir(resolved);
        }
        if attrs & FILE_ATTRIBUTE_READONLY != 0 && attrs & FILE_ATTRIBUTE_DIRECTORY == 0 {
            if let Ok(mut perms) = std::fs::metadata(resolved).map(|m| m.permissions()) {
                perms.set_readonly(false);
                let _ = std::fs::set_permissions(resolved, perms);
            }
        }
    }
    std::fs::remove_file(resolved)
}

/// Non-Windows remove_file *is* unlink(2): a symlink operand unlinks the
/// link itself and a read-only file unlinks when the directory permits.
#[cfg(not(windows))]
fn remove_unlink_target(resolved: &Path) -> io::Result<()> {
    std::fs::remove_file(resolved)
}

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
        let _ = usage_error(
            io,
            "unlink",
            &tr!(
                ctx,
                "command.unlink.error.missing_operand",
                "missing operand"
            ),
        );
        return Ok(1);
    }
    if p.positionals.len() > 1 {
        let shown = p.positionals[1].to_string_lossy().into_owned();
        let _ = usage_error(
            io,
            "unlink",
            &tr!(
                ctx,
                "command.unlink.error.extra_operand",
                "extra operand '{operand}'",
                operand = shown
            ),
        );
        return Ok(1);
    }
    let name = p.positionals[0].to_string_lossy().into_owned();
    // unlink("") fails ENOENT without touching the cwd (GNU probed); the
    // 1.x body reached the same text through DeleteFileW(""). resolve()
    // would anchor the empty operand at ctx.cwd, so special-case it first.
    if name.is_empty() {
        let _ = writeln!(
            io.stderr,
            "unlink: {}",
            tr!(
                ctx,
                "command.unlink.error.cannot_unlink",
                "cannot unlink '{file}': {reason}",
                file = "",
                reason = "No such file or directory"
            )
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
                let _ = writeln!(
                    io.stderr,
                    "unlink: {}",
                    tr!(
                        ctx,
                        "command.unlink.error.cannot_unlink",
                        "cannot unlink '{file}': {reason}",
                        file = name,
                        reason = "Not a directory"
                    )
                );
                return Ok(1);
            }
        }
    }
    match remove_unlink_target(&resolved) {
        Ok(()) => Ok(0),
        Err(e) => {
            let why = describe_unlink_failure(&resolved, &name, &e);
            let _ = writeln!(
                io.stderr,
                "unlink: {}",
                tr!(
                    ctx,
                    "command.unlink.error.cannot_unlink",
                    "cannot unlink '{file}': {reason}",
                    file = name,
                    reason = why
                )
            );
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
    fn directory_symlink_unlinks_the_link_itself() {
        // CI regression gate (unlink_directory_symlink_removes_link_only_
        // if_available): unlink(2) removes the symlink entry, never the
        // target. The 1.x body used RemoveDirectoryW on the reparse
        // point; remove_unlink_target keeps that. Skipped when the
        // process lacks symlink-creation privilege.
        let sc = ScratchCtx::new("unlink6");
        std::fs::create_dir_all(sc.path("target-dir")).unwrap();
        std::fs::write(sc.path("target-dir").join("payload.txt"), "p").unwrap();
        #[cfg(windows)]
        let created =
            std::os::windows::fs::symlink_dir(sc.path("target-dir"), sc.path("dir-link")).is_ok();
        #[cfg(not(windows))]
        let created =
            std::os::unix::fs::symlink(sc.path("target-dir"), sc.path("dir-link")).is_ok();
        if !created {
            println!("skipped: symlink creation unavailable");
            return;
        }
        let (r, _, err) = run(&sc, &["dir-link"]);
        assert_eq!(r, 0, "stderr: {err}");
        assert!(err.is_empty());
        assert!(!sc.path("dir-link").exists(), "link must be gone");
        assert!(sc.path("target-dir").exists());
        assert!(sc.path("target-dir").join("payload.txt").exists());
    }

    #[test]
    fn read_only_file_unlinks_like_gnu() {
        // [GNU] unlink(2) never consults the file's own write permission
        // (unlink.c:82-83); the 1.x body pre-cleared FILE_ATTRIBUTE_
        // READONLY so DeleteFileW would accept it. The wired body keeps
        // the pre-clear.
        let sc = ScratchCtx::new("unlink7");
        let f = sc.path("ro.txt");
        std::fs::write(&f, "x").unwrap();
        let mut perms = std::fs::metadata(&f).unwrap().permissions();
        perms.set_readonly(true);
        std::fs::set_permissions(&f, perms).unwrap();
        let (r, _, err) = run(&sc, &["ro.txt"]);
        assert_eq!(r, 0, "stderr: {err}");
        assert!(!f.exists());
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

    #[test]
    fn catalog_injection_translates_cannot_unlink() {
        use winux_core::host::MapHost;
        use winux_core::{Ctx, MapEnv};

        let sc = ScratchCtx::new("unlink-i18n");
        let mut host = MapHost::default();
        host.messages.insert(
            "command.unlink.error.cannot_unlink".to_string(),
            "无法取消链接 '{file}'：{reason}".to_string(),
        );
        let env = MapEnv::new();
        let ctx = Ctx::with_host(sc.dir.clone(), &env, &host);
        let mut io = MemIo::new("");
        let args: Vec<OsString> = vec!["nosuch".into()];
        let r = super::unlink_cmd(&args, &mut io.split(), &ctx).unwrap();
        assert_eq!(r, 1);
        assert!(
            io.err().contains("无法取消链接 'nosuch'："),
            "got {}",
            io.err()
        );
    }
}
