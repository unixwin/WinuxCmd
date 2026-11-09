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
    let path = if Path::new(operand).is_absolute() {
        PathBuf::from(operand)
    } else {
        ctx.cwd.join(operand)
    };
    match mode {
        CanonMode::NoCheck => Ok(crate::fsutil::normalize_lex(ctx, operand)),
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
