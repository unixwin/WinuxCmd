//! Shared canonicalization for `readlink -f/-e/-m` and `realpath`.
//!
//! GNU rules (probed against coreutils 9.4):
//! * `-f` (default for realpath): all components *except the last* must
//!   exist; the last is resolved if it exists.
//! * `-e`: every component must exist.
//! * `-m`: nothing must exist; the result is lexical.
//!
//! Windows pilot output form: canonical `std::fs::canonicalize` with the
//! `\\?\` verbatim prefix stripped (native `D:\...` form). The gate compares
//! path FORM against the C++ exe only; GNU is compared on rc/stderr since
//! POSIX and Windows path forms cannot be byte-equal.

use std::path::{Path, PathBuf};

use winux_core::{strip_verbatim, Ctx};

#[derive(Clone, Copy, PartialEq, Debug)]
pub enum CanonMode {
    /// readlink -f / realpath default: all but last must exist
    AllButLast,
    /// readlink -e / realpath -e: everything must exist
    AllMustExist,
    /// readlink -m / realpath -m: no existence requirement
    NoCheck,
}

fn is_sep(b: u8) -> bool {
    b == b'/' || b == b'\\'
}

fn is_drive_letter(b: u8) -> bool {
    b.is_ascii_alphabetic()
}

/// MSYS single-letter drive form "/X" or "/X/..." (native_path.cppm
/// is_drive_form): no shell mount name collides, they are all multi-letter.
fn is_drive_form(b: &[u8]) -> bool {
    b.len() >= 2 && is_sep(b[0]) && is_drive_letter(b[1]) && (b.len() == 2 || is_sep(b[2]))
}

/// Strip trailing separators, keeping "X:\" roots (native_path.cppm
/// strip_trailing_separators; the UNC guard only matters for operands the
/// folds below never apply to).
fn strip_trailing_seps(s: &str) -> &str {
    let b = s.as_bytes();
    let mut end = b.len();
    while end > 1 && is_sep(b[end - 1]) {
        if end == 3 && b[1] == b':' {
            break;
        }
        end -= 1;
    }
    &s[..end]
}

/// Fold the Cygwin "/cygdrive/X" (#1145) and WSL interop "/mnt/X" (#1172)
/// prefixes onto the MSYS single-letter spelling they name, mirroring
/// native_path.cppm fold_posix_drive_prefix_w: cygdrive is case-insensitive,
/// mnt is the case-sensitive literal WSL itself emits, bare /mnt and
/// non-drive children ("/mnt/data", "/mntcc") never fold. Returns the
/// operand unchanged when neither prefix applies.
fn fold_posix_drive_prefix(s: &str) -> String {
    let b = s.as_bytes();
    // /cygdrive/X: name at [1..9], separator at [9], drive at [10].
    if b.len() >= 11 && is_sep(b[0]) {
        let name = &b[1..9];
        let cygdrive = b"cygdrive";
        let name_match = name
            .iter()
            .zip(cygdrive)
            .all(|(ch, want)| ch.to_ascii_lowercase() == *want);
        let drive = b[10];
        if name_match
            && is_sep(b[9])
            && is_drive_letter(drive)
            && (b.len() == 11 || is_sep(b[11]))
        {
            let mut out = String::from("/");
            out.push_str(&s[10..]);
            return out;
        }
    }
    // /mnt/X: the literal is case-sensitive (rubash#444, rubash#592).
    if b.len() >= 6 && is_sep(b[0]) && &b[1..4] == b"mnt" {
        let drive = b[5];
        if is_sep(b[4]) && is_drive_letter(drive) && (b.len() == 6 || is_sep(b[6])) {
            let mut out = String::from("/");
            out.push_str(&s[5..]);
            return out;
        }
    }
    s.to_string()
}

/// Fold the MSYS drive dialect "/e/Data" onto its native spelling
/// "E:\Data" (native_path.cppm fold_drive_dialect_w, #1231): the first
/// segment must be exactly one drive letter.
fn fold_drive_dialect(s: &str) -> Option<String> {
    let b = s.as_bytes();
    if !is_drive_form(b) {
        return None;
    }
    let mut drive = b[1];
    if drive.is_ascii_lowercase() {
        drive = drive.to_ascii_uppercase();
    }
    let mut folded = String::new();
    folded.push(drive as char);
    folded.push_str(":\\");
    if b.len() > 3 {
        folded.push_str(&s[3..]);
    }
    Some(folded.replace('/', "\\"))
}

/// Fold literal colons in the operand's final component onto the
/// private-use carrier U+F03A (native_path.cppm fold_carrier_colons_w,
/// #1283): Win32 would read them as an alternate-data-stream separator,
/// while the shell's path layer stores such names on disk encoded. The bare
/// drive ("D:") keeps its drive meaning; verbatim/device spellings pass
/// through.
fn fold_carrier_colons(s: &str) -> Option<String> {
    if s.starts_with("\\\\?\\") || s.starts_with("\\\\.\\") {
        return None;
    }
    let sep = s.rfind(|c| c == '/' || c == '\\');
    let tail = match sep {
        Some(i) => &s[i + 1..],
        None => s,
    };
    if !tail.contains(':') {
        return None;
    }
    if sep.is_none() && s.len() == 2 {
        return None;
    }
    let begin = sep.map(|i| i + 1).unwrap_or(0);
    let mut folded = s.to_string();
    let mut byte = begin;
    while byte < folded.len() {
        if folded.as_bytes()[byte] == b':' {
            folded.replace_range(byte..byte + 1, "\u{F03A}");
            byte += '\u{F03A}'.len_utf8();
        } else {
            byte += 1;
        }
    }
    Some(folded)
}

/// The #1231/#1246/#1283 operand-preparation chain from the C++ resolver
/// (canonicalize_path_w): strip trailing separators, then try the prefix
/// fold, the drive-dialect fold, and the carrier-colon fold in order; a
/// folded operand is native-dialect input. The shell-tree mapping
/// (niubash#346) is inert without a configured shell root and is not
/// carried here.
pub(crate) fn prepare_operand(operand: &str) -> String {
    let stripped = strip_trailing_seps(operand);
    let prefix_folded = fold_posix_drive_prefix(stripped);
    if let Some(folded) = fold_drive_dialect(&prefix_folded) {
        return folded;
    }
    if let Some(carrier) = fold_carrier_colons(&prefix_folded) {
        return carrier;
    }
    operand.to_string()
}

/// ENAMETOOLONG marker for fsutil::reason: a component over NAME_MAX in an
/// existence-checking mode reports "File name too long" ([GNU] #370).
fn name_too_long() -> std::io::Error {
    std::io::Error::new(std::io::ErrorKind::InvalidInput, "name too long")
}

/// True when any component of the (folded) operand exceeds NAME_MAX (255).
fn has_overlong_component(effective: &str) -> bool {
    effective
        .split(['/', '\\'])
        .filter(|c| !c.is_empty() && *c != "." && *c != "..")
        .any(|c| c.chars().count() > 255)
}

/// The -m lexical walk over the FOLDED operand, native-spelled:
/// "X:\..." anchors at the drive root, a POSIX-rooted "/x" that no fold
/// claimed resolves under the cwd (the C++ resolver's current_directory
/// start), everything joins with '\' — the spelling the 1.x applet's -m
/// prints (kept native by #1491-adjacent display rules; the tests pin
/// "E:\...\absent.txt", "X:\", "<cwd>\nodrive").
fn normalize_lex_native(ctx: &Ctx, effective: &str) -> String {
    let cwd = ctx.cwd.to_string_lossy().into_owned();
    let (root, rest): (String, &str) = if effective.len() >= 2
        && effective.as_bytes()[1] == b':'
        && is_drive_letter(effective.as_bytes()[0])
    {
        // Native drive form: the drive root anchors the walk.
        let mut r = String::new();
        r.push(effective.as_bytes()[0] as char);
        r.push_str(":\\");
        let skip = if effective.len() > 2
            && (effective.as_bytes()[2] == b'\\' || effective.as_bytes()[2] == b'/')
        {
            3
        } else {
            2
        };
        (r, &effective[skip.min(effective.len())..])
    } else if effective.starts_with('/') || effective.starts_with('\\') {
        // POSIX-rooted spelling no fold claimed: the C++ resolver walks it
        // from the current directory.
        (cwd.clone(), &effective[1..])
    } else {
        (cwd.clone(), effective)
    };
    let mut stack: Vec<&str> = Vec::new();
    for part in rest.split(['/', '\\']) {
        match part {
            "" | "." => {}
            ".." => {
                stack.pop();
            }
            other => stack.push(other),
        }
    }
    let mut out = root;
    for part in stack {
        if !out.ends_with('\\') {
            out.push('\\');
        }
        out.push_str(part);
    }
    out
}

pub fn canonicalize_operand(
    ctx: &Ctx,
    operand: &str,
    mode: CanonMode,
) -> Result<String, std::io::Error> {
    // GNU treats an empty operand as an error ("'': No such file or
    // directory" in realpath), never as the cwd
    if operand.is_empty() {
        return Err(std::io::Error::from(std::io::ErrorKind::NotFound));
    }
    let effective = prepare_operand(operand);
    // [GNU] A trailing separator makes the final component a directory
    // requirement: an existing regular file ("file.txt/") fails even in
    // the permissive mode, while a missing leaf still resolves.
    let needs_directory = operand.ends_with('/') || operand.ends_with('\\');
    let path = if Path::new(&effective).is_absolute() {
        PathBuf::from(&effective)
    } else {
        ctx.cwd.join(&effective)
    };
    match mode {
        CanonMode::NoCheck => {
            let out = normalize_lex_native(ctx, &effective);
            // [GNU] -m resolves symlinks without existence requirements: a
            // dangling leaf link resolves to its target's path.
            if let Ok(target) = std::fs::read_link(&out) {
                let parent = match std::path::Path::new(&out).parent() {
                    Some(pp) => pp.to_string_lossy().into_owned(),
                    None => String::new(),
                };
                let joined = format!("{}\\{}", parent, target.to_string_lossy());
                return Ok(normalize_lex_native(ctx, &joined));
            }
            Ok(out)
        }
        CanonMode::AllMustExist => {
            if has_overlong_component(&effective) {
                return Err(name_too_long());
            }
            let p = std::fs::canonicalize(&path)?;
            let out = strip_verbatim(p).to_string_lossy().into_owned();
            if needs_directory && !std::path::Path::new(&out).is_dir() {
                return Err(std::io::Error::from(std::io::ErrorKind::NotFound));
            }
            Ok(out)
        }
        CanonMode::AllButLast => {
            if has_overlong_component(&effective) {
                return Err(name_too_long());
            }
            if let Ok(p) = std::fs::canonicalize(&path) {
                let out = strip_verbatim(p).to_string_lossy().into_owned();
                // [GNU] A trailing separator on an existing non-directory is
                // ENOTDIR even in the permissive mode.
                if needs_directory && !std::path::Path::new(&out).is_dir() {
                    return Err(std::io::Error::from(std::io::ErrorKind::NotADirectory));
                }
                return Ok(out);
            }
            // Last component may be missing: canonicalize the parent, which
            // must exist AND be a directory ([GNU] traversing a regular file
            // reports ENOTDIR even when the remaining component is last).
            let parent = path.parent().unwrap_or(Path::new("."));
            let last = path
                .file_name()
                .map(|f| f.to_string_lossy().into_owned())
                .unwrap_or_default();
            let parent_abs = if parent.is_absolute() {
                parent.to_path_buf()
            } else {
                ctx.cwd.join(parent)
            };
            let canon_parent = std::fs::canonicalize(&parent_abs)?;
            let parent_out = strip_verbatim(canon_parent).to_string_lossy().into_owned();
            if !std::path::Path::new(&parent_out).is_dir() {
                return Err(std::io::Error::from(std::io::ErrorKind::NotADirectory));
            }
            let parent_out2 = parent_out.clone();
            let mut out = parent_out;
            if !out.ends_with('/') && !out.ends_with('\\') && !last.is_empty() {
                out.push('\\');
            }
            out.push_str(&last);
            // [GNU] AllButLast resolves the FINAL symlink even when its
            // target does not exist (readlink -f dang, dang -> z_absent_tgt
            // -> <parent>/z_absent_tgt, rc 0): the OS canonicalize failed on
            // the dangling link, so splice the link target onto the
            // canonical parent and fold it lexically.
            if let Ok(target) = std::fs::read_link(&path) {
                let joined = format!("{}\\{}", parent_out2, target.to_string_lossy());
                return Ok(normalize_lex_native(ctx, &joined));
            }
            // [GNU] The last-component exemption ignores trailing slashes: a
            // MISSING leaf followed only by separators still canonicalizes,
            // but an existing non-directory leaf with a trailing separator
            // is ENOTDIR (the OS canonicalize failed on the separator, so
            // the leaf kind is judged explicitly here).
            if needs_directory && !last.is_empty() {
                let leaf = std::path::Path::new(&out);
                if let Ok(m) = std::fs::metadata(leaf) {
                    if !m.is_dir() {
                        return Err(std::io::Error::from(std::io::ErrorKind::NotADirectory));
                    }
                }
            }
            Ok(out)
        }
    }
}

#[cfg(test)]
mod tests {
    use super::{fold_carrier_colons, fold_drive_dialect, fold_posix_drive_prefix, prepare_operand};

    #[test]
    fn drive_prefix_folds_match_the_cxx_boundary() {
        // Probes pinned against the C++ canonicalize_path_w folds
        // (#1145/#1172/#1231/#1246/#1283):
        assert_eq!(fold_posix_drive_prefix("/cygdrive/e/Data"), "/e/Data");
        assert_eq!(fold_posix_drive_prefix("/CYGDRIVE/e"), "/e");
        assert_eq!(fold_posix_drive_prefix("/mnt/e/Data"), "/e/Data");
        assert_eq!(fold_posix_drive_prefix("/mnt/data"), "/mnt/data");
        assert_eq!(fold_posix_drive_prefix("/mntcc"), "/mntcc");
        assert_eq!(fold_posix_drive_prefix("/mnt"), "/mnt");
        assert_eq!(fold_drive_dialect("/e/Data"), Some("E:\\Data".into()));
        assert_eq!(fold_drive_dialect("/e"), Some("E:\\".into()));
        assert_eq!(fold_drive_dialect("/usr/bin"), None);
        assert_eq!(fold_drive_dialect("C:\\x"), None);
        // "D:name" carries the carrier-colon encoding; bare "D:" stays a drive.
        assert_eq!(
            fold_carrier_colons("D:name"),
            Some(format!("D{}name", '\u{F03A}' as char))
        );
        assert_eq!(fold_carrier_colons("D:"), None);
        assert_eq!(fold_carrier_colons("a/D:b"), Some("a/D\u{F03A}b".into()));
        // Trailing separators strip before the folds; the unchanged operand
        // falls through verbatim.
        assert_eq!(prepare_operand("a/b/"), "a/b/");
        assert_eq!(prepare_operand("/c/Users/x"), "C:\\Users\\x");
        assert_eq!(prepare_operand("/cygdrive/c/x/"), "C:\\x");
    }
}

#[cfg(test)]
mod dangling_tests {
    use super::{canonicalize_operand, CanonMode};
    use winux_core::test_support::{MemIo, ScratchCtx};

    fn try_symlink(sc: &ScratchCtx, target: &str, link: &str) -> bool {
        let out = std::process::Command::new("cmd")
            .args(["/C", "mklink"])
            .arg(sc.path(link))
            .arg(target)
            .output();
        match out {
            Ok(o) if o.status.success() => true,
            _ => false,
        }
    }

    #[test]
    fn all_but_last_and_missing_resolve_dangling_leaf() {
        // Probed GNU 9.4: readlink -f dang / realpath dang / realpath -m dang
        // with dang -> z_absent_tgt all print <parent>/z_absent_tgt, rc 0.
        let sc = ScratchCtx::new("canon-dang");
        let ctx = sc.ctx();
        if !try_symlink(&sc, "z_absent_tgt", "dang") {
            return; // environment cannot create symlinks
        }
        for mode in [CanonMode::AllButLast, CanonMode::NoCheck] {
            let r = canonicalize_operand(&ctx, "dang", mode).unwrap();
            assert!(
                r.ends_with("z_absent_tgt"),
                "mode={mode:?} must resolve the dangling leaf to its target, got {r}"
            );
            assert!(!r.contains("dang"), "mode={mode:?} got {r}");
        }
        // -e still fails on the dangling link (all components must exist)
        let r = canonicalize_operand(&ctx, "dang", CanonMode::AllMustExist);
        assert!(r.is_err());
    }
}
