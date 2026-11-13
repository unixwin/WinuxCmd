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

fn is_drive_letter(b: u8) -> bool {
    b.is_ascii_alphabetic()
}

/// The #1231/#1246/#1283 operand-preparation chain from the C++ resolver
/// (canonicalize_path_w:1371): strip trailing separators, then the prefix
/// fold, the drive-dialect fold, the carrier-colon fold, and the
/// shell-tree mapping — a folded operand is native-dialect input. The
/// folds live in winux_core::path since the wave3b port (#1488); the
/// shell-tree leg (niubash#346) was deliberately absent before wave3b and
/// is inert without a configured shell root.
pub(crate) fn prepare_operand(ctx: &Ctx, operand: &str) -> String {
    winux_core::path::prepare_operand(ctx.env, operand)
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
    let effective = prepare_operand(ctx, operand);
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
    use super::prepare_operand;
    use winux_core::MapEnv;

    fn env(vars: &[(&str, &str)]) -> MapEnv {
        let mut e = MapEnv::new();
        for (k, v) in vars {
            e.set(k, v);
        }
        e
    }

    #[test]
    fn drive_prefix_folds_match_the_cxx_boundary() {
        // Probes pinned against the C++ canonicalize_path_w folds
        // (#1145/#1172/#1231/#1246/#1283) — now via the winux-core port:
        let p = |operand: &str, vars: &[(&str, &str)]| {
            prepare_operand(
                &winux_core::Ctx {
                    cwd: std::path::PathBuf::from("wd"),
                    env: &env(vars),
                },
                operand,
            )
        };
        let none: [(&str, &str); 0] = [];
        // drive dialect and the cygdrive/mnt prefixes fold to the native
        // spelling without a shell root
        assert_eq!(p("/e/Data", &none), "E:\\Data");
        assert_eq!(p("/e", &none), "E:\\");
        assert_eq!(p("/cygdrive/e/Data", &none), "E:\\Data");
        assert_eq!(p("/CYGDRIVE/e", &none), "E:\\");
        assert_eq!(p("/mnt/e/Data", &none), "E:\\Data");
        // near-misses stay literal (ordinary /mnt and /mntcc directories)
        assert_eq!(p("/mnt/data", &none), "/mnt/data");
        assert_eq!(p("/mntcc", &none), "/mntcc");
        assert_eq!(p("/mnt", &none), "/mnt");
        assert_eq!(p("/usr/bin", &none), "/usr/bin");
        assert_eq!(p("C:\\x", &none), "C:\\x");
        // "D:name" carries the carrier-colon encoding; bare "D:" stays a drive.
        assert_eq!(p("D:name", &none), format!("D{}name", '\u{F03A}' as char));
        assert_eq!(p("D:", &none), "D:");
        assert_eq!(p("a/D:b", &none), format!("a/D{}b", '\u{F03A}' as char));
        // Trailing separators strip before the folds; the unchanged operand
        // falls through verbatim.
        assert_eq!(p("a/b/", &none), "a/b/");
        assert_eq!(p("/c/Users/x", &none), "C:\\Users\\x");
        assert_eq!(p("/cygdrive/c/x/", &none), "C:\\x");
        // shell-tree leg (wave3b): a configured root claims the remaining
        // "/"-prefixed spellings; /tmp pins to TMPDIR.
        let rooted = &[("WINUXSH_ROOT", "D:\\shellroot"), ("TMPDIR", "C:\\t")];
        assert_eq!(p("/etc/passwd", rooted), "D:\\shellroot\\etc\\passwd");
        assert_eq!(p("/tmp/x", rooted), "C:\\t\\x");
        assert_eq!(p("/", rooted), "D:\\shellroot");
        // devices stay literal (the resolver never names the Windows device)
        assert_eq!(p("/dev/null", rooted), "/dev/null");
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
            // the link name must not survive as a path component; checked
            // per-component because the scratch dir itself is named
            // canon-dang-* (a plain contains("dang") matches it and would
            // fail on symlink-capable hosts like the CI runner)
            assert!(
                !r.split(['\\', '/']).any(|c| c == "dang"),
                "mode={mode:?} got {r}"
            );
        }
        // -e still fails on the dangling link (all components must exist)
        let r = canonicalize_operand(&ctx, "dang", CanonMode::AllMustExist);
        assert!(r.is_err());
    }
}
