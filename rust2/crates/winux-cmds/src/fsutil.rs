//! Shared filesystem helpers for wave1 commands: GNU-style reason strings
//! for io errors and lexical path splitting (GNU works on `/`-split strings,
//! so we do too — no `PathBuf` component normalization that would swallow
//! empty components or change message text).

use std::ffi::OsStr;
use std::io;
use std::path::{Component, Path, PathBuf, Prefix};

use winux_core::{tr, Ctx};

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
            return PathBuf::from(mapped);
        }
    }
    if p.is_absolute() {
        p.to_path_buf()
    } else {
        let joined = ctx.cwd.join(p);
        // the NUL.txt trap (#1333): a leaf whose stem is a DOS device name
        // ("NUL.txt") resolves onto the DEVICE namespace through the plain
        // Win32 open ("NUL.txt" answers a stat!), while GNU and the 1.x
        // applets (native_path.cppm to_extended_path_preserving, the
        // has_device_stem_with_extension_w branch) treat it as a literal
        // on-disk name. Probes and opens go through the \\?\ extended
        // spelling, which disables the device-stem resolution.
        let text = joined.to_string_lossy().into_owned();
        if winux_core::path::has_device_stem_with_extension(&text) {
            return extended_literal(&text);
        }
        joined
    }
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

// ---------------------------------------------------------------------------
// Preserve-root failsafe (the GNU chmod/chown/chgrp contract on Windows
// spellings). Shared by the three commands so every root spelling — POSIX,
// drive, UNC, verbatim, dot-folded, or a shell-namespace alias — hits the
// same boundary.
// ---------------------------------------------------------------------------

/// True when `operand` — with `resolved` already produced by the caller's
/// `resolve(ctx, operand)` — names a filesystem or shell-namespace root
/// that recursive operation must refuse when the command's
/// `--preserve-root` flag is in force.
///
/// Pure lexical + injected-env check: no stat, canonicalize, read_dir,
/// network probe, process env/cwd access, or mutation. Commands pass the
/// ORIGINAL display operand plus the resolved path; this helper never
/// re-resolves and never touches the filesystem.
pub(crate) fn is_preserve_root_operand(ctx: &Ctx, operand: &str, resolved: &Path) -> bool {
    if raw_root_operand(operand) {
        return true;
    }
    let native = normalize_native(resolved);
    is_native_volume_root(&native) || namespace_root_matches(ctx, &native)
}

/// The default-walk safety floor (A1 matrix adjudication, blocker 1):
/// GNU coreutils 9.4 WALKS `/` by default (`bool preserve_root = false` in
/// chmod.c/chgrp.c/chown.c main — refusal is opt-in via --preserve-root),
/// and the shell-namespace sandbox root keeps that walkable-by-default
/// behavior here. But a recursive walk of a REAL native volume root
/// (drive root, share root, a raw separator spelling, or a reparse point
/// resolving onto one) is unrecoverable on Windows, so those identities
/// are refused even without --preserve-root and even with
/// --no-preserve-root. Recorded divergence; namespace roots are exempt.
pub(crate) fn is_native_root_operand(ctx: &Ctx, operand: &str, resolved: &Path) -> bool {
    let native = normalize_native(resolved);
    // The namespace exemption applies ONLY to a CONFIGURED shell root (the
    // injected sandbox is what "/" means to the running shell). The
    // cwd-volume fallback must never exempt the real volume root — that
    // would let a standalone run walk C:\ by default (the A1 incident
    // class, reproduced by the first floor test and fixed 2026-11-14).
    if winux_core::path::shell_root(ctx.env).is_some() && namespace_root_matches(ctx, &native) {
        return false;
    }
    raw_root_operand(operand) || is_native_volume_root(&native)
}

/// GNU ROOT_DEV_INO_WARN display rule (probed WSL coreutils 9.4, M2/M4
/// matrices): the operand "/" prints plain `'/'; the two-separator
/// spelling "//" prints verbatim with the `(same as '/')` suffix; longer
/// pure-separator spellings fold to plain `/`; every other identity that
/// stats to the root (`/.`, `/tmp/..`, `/proc/self/root`, a -H/-L symlink
/// to the root) prints the operand verbatim with the suffix. Returns
/// (display string, needs the suffix).
pub(crate) fn preserve_root_display(operand: &str) -> (String, bool) {
    let b = operand.as_bytes();
    if !b.is_empty() && b.iter().all(|&c| c == b'/' || c == b'\\') {
        if b.len() == 2 {
            return (operand.to_string(), true);
        }
        return ("/".to_string(), false);
    }
    (operand.to_string(), true)
}

/// The GNU two-line failsafe for `cmd` on `operand` (probed byte shape:
/// `cmd: it is dangerous to operate recursively on 'X'[ (same as '/')]
/// cmd: use --no-preserve-root to override this failsafe`, rc 1).
///
/// Both lines are I18N-keyed per command (`command.<cmd>.error.*`); the
/// English fallbacks are GNU `error()`'s INNER wording and the `cmd: `
/// prefix stays in code, so a catalog template never has to repeat the
/// program name. The operand is a binding, not template text — a brace in
/// a user path is substituted once and never re-scanned.
pub(crate) fn preserve_root_refusal_lines(ctx: &Ctx, cmd: &str, operand: &str) -> [String; 2] {
    let (display, suffix) = preserve_root_display(operand);
    // The "(same as '/')" suffix is part of the sentence, not a data
    // binding: a trailing English fragment glued to a translated clause
    // cannot be localized, so the suffix and non-suffix shapes are distinct
    // keys (GNU prints the operand verbatim either way). tr! needs literal
    // keys, hence the per-command spelling.
    let (danger, hint) = match cmd {
        "chmod" => (
            if suffix {
                tr!(
                    ctx,
                    "command.chmod.error.dangerous_recursive_root",
                    "it is dangerous to operate recursively on '{operand}' (same as '/')",
                    operand = display
                )
            } else {
                tr!(
                    ctx,
                    "command.chmod.error.dangerous_recursive",
                    "it is dangerous to operate recursively on '{operand}'",
                    operand = display
                )
            },
            tr!(
                ctx,
                "command.chmod.error.preserve_root_hint",
                "use --no-preserve-root to override this failsafe"
            ),
        ),
        "chgrp" => (
            if suffix {
                tr!(
                    ctx,
                    "command.chgrp.error.dangerous_recursive_root",
                    "it is dangerous to operate recursively on '{operand}' (same as '/')",
                    operand = display
                )
            } else {
                tr!(
                    ctx,
                    "command.chgrp.error.dangerous_recursive",
                    "it is dangerous to operate recursively on '{operand}'",
                    operand = display
                )
            },
            tr!(
                ctx,
                "command.chgrp.error.preserve_root_hint",
                "use --no-preserve-root to override this failsafe"
            ),
        ),
        _ => (
            if suffix {
                tr!(
                    ctx,
                    "command.chown.error.dangerous_recursive_root",
                    "it is dangerous to operate recursively on '{operand}' (same as '/')",
                    operand = display
                )
            } else {
                tr!(
                    ctx,
                    "command.chown.error.dangerous_recursive",
                    "it is dangerous to operate recursively on '{operand}'",
                    operand = display
                )
            },
            tr!(
                ctx,
                "command.chown.error.preserve_root_hint",
                "use --no-preserve-root to override this failsafe"
            ),
        ),
    };
    [format!("{cmd}: {danger}"), format!("{cmd}: {hint}")]
}

/// Reparse-point identity (blocker 3 mapping): a junction/symlink whose
/// TARGET is a filesystem root carries the root identity through the link.
/// This is the Windows analog of GNU's stat-based (dev,ino) check, which
/// refuses `chmod -RH --preserve-root <symlink-to-root>` upstream (the
/// upstream comment in chown-core.c names exactly that shape, and the M2
/// P8/P10/P12 cells confirm it). Pure lexical: `read_link` + normalize +
/// the injected-namespace comparison — the link is never followed here.
pub(crate) fn link_target_is_root(ctx: &Ctx, path: &Path) -> bool {
    let target = match std::fs::read_link(path) {
        Ok(t) => t,
        Err(_) => return false,
    };
    let anchored = if target.is_absolute() {
        target
    } else if let Some(parent) = path.parent() {
        parent.join(&target)
    } else {
        target
    };
    let native = normalize_native(&anchored);
    is_native_volume_root(&native) || namespace_root_matches(ctx, &native)
}

/// POSIX-spelling half: an all-separator operand ("/", "//", "\\", ...) or
/// a rooted operand whose lexical `.`/`..` fold removes every component
/// ("/.", "/e/..", "///e/../..") names the root; `..` clamps at the root
/// and can never go above it. Exactly two leading separators followed by
/// a component are opted OUT of this raw collapse — the C++ contract
/// treats "//x" as the implementation-defined (UNC-ish) spelling, so
/// "//e/.." must not be protected here; the native checks still get their
/// say through `resolved`.
fn raw_root_operand(operand: &str) -> bool {
    let b = operand.as_bytes();
    if b.is_empty() {
        return false;
    }
    let is_sep = |c: u8| c == b'/' || c == b'\\';
    if b.iter().all(|&c| is_sep(c)) {
        return true;
    }
    let lead = b.iter().take_while(|&&c| is_sep(c)).count();
    if lead == 0 || lead == 2 {
        return false;
    }
    let mut depth = 0usize;
    for comp in operand[lead..].split(|c| c == '/' || c == '\\') {
        match comp {
            "" | "." => {}
            ".." => depth = depth.saturating_sub(1),
            _ => depth += 1,
        }
    }
    depth == 0
}

/// Lexical `.`/`..` fold of an already-resolved native path: the verbatim
/// prefix is stripped first, `.` is dropped, and `..` pops only a Normal
/// component, clamping at the volume/namespace root — a `..` can never
/// escape `C:\` or a UNC share root.
fn normalize_native(p: &Path) -> PathBuf {
    let stripped = winux_core::strip_verbatim(p.to_path_buf());
    let mut kept = Vec::new();
    for c in stripped.components() {
        match c {
            Component::CurDir => {}
            Component::ParentDir => {
                if matches!(kept.last(), Some(Component::Normal(_))) {
                    kept.pop();
                }
            }
            other => kept.push(other),
        }
    }
    let mut out = PathBuf::new();
    for c in kept {
        out.push(c.as_os_str());
    }
    out
}

/// Native volume/share root check on the NORMALIZED resolved path:
/// a bare drive prefix (`D:` — the preexisting policy), prefix+RootDir
/// (`D:\`), a plain RootDir (`\`), or a VALID UNC share root
/// (`\\srv\share`). UNC server/share components that are empty, `.` or
/// `..` are rejected as roots — `\\e\..` is not one — and a bare server
/// without a share never qualifies.
fn is_native_volume_root(p: &Path) -> bool {
    let comps: Vec<Component> = p.components().collect();
    match comps.as_slice() {
        [] => false,
        [Component::Prefix(pre)] => {
            !matches!(pre.kind(), Prefix::UNC(..) | Prefix::VerbatimUNC(..))
        }
        [Component::Prefix(pre), Component::RootDir] => match pre.kind() {
            Prefix::UNC(s, sh) | Prefix::VerbatimUNC(s, sh) => {
                valid_unc_component(s) && valid_unc_component(sh)
            }
            _ => true,
        },
        [Component::RootDir] => true,
        _ => false,
    }
}

fn valid_unc_component(s: &OsStr) -> bool {
    let s = s.to_string_lossy();
    !s.is_empty() && s != "." && s != ".."
}

/// Shell-namespace half: the configured root (`__RUBASH_SHELL_ROOT` and
/// its aliases, via winux_core::path::shell_root) is what POSIX `/` means
/// to the shell; without one, the cwd's own volume root is the fallback.
/// Both sides are normalized lexically and compared ASCII
/// case-insensitively — this is what catches `.`, `./`, `././`, `..` and
/// native/case aliases of the namespace root.
fn namespace_root_matches(ctx: &Ctx, native: &Path) -> bool {
    let Some(root) = namespace_root(ctx) else {
        return false;
    };
    let want = path_key(&root);
    !want.is_empty() && want.eq_ignore_ascii_case(&path_key(native))
}

fn namespace_root(ctx: &Ctx) -> Option<PathBuf> {
    if let Some(root) = winux_core::path::shell_root(ctx.env) {
        let native = winux_core::path::nativeize_base(&root);
        let p = Path::new(&native);
        let anchored = if p.is_absolute() {
            p.to_path_buf()
        } else {
            ctx.cwd.join(p)
        };
        return Some(normalize_native(&anchored));
    }
    volume_root_of(&normalize_native(&ctx.cwd))
}

/// The prefix+RootDir portion of a normalized path ("C:\x\y" -> "C:\",
/// "\\srv\share\x" -> "\\srv\share\"). A bare drive prefix still names its
/// volume root ("D:" -> "D:\"). None when the path has no native root —
/// e.g. a relative cwd — which disables the namespace comparison while
/// the raw/native-root guards keep working.
fn volume_root_of(p: &Path) -> Option<PathBuf> {
    let mut it = p.components();
    let mut out = PathBuf::new();
    match it.next() {
        Some(Component::Prefix(pre)) => {
            out.push(pre.as_os_str());
            out.push(Component::RootDir.as_os_str());
            Some(out)
        }
        Some(Component::RootDir) => {
            out.push(Component::RootDir.as_os_str());
            Some(out)
        }
        _ => None,
    }
}

/// Canonical comparison key for a normalized native path: components
/// re-joined with `\` so separator spelling and trailing separators
/// collapse; the caller compares with ASCII case-insensitivity.
fn path_key(p: &Path) -> String {
    let mut key = String::new();
    for c in p.components() {
        match c {
            Component::Prefix(pre) => key.push_str(&pre.as_os_str().to_string_lossy()),
            Component::RootDir => key.push('\\'),
            Component::CurDir | Component::ParentDir => {}
            Component::Normal(n) => {
                if !key.is_empty() && !key.ends_with('\\') {
                    key.push('\\');
                }
                key.push_str(&n.to_string_lossy());
            }
        }
    }
    key
}

#[cfg(test)]
mod tests {
    use super::{is_preserve_root_operand, normalize_lex};
    use std::path::{Path, PathBuf};
    use winux_core::test_support::ScratchCtx;
    use winux_core::{Ctx, MapEnv};

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

    /// The shared preserve-root guard, fed synthetic ctx/env and an
    /// explicit resolved path — never resolve() or touch real paths.
    fn guard(ctx: &Ctx, operand: &str, resolved: &str) -> bool {
        is_preserve_root_operand(ctx, operand, Path::new(resolved))
    }

    #[test]
    fn preserve_root_operand_raw_and_native_roots() {
        // no shell root configured: the namespace fallback is the cwd's
        // own volume root (C:\), so nothing below C:\work matches.
        let env = MapEnv::new();
        let ctx = Ctx::new(PathBuf::from(r"C:\work"), &env);
        // raw all-separator and rooted dot-fold spellings
        for (op, resolved) in [
            ("/", "/"),
            ("//", "//"),
            ("///", "///"),
            ("\\", "\\"),
            ("\\\\", "\\\\"),
            ("/.", r"C:\"),
            ("/./", r"C:\"),
            ("/..", r"C:\"),
            ("/e/..", r"E:\"),
            ("/tmp/..", r"T:\"),
            ("///e/../..", r"E:\"),
        ] {
            assert!(guard(&ctx, op, resolved), "raw root {op:?}");
        }
        // spellings whose RESOLVED form is a native volume/share root;
        // resolved strings keep their '..' unfolded so normalize_native
        // performs the actual ParentDir boundary work.
        for (op, resolved) in [
            ("/e", r"E:\"),
            ("/mnt/e", r"E:\"),
            ("/cygdrive/e", r"E:\"),
            (r"E:\foo\..", r"E:\foo\.."),
            ("C:", "C:"),
            ("D:", "D:"),
            (r"\\server\share", r"\\server\share"),
            (r"\\server\share\dir\..", r"\\server\share\dir\.."),
            (r"\\server\share\..\..", r"\\server\share\..\.."),
            (r"\\?\E:\", r"\\?\E:\"),
            (r"\\?\UNC\server\share", r"\\?\UNC\server\share"),
        ] {
            assert!(guard(&ctx, op, resolved), "native root {op:?}");
        }
        // non-roots: subdirectory, dot below the volume root, plain file,
        // UNC child, a '..' that lands beside the root, and the
        // exactly-two-slash UNC-ish spelling whose share component '..'
        // is not a valid root.
        for (op, resolved) in [
            ("sub", r"C:\work\sub"),
            (".", r"C:\work"),
            ("f", r"C:\work\f"),
            (r"\\server\share\dir", r"\\server\share\dir"),
            (r"E:\foo\..\bar", r"E:\foo\..\bar"),
            ("//e/..", r"\\e\.."),
            (r"E:\foo", r"E:\foo"),
        ] {
            assert!(!guard(&ctx, op, resolved), "non-root {op:?}");
        }
    }

    #[test]
    fn preserve_root_display_rule() {
        // GNU ROOT_DEV_INO_WARN display (M2 P-cells / M4 g-cells): "/" and
        // 3+-separator spellings print plain; "//" and every other root
        // identity print verbatim with the "(same as '/')" suffix.
        use super::preserve_root_display;
        let env = MapEnv::new();
        let ctx = Ctx::new(PathBuf::from("wd"), &env);
        assert_eq!(preserve_root_display("/"), ("/".to_string(), false));
        assert_eq!(preserve_root_display("///"), ("/".to_string(), false));
        assert_eq!(preserve_root_display("////"), ("/".to_string(), false));
        assert_eq!(preserve_root_display("//"), ("//".to_string(), true));
        assert_eq!(preserve_root_display("/."), ("/.".to_string(), true));
        assert_eq!(
            preserve_root_display("/tmp/.."),
            ("/tmp/..".to_string(), true)
        );
        assert_eq!(
            preserve_root_display("/proc/self/root"),
            ("/proc/self/root".to_string(), true)
        );
        let lines = super::preserve_root_refusal_lines(&ctx, "chown", "/tmp/..");
        assert_eq!(
            lines[0],
            "chown: it is dangerous to operate recursively on '/tmp/..' (same as '/')"
        );
        assert_eq!(
            lines[1],
            "chown: use --no-preserve-root to override this failsafe"
        );
        let lines = super::preserve_root_refusal_lines(&ctx, "chmod", "////");
        assert_eq!(
            lines[0],
            "chmod: it is dangerous to operate recursively on '/'"
        );
    }

    #[test]
    fn native_floor_predicate() {
        // The default-walk floor (A1 adjudication): real native volume
        // roots refuse; the exemption applies only to a CONFIGURED shell
        // root, never the cwd-volume fallback (that fallback exemption let
        // the first floor test walk the real C: drive — fixed 2026-11-14).
        use super::is_native_root_operand;
        let env = MapEnv::new();
        let ctx = Ctx::new(PathBuf::from(r"C:\\work"), &env);
        assert!(is_native_root_operand(&ctx, r"C:\\", Path::new(r"C:\\")));
        assert!(is_native_root_operand(&ctx, "/", Path::new("/")));
        assert!(is_native_root_operand(&ctx, "/e/..", Path::new(r"E:\\")));
        assert!(is_native_root_operand(&ctx, "//", Path::new("//")));
        // configured shell root: the sandbox namespace "/" is exempt
        let mut env2 = MapEnv::new();
        env2.set("__RUBASH_SHELL_ROOT", r"T:\\sandbox\\root");
        let ctx2 = Ctx::new(PathBuf::from(r"T:\\sandbox\\root"), &env2);
        assert!(!is_native_root_operand(
            &ctx2,
            "/",
            Path::new(r"T:\\sandbox\\root")
        ));
        assert!(!is_native_root_operand(
            &ctx2,
            ".",
            Path::new(r"T:\\sandbox\\root")
        ));
        // but native roots under a configured root still refuse
        assert!(is_native_root_operand(&ctx2, r"C:\\", Path::new(r"C:\\")));
        // subpaths are never floor roots
        assert!(!is_native_root_operand(
            &ctx,
            "sub",
            Path::new(r"C:\\work\\sub")
        ));
    }

    #[test]
    fn preserve_root_operand_shell_namespace() {
        // configured shell root T:\sandbox\root; cwd sits ON the root.
        let mut env = MapEnv::new();
        env.set("__RUBASH_SHELL_ROOT", r"T:\sandbox\root");
        let ctx = Ctx::new(PathBuf::from(r"T:\sandbox\root"), &env);
        for (op, resolved) in [
            (".", r"T:\sandbox\root"),
            ("./", r"T:\sandbox\root\."),
            ("././", r"T:\sandbox\root\.\."),
            // mixed ASCII case + trailing separators still match
            (".", r"t:\SANDBOX\ROOT\."),
            ("/", r"T:\sandbox\root"),
        ] {
            assert!(guard(&ctx, op, resolved), "namespace root {op:?}");
        }
        // below the root nothing is protected
        for (op, resolved) in [
            ("sub", r"T:\sandbox\root\sub"),
            ("f", r"T:\sandbox\root\f"),
            (r"\\srv\share\dir", r"\\srv\share\dir"),
        ] {
            assert!(!guard(&ctx, op, resolved), "below root {op:?}");
        }
        // cwd one level below: '..' and 'sub\..' fold back onto the root
        let ctx_sub = Ctx::new(PathBuf::from(r"T:\sandbox\root\sub"), &env);
        assert!(guard(&ctx_sub, "..", r"T:\sandbox\root\sub\.."));
        assert!(guard(&ctx_sub, "sub\\..", r"T:\sandbox\root\sub\.."));
        // but '.' from below the root is just a subdirectory
        assert!(!guard(&ctx_sub, ".", r"T:\sandbox\root\sub"));
        // a different injected root changes the verdict — no process state
        let mut env2 = MapEnv::new();
        env2.set("__RUBASH_SHELL_ROOT", r"X:\elsewhere");
        let ctx2 = Ctx::new(PathBuf::from(r"T:\sandbox\root"), &env2);
        assert!(!guard(&ctx2, ".", r"T:\sandbox\root"));
        // UNC-configured namespace root is not a volume root itself
        let mut env3 = MapEnv::new();
        env3.set("__RUBASH_SHELL_ROOT", r"\\srv\share\payload");
        let ctx3 = Ctx::new(PathBuf::from(r"\\srv\share\payload"), &env3);
        assert!(guard(&ctx3, "/", r"\\srv\share\payload"));
        assert!(guard(&ctx3, ".", r"\\srv\share\payload"));
        assert!(!guard(&ctx3, "d", r"\\srv\share\payload\d"));
    }
}
