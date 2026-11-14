//! Shared filesystem helpers for wave1 commands: GNU-style reason strings
//! for io errors and lexical path splitting (GNU works on `/`-split strings,
//! so we do too — no `PathBuf` component normalization that would swallow
//! empty components or change message text).

use std::io;
use std::path::{Path, PathBuf};

use winux_core::Ctx;

/// Map an io error to the GNU strerror-style reason string.
pub fn reason(e: &io::Error) -> &'static str {
    match e.kind() {
        io::ErrorKind::NotFound => "No such file or directory",
        io::ErrorKind::AlreadyExists => "File exists",
        io::ErrorKind::PermissionDenied => "Permission denied",
        io::ErrorKind::NotADirectory => "Not a directory",
        io::ErrorKind::DirectoryNotEmpty => "Directory not empty",
        io::ErrorKind::IsADirectory => "Is a directory",
        io::ErrorKind::InvalidInput => "File name too long",
        _ => "Invalid argument",
    }
}

/// Lexically split a path operand into components on `/` only (GNU
/// semantics; backslash is NOT a separator in GNU string processing).
pub fn split_slashes(p: &str) -> Vec<&str> {
    p.split('/').filter(|c| !c.is_empty()).collect()
}

/// GNU quotef-style quoting for stderr diagnostics: quote with `'...'`
/// when the name is empty or contains characters outside the safe set
/// (`missing.txt` stays bare, `*.txt` and the empty name print as
/// `'*.txt'` / `''` — probed GNU 9.4, WSL + MSYS).
pub fn quotef(name: &str) -> String {
    let safe = !name.is_empty()
        && name
            .bytes()
            .all(|b| b.is_ascii_alphanumeric() || b"_.-/:@%^+,".contains(&b));
    if safe {
        name.to_string()
    } else {
        format!("'{name}'")
    }
}

/// GNU strerror for a failed open of a literal operand, recovered from the
/// Windows-native failure: empty names and invalid-name artifacts (literal
/// `*.txt`, trailing `/`) cannot be opened natively, so the operand string
/// plus a metadata probe reconstructs what open(2) reports on GNU — probed
/// byte-identical on WSL 9.4 and MSYS (target/wc2b-probe probes).
///
/// `create` selects the O_CREAT semantics GNU truncate runs with: a missing
/// `name/` is EISDIR because the trailing slash demands a directory, while
/// a plain open (basenc/digest/tac) reports ENOENT for the same operand.
pub fn gnu_open_reason(ctx: &Ctx, operand: &str, e: &io::Error, create: bool) -> String {
    if operand.is_empty() {
        // open("", ...) is ENOENT on GNU; the empty name never resolves
        // to the cwd the way a Windows CreateFile("") would.
        return "No such file or directory".to_string();
    }
    if let Some(stripped) = operand.strip_suffix('/') {
        let exists = !stripped.is_empty() && std::fs::metadata(resolve(ctx, stripped)).is_ok();
        return if exists {
            "Not a directory".to_string()
        } else if create {
            "Is a directory".to_string()
        } else {
            "No such file or directory".to_string()
        };
    }
    // A directory operand "opens" on Linux and dies at the read (EISDIR);
    // Windows-native opens fail outright, so recover the reason via a
    // metadata check.
    if std::fs::metadata(resolve(ctx, operand))
        .map(|m| m.is_dir())
        .unwrap_or(false)
    {
        return "Is a directory".to_string();
    }
    if !create && reason(e) == "Invalid argument" {
        // Windows-invalid-name artifact for a target that does not exist:
        // GNU reports ENOENT because open(2) never accepts the name.
        return "No such file or directory".to_string();
    }
    reason(e).to_string()
}

/// Join `base` with a lexical component.
pub fn join_lex(base: &Path, comp: &str) -> PathBuf {
    base.join(comp)
}

/// Absolute (ctx.cwd-anchored) path of a possibly relative operand.
///
/// The shell-namespace boundary (#1488 wave3b, the winux-core port of 1.x
/// native_path.cppm normalize_api_operand_w): POSIX spellings the shell
/// hands its own applets verbatim (/dev/*, /tmp, /var/tmp, /home, /mnt/X,
/// the MSYS /X drive form, and everything under the configured root) map
/// to their native paths here; relative and already-native operands keep
/// the ctx.cwd join exactly as before.
pub fn resolve(ctx: &Ctx, operand: impl AsRef<Path>) -> PathBuf {
    let p = operand.as_ref();
    if let Some(s) = p.to_str() {
        let mapped = winux_core::path::normalize_operand(ctx.env, &ctx.cwd, s);
        // only a claimed spelling changes the resolution — and only when
        // the result stands on its own (an absolute native path, or a
        // device name Win32 resolves cwd-independently). Relative
        // operands keep the ctx.cwd join so the OS-level trailing-
        // separator semantics GNU relies on stay intact.
        if mapped != s
            && (Path::new(&mapped).is_absolute()
                || winux_core::path::resolve_pseudo_device(s).is_some())
        {
            return literal_operand_path(&mapped);
        }
    }
    if p.is_absolute() {
        return literal_operand_path(&p.to_string_lossy());
    }
    let joined = ctx.cwd.join(p);
    // the NUL.txt trap (#1333) and the literal trailing-dot/space names
    // (#1283/#1332) resolve through the \\?\ namespace; every other
    // operand keeps the plain cwd join.
    literal_operand_path(&joined.to_string_lossy())
}

/// The \\?\ literal spelling of a native absolute path (UNC through the
/// \\?\UNC\ form) — the #1333 device-stem trap needs it.
fn extended_literal(native_abs: &str) -> PathBuf {
    if let Some(rest) = native_abs.strip_prefix(r"\\?\") {
        return PathBuf::from(format!(r"\\?\{rest}"));
    }
    if let Some(rest) = native_abs.strip_prefix(r"\\") {
        return PathBuf::from(format!(r"\\?\UNC\{rest}"));
    }
    PathBuf::from(format!(r"\\?\{native_abs}"))
}

/// native_path.cppm has_literal_dot_space_component (#1283/#1332): a
/// component whose last character is '.' or ' ' (other than "." / "..")
/// names a literal on-disk entry under the NT namespace, but every plain
/// Win32 path call strips the suffix and reaches a DIFFERENT object — so
/// such operands must open through the \\?\ spelling.
fn has_literal_dot_space_component(native: &str) -> bool {
    native.split(['\\', '/']).any(|component| {
        !component.is_empty()
            && component != "."
            && component != ".."
            && (component.ends_with('.') || component.ends_with(' '))
    })
}

/// Absolute \\?\ (or \\?\UNC\) spelling of `native` with `.`/`..` folded
/// lexically — the extended namespace itself does not normalize them, so the
/// fold is ours (native_path.cppm to_extended_path does the same split).
fn extended_preserving(native: &str) -> PathBuf {
    if native.starts_with(r"\\?\") || native.starts_with(r"\\.\") {
        return PathBuf::from(native);
    }
    use std::path::Component;
    let mut prefix: Option<String> = None;
    let mut parts: Vec<String> = Vec::new();
    for component in Path::new(native).components() {
        match component {
            Component::Prefix(p) => {
                prefix = Some(p.as_os_str().to_string_lossy().into_owned())
            }
            Component::RootDir => {}
            Component::CurDir => {}
            Component::ParentDir => {
                parts.pop();
            }
            Component::Normal(name) => {
                parts.push(name.to_string_lossy().into_owned())
            }
        }
    }
    let body = parts.join("\\");
    let Some(prefix) = prefix else {
        // Rooted but prefix-less ("\x"): the caller joined the cwd, so this
        // only happens for a malformed absolute spelling; keep it verbatim.
        return PathBuf::from(native);
    };
    if let Some(unc) = prefix.strip_prefix(r"\\") {
        let rest = if body.is_empty() {
            unc.to_string()
        } else {
            format!("{unc}\\{body}")
        };
        return PathBuf::from(format!(r"\\?\UNC\{rest}"));
    }
    let rest = if body.is_empty() {
        format!("{prefix}\\")
    } else {
        format!("{prefix}\\{body}")
    };
    PathBuf::from(format!(r"\\?\{rest}"))
}

/// Resolve an operand the way the 1.x native_path boundary did: the extended
/// spelling wins only for operands the Win32 plain path cannot address
/// (#1332 trailing dot/space components, #1333 device-stem leaves).
fn literal_operand_path(native: &str) -> PathBuf {
    if has_literal_dot_space_component(native) {
        return extended_preserving(native);
    }
    if winux_core::path::has_device_stem_with_extension(native) {
        return extended_literal(native);
    }
    PathBuf::from(native)
}

/// Lexical normalization used by the `-m` (no existence check) modes:
/// absolutize against ctx.cwd, then collapse `.` and `..` (and duplicated
/// separators) without touching the filesystem.
pub fn normalize_lex(ctx: &Ctx, operand: &str) -> String {
    let (prefix, rest) = if let Some(r) = operand.strip_prefix('/') {
        ("/".to_string(), r)
    } else {
        (format!("{}", ctx.cwd.to_string_lossy()), operand)
    };
    let mut stack: Vec<String> = Vec::new();
    for part in rest.split('/') {
        match part {
            "" | "." => {}
            ".." => {
                stack.pop();
            }
            other => stack.push(other.to_string()),
        }
    }
    let mut out = prefix;
    for (i, part) in stack.iter().enumerate() {
        if i > 0 || (out != "/" && !out.ends_with('/') && !out.is_empty()) {
            out.push('/');
        }
        out.push_str(part);
    }
    if out.is_empty() {
        "/".to_string()
    } else {
        out
    }
}

#[cfg(test)]
mod tests {
    use super::normalize_lex;
    use winux_core::test_support::ScratchCtx;

    #[test]
    fn lexical_normalization() {
        let sc = ScratchCtx::new("fsutil");
        let ctx = sc.ctx();
        assert_eq!(
            normalize_lex(&ctx, "a/b/../c"),
            format!("{}/a/c", sc.dir.to_string_lossy())
        );
        assert_eq!(normalize_lex(&ctx, "/"), "/");
        assert_eq!(
            normalize_lex(&ctx, "a/./b//c"),
            format!("{}/a/b/c", sc.dir.to_string_lossy())
        );
    }
}
