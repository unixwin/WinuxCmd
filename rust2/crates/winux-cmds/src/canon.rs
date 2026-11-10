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

#[derive(Clone, Copy, PartialEq)]
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
fn prepare_operand(operand: &str) -> String {
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
    let path = if Path::new(&effective).is_absolute() {
        PathBuf::from(&effective)
    } else {
        ctx.cwd.join(&effective)
    };
    match mode {
        CanonMode::NoCheck => Ok(crate::fsutil::normalize_lex(ctx, &effective)),
        CanonMode::AllMustExist => {
            let p = std::fs::canonicalize(&path)?;
            Ok(strip_verbatim(p).to_string_lossy().into_owned())
        }
        CanonMode::AllButLast => {
            if let Ok(p) = std::fs::canonicalize(&path) {
                return Ok(strip_verbatim(p).to_string_lossy().into_owned());
            }
            // last component may be missing: canonicalize the parent
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
            let mut out = strip_verbatim(canon_parent).to_string_lossy().into_owned();
            if !out.ends_with('/') && !out.ends_with('\\') && !last.is_empty() {
                out.push('\\');
            }
            out.push_str(&last);
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
