//! Shell-namespace path resolution for WinuxCmd 2.0 (#1488, wave3b).
//!
//! This is the Rust port of the 1.x C++ operand boundary
//! `src/utils/native_path.cppm` (bash-side contract niubash#346 "coreutils
//! own the shell tree"): rubash/niubash hands its own applets POSIX
//! spellings verbatim, so the applets resolve `/`, `/tmp`, `/var/tmp`,
//! `/home`, `/mnt/X` and the MSYS `/X` drive form themselves. A foreign
//! process never sees these spellings, so this is the shell tree's dialect,
//! not a general POSIX emulation layer. It is inert unless a shell root is
//! configured, so standalone WinuxCmd keeps its native-dialect behaviour.
//!
//! Every function cites its 1.x owner (`native_path.cppm:LINE`); the DOS
//! device tables are moved verbatim (24 reserved names, COM1-9/LPT1-9).
//! Environment access goes through the injected [`EnvSource`] (the C++
//! `GetEnvironmentVariableW` tier is case-insensitive, first match wins —
//! [`crate::win32_env_get`]), never the process environment: the winuxcmd-ffi
//! ABI snapshot and the CLI driver both feed a `Ctx`, so the layer behaves
//! identically in-process and spawned.
//!
//! The one boundary commands route through is [`normalize_operand`] (the
//! `normalize_api_operand_w` fold sequence): pseudo-devices resolve to their
//! Windows device names, the `/cygdrive/X` (#1145) and `/mnt/X` (#1172)
//! prefixes fold onto the MSYS `/X` spelling, `/X` folds onto `X:\`
//! (#1231), literal final-component colons fold onto the U+F03A carrier
//! (#1283), and the remaining `/`-prefixed spellings map through the shell
//! tree with the `.exe` leaf retry of `shell_tree_to_native_w`.

use std::path::Path;

use crate::{win32_env_get, EnvSource};

// ---------------------------------------------------------------------------
// Lexical primitives (portable; no FFI)
// ---------------------------------------------------------------------------

/// Both separators name the same thing in the operand dialect
/// (native_path.cppm:46 is_separator).
pub fn is_separator(c: u8) -> bool {
    c == b'\\' || c == b'/'
}

/// native_path.cppm:137 is_drive_letter.
pub fn is_drive_letter(c: u8) -> bool {
    c.is_ascii_alphabetic()
}

/// Length of the Win32 root of a backslash-normalized path: `X:\`, the UNC
/// share `\\server\share`, or the bare `\\server\` form. None when relative.
fn win32_root_length(native: &str) -> Option<usize> {
    let b = native.as_bytes();
    if b.len() >= 2 && is_separator(b[0]) && is_separator(b[1]) {
        // UNC: root runs through the share name (unc_root_length semantics).
        let unc = unc_root_length(native)?;
        return Some(unc);
    }
    if b.len() >= 3 && is_drive_letter(b[0]) && b[1] == b':' && is_separator(b[2]) {
        return Some(3);
    }
    None
}

/// native_path.cppm:52 unc_root_length: for `\\server\share[...]` the index
/// of the separator ending the share (or the length when the path ends
/// there). None unless the operand starts with two separators.
pub fn unc_root_length(path: &str) -> Option<usize> {
    let b = path.as_bytes();
    if b.len() < 3 || !is_separator(b[0]) || !is_separator(b[1]) {
        return None;
    }
    let server_end = match path[2..].find(|c| c == '/' || c == '\\') {
        Some(i) => i + 2,
        None => return Some(b.len()),
    };
    let share_start = server_end + 1;
    if share_start >= b.len() {
        return Some(b.len());
    }
    match path[share_start..].find(|c| c == '/' || c == '\\') {
        Some(i) => Some(share_start + i),
        None => Some(b.len()),
    }
}

/// native_path.cppm:80 strip_trailing_separators: a drive root (`X:\`) and
/// an UNC root keep their trailing separator.
pub fn strip_trailing_separators(path: &str) -> &str {
    let mut end = path.len();
    while end > 1 && is_separator(path.as_bytes()[end - 1]) {
        let prefix = &path[..end];
        if prefix.len() == 3 && path.as_bytes()[1] == b':' {
            break;
        }
        if let Some(root_len) = unc_root_length(prefix) {
            if prefix.len() <= root_len + 1 {
                break;
            }
        }
        // step back over one full UTF-8 code point (separators are ASCII)
        let mut back = 1;
        while end - back > 0 && path.as_bytes()[end - back] & 0xC0 == 0x80 {
            back += 1;
        }
        end -= back;
    }
    &path[..end]
}

/// The reserved DOS device names (native_path.cppm:858 kDosDevices — the
/// same 24-entry table backs is_dos_device_name_w and
/// has_device_stem_with_extension_w's stem list).
pub const DOS_DEVICES: [&str; 24] = [
    "NUL", "CON", "PRN", "AUX", "CONIN$", "CONOUT$", "COM1", "COM2", "COM3", "COM4", "COM5",
    "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8",
    "LPT9",
];

/// CI table lookup shared by the whole device family.
fn dos_device_table_match(name: &str) -> bool {
    let upper = name.to_ascii_uppercase();
    DOS_DEVICES.iter().any(|d| *d == upper)
}

/// native_path.cppm:893 is_dos_device_name_w: a device name only resolves
/// when it is the whole operand ("dir\\NUL" names an ordinary file, and so
/// does "\\\\?\\C:\\dir\\NUL"); an explicit device namespace
/// ("\\\\.\\NUL") is already a device spelling.
pub fn is_dos_device_name(path: &str) -> bool {
    let stripped = strip_trailing_separators(path);
    if stripped.len() > 4 && stripped.starts_with(r"\\.\") {
        return true;
    }
    if stripped.contains('/') || stripped.contains('\\') {
        return false;
    }
    let upper = stripped.to_ascii_uppercase();
    if matches!(
        upper.as_str(),
        "NUL" | "CON" | "PRN" | "AUX" | "CONIN$" | "CONOUT$"
    ) {
        return true;
    }
    upper.len() == 4
        && (upper.starts_with("COM") || upper.starts_with("LPT"))
        && (b'1'..=b'9').contains(&upper.as_bytes()[3])
}

/// native_path.cppm:814 pseudo_device_std_fd_w: the standard-stream pseudo
/// paths (/dev/std{in,out,err}, /dev/fd/N, /proc/{self,pid}/fd/N) name CRT
/// descriptors 0/1/2.
pub fn pseudo_device_std_fd(path: &str) -> Option<u32> {
    match path {
        "/dev/stdin" => return Some(0),
        "/dev/stdout" => return Some(1),
        "/dev/stderr" => return Some(2),
        _ => {}
    }
    let fd_of_tail = |tail: &str| -> Option<u32> {
        let b = tail.as_bytes();
        if b.len() == 1 && (b'0'..=b'2').contains(&b[0]) {
            Some((b[0] - b'0') as u32)
        } else {
            None
        }
    };
    if let Some(tail) = path.strip_prefix("/dev/fd/") {
        return fd_of_tail(tail);
    }
    // /proc/self/fd/N and the MSYS-resolved /proc/<pid>/fd/N form.
    if let Some(rest) = path.strip_prefix("/proc/") {
        if let Some(fd_pos) = rest.find("/fd/") {
            let pid = &rest[..fd_pos];
            let valid_pid =
                !pid.is_empty() && (pid == "self" || pid.bytes().all(|c| c.is_ascii_digit()));
            if valid_pid {
                return fd_of_tail(&rest[fd_pos + 4..]);
            }
        }
    }
    None
}

/// native_path.cppm:847 resolve_pseudo_device_w: POSIX pseudo-device
/// spellings fold onto their Windows devices (/dev/null -> NUL,
/// /dev/tty -> CONIN$, the std streams onto CONIN$/CONOUT$) and bare
/// reserved names are recognized case-insensitively (MSYS rewrites
/// /dev/null to "nul" in native argv). The Windows device name must pass
/// through WITHOUT the \\?\ prefix — the extended namespace bypasses
/// Win32 device-name resolution (native_path.cppm:935 make_api_path_operand_w).
pub fn resolve_pseudo_device(path: &str) -> Option<&'static str> {
    match path {
        "/dev/null" => return Some("NUL"),
        "/dev/tty" => return Some("CONIN$"),
        _ => {}
    }
    if let Some(fd) = pseudo_device_std_fd(path) {
        return Some(if fd == 0 { "CONIN$" } else { "CONOUT$" });
    }
    if !path.contains('/') && !path.contains('\\') && dos_device_table_match(path) {
        // verbatim: the operand already spells the device name
        return DOS_DEVICES
            .iter()
            .find(|d| path.eq_ignore_ascii_case(d))
            .copied();
    }
    None
}

/// native_path.cppm:924 is_character_device_w: whether the operand denotes
/// a Windows character device, directly or through a POSIX pseudo-device.
/// GNU reports /dev/null as a character special file (`test -c /dev/null`
/// is true, `test -b /dev/null` false).
pub fn is_character_device(path: &str) -> bool {
    match resolve_pseudo_device(path) {
        Some(dev) => is_dos_device_name(dev),
        None => is_dos_device_name(path),
    }
}

/// native_path.cppm:720 has_device_stem_with_extension_w: a device-stem
/// leaf carrying an extension ("NUL.txt") denotes an on-disk file, but
/// GetFullPathNameW resolves the stem onto the device namespace (#1333),
/// so an extended form built from it names no file. Bare device names
/// never reach this predicate, and stems that merely CONTAIN a device
/// name ("ANUL.txt") are ordinary files.
pub fn has_device_stem_with_extension(path: &str) -> bool {
    let sep = path.rfind(|c| c == '\\' || c == '/');
    let leaf = match sep {
        Some(i) => &path[i + 1..],
        None => path,
    };
    let dot = match leaf.find('.') {
        Some(0) | None => return false,
        Some(d) => d,
    };
    let stem = &leaf[..dot];
    DOS_DEVICES.iter().any(|d| stem.eq_ignore_ascii_case(d))
}

/// native_path.cppm:383 fold_posix_drive_prefix_w: the Cygwin "/cygdrive/X"
/// (#1145, case-insensitive name) and WSL interop "/mnt/X" (#1172,
/// case-SENSITIVE "mnt" — the spelling WSL itself emits, and the exact
/// shape rubash's windows_mnt_drive_argument accepts, rubash#444/rubash#592)
/// prefixes fold onto the MSYS single-letter spelling they name. Bare /mnt
/// and non-drive children ("/mnt/data", "/mntcc") never fold.
pub fn fold_posix_drive_prefix(normalized: &str) -> Option<String> {
    // /cygdrive/<d>[sep] — name comparison case-insensitive
    {
        let name = b"cygdrive";
        let b = normalized.as_bytes();
        if b.len() >= name.len() + 3 && is_separator(b[0]) {
            let matches_name = b[1..1 + name.len()]
                .iter()
                .zip(name)
                .all(|(x, y)| x.to_ascii_lowercase() == *y);
            let drive_pos = name.len() + 2;
            if matches_name
                && is_separator(b[1 + name.len()])
                && is_drive_letter(b[drive_pos])
                && (b.len() == drive_pos + 1 || is_separator(b[drive_pos + 1]))
            {
                return Some(format!("/{}", &normalized[drive_pos..]));
            }
        }
    }
    // /mnt/<d>[sep] — literal lowercase "mnt", byte-for-byte
    {
        let name = b"mnt";
        let b = normalized.as_bytes();
        if b.len() >= name.len() + 3 && is_separator(b[0]) {
            let matches_name = &b[1..1 + name.len()] == name;
            let drive_pos = name.len() + 2;
            if matches_name
                && is_separator(b[1 + name.len()])
                && is_drive_letter(b[drive_pos])
                && (b.len() == drive_pos + 1 || is_separator(b[drive_pos + 1]))
            {
                return Some(format!("/{}", &normalized[drive_pos..]));
            }
        }
    }
    None
}

/// native_path.cppm:154 is_drive_form: the MSYS single-character drive form
/// "/X" or "/X/...". No shell mount name collides (they are all
/// multi-letter).
pub fn is_drive_form(normalized: &str) -> bool {
    let b = normalized.as_bytes();
    b.len() >= 2
        && is_separator(b[0])
        && is_drive_letter(b[1])
        && (b.len() == 2 || is_separator(b[2]))
}

/// native_path.cppm:450 fold_drive_dialect_w: "/e" and "/e/Data" name
/// "E:\" and "E:\Data" (#1231). The first segment must be exactly one
/// drive letter ("usr", "cygdrive", "mntcc" are never drives; the prefix
/// forms fold into this spelling first).
pub fn fold_drive_dialect(normalized: &str) -> Option<String> {
    if !is_drive_form(normalized) {
        return None;
    }
    let b = normalized.as_bytes();
    let drive = b[1].to_ascii_uppercase() as char;
    let mut folded = String::new();
    folded.push(drive);
    folded.push(':');
    folded.push('\\');
    if b.len() > 3 {
        folded.push_str(&normalized[3..]);
    }
    Some(folded.replace('/', "\\"))
}

/// native_path.cppm:484 fold_carrier_colons_w: literal colons in the
/// operand's final component fold onto the private-use carrier U+F03A
/// (#1283) — Win32 would read them as an ADS separator, while the shell's
/// path layer stores such names on disk encoded. The bare drive ("D:")
/// keeps its drive meaning, and verbatim/device spellings ("\\\\?\\",
/// "\\\\.\\") pass through untouched.
pub fn fold_carrier_colons(normalized: &str) -> Option<String> {
    if normalized.starts_with(r"\\?\") || normalized.starts_with(r"\\.\") {
        return None;
    }
    let sep = normalized.rfind(|c| c == '\\' || c == '/');
    let tail = match sep {
        Some(i) => &normalized[i + 1..],
        None => normalized,
    };
    if !tail.contains(':') {
        return None;
    }
    if sep.is_none() && normalized.chars().count() == 2 {
        return None; // bare "D:" — the drive itself, not a file name
    }
    let tail_begin = sep.map(|i| i + 1).unwrap_or(0);
    let mut folded = String::with_capacity(normalized.len());
    for (i, ch) in normalized.char_indices() {
        if i >= tail_begin && ch == ':' {
            folded.push('\u{F03A}');
        } else {
            folded.push(ch);
        }
    }
    Some(folded)
}

// ---------------------------------------------------------------------------
// Shell namespace (native_path.cppm:125 shell_ns + :246 shell_tree_map_w)
// ---------------------------------------------------------------------------

/// The configured install root (native_path.cppm:141 root()): the shell
/// names its tree through `__RUBASH_SHELL_ROOT` and the neutral/compat
/// aliases, first non-empty wins.
pub fn shell_root(env: &dyn EnvSource) -> Option<String> {
    for name in [
        "__RUBASH_SHELL_ROOT",
        "NIU_ROOT",
        "WINUXSH_ROOT",
        "RUBASH_ROOT",
    ] {
        if let Some(v) = win32_env_get(env, name) {
            let v = v.to_string_lossy().into_owned();
            if !v.is_empty() {
                return Some(v);
            }
        }
    }
    None
}

/// native_path.cppm:164 nativeize_base_w: fold an environment-provided base
/// ("C:/x", "/c/x") into the backslash native form join_tail expects.
pub fn nativeize_base(base: &str) -> String {
    let mut value = base.replace('/', "\\");
    let b = value.as_bytes();
    if b.len() >= 2 && b[0] == b'\\' && is_drive_letter(b[1]) && (b.len() == 2 || b[2] == b'\\') {
        let drive = b[1].to_ascii_uppercase() as char;
        let rest = value[2..].to_string();
        value = format!("{drive}:{rest}");
    }
    value
}

/// Lexical parent of a native path (the std::filesystem::path::parent_path
/// the 1.x home_parent_w used): "C:\\a\\b" -> "C:\\a", "C:\\a" -> "C:\\",
/// "C:\\" -> itself, a single component has no parent.
fn native_parent(path: &str) -> Option<String> {
    let s = path.replace('/', "\\");
    if let Some(root_len) = win32_root_length(&s) {
        if root_len >= s.len() {
            return Some(s); // a bare root is its own parent
        }
        let tail = &s[root_len..];
        return match tail.rfind('\\') {
            Some(i) => Some(s[..root_len + i].to_string()),
            None => Some(s[..root_len].to_string()),
        };
    }
    match s.rfind('\\') {
        Some(0) => Some("\\".to_string()),
        Some(i) => Some(s[..i].to_string()),
        None => None, // a bare name has no parent
    }
}

/// native_path.cppm:176 tmp_base_w: /tmp resolves through TMPDIR, TEMP or
/// TMP (the per-user temporary namespace, niubash#94 — never a fixed
/// location under a possibly non-writable install root).
pub fn tmp_base(env: &dyn EnvSource) -> Option<String> {
    for name in ["TMPDIR", "TEMP", "TMP"] {
        if let Some(v) = win32_env_get(env, name) {
            let v = v.to_string_lossy().into_owned();
            if !v.is_empty() {
                return Some(nativeize_base(&v));
            }
        }
    }
    None
}

/// native_path.cppm:184 home_parent_w: /home is the parent of the user's
/// home directory (HOME, USERPROFILE).
pub fn home_parent(env: &dyn EnvSource) -> Option<String> {
    for name in ["HOME", "USERPROFILE"] {
        if let Some(v) = win32_env_get(env, name) {
            let v = v.to_string_lossy().into_owned();
            if v.is_empty() {
                continue;
            }
            if let Some(parent) = native_parent(&nativeize_base(&v)) {
                if !parent.is_empty() {
                    return Some(parent);
                }
            }
        }
    }
    None
}

/// native_path.cppm:203 join_tail: append a shell tail (the part after the
/// mount name) to a native base, folding "." and ".." lexically like
/// rubash's map_logical_path. The tail's components fold against an
/// initially EMPTY stack — a ".." that reaches the mount base is a no-op
/// (the POSIX root clamp, #1432), never a pop of a base component; seeding
/// the stack with the base let "/../x" escape the payload root.
pub fn join_tail(tail: &str, base: String) -> String {
    let mut components: Vec<&str> = Vec::new();
    for component in tail.split('/') {
        match component {
            ".." => {
                components.pop();
            }
            "" | "." => {}
            other => components.push(other),
        }
    }
    let mut joined = base;
    for component in components {
        if !joined.ends_with('\\') {
            joined.push('\\');
        }
        joined.push_str(component);
    }
    joined
}

/// native_path.cppm:227 has_prefix_ci.
fn has_prefix_ci(value: &str, prefix: &str) -> bool {
    value.len() >= prefix.len() && value[..prefix.len()].eq_ignore_ascii_case(prefix)
}

/// native_path.cppm:246 shell_tree_map_w: map a shell-namespace spelling to
/// a native path, or None when the operand is not part of the shell
/// namespace (a native/relative path, a double-slash spelling, /dev/*, a
/// drive form handled by the caller, or an unconfigured root).
pub fn shell_tree_map(env: &dyn EnvSource, normalized: &str) -> Option<String> {
    let root = shell_root(env)?;
    if normalized.is_empty() || !normalized.starts_with('/') {
        return None;
    }
    if normalized.len() >= 2 && is_separator(normalized.as_bytes()[1]) {
        return None;
    }
    if is_drive_form(normalized) {
        return None;
    }
    // /dev is a capability namespace resolved by the pseudo-device table;
    // its remaining entries must not silently become ordinary files.
    if normalized == "/dev" || normalized.starts_with("/dev/") {
        return None;
    }

    if normalized == "/tmp" || normalized.starts_with("/tmp/") {
        let base = tmp_base(env)?;
        return Some(join_tail(&normalized[4..], base));
    }
    if normalized == "/var/tmp" || normalized.starts_with("/var/tmp/") {
        let base = tmp_base(env)?;
        let base = join_tail("var/tmp", base);
        return Some(join_tail(&normalized[8..], base));
    }
    if normalized == "/home" || normalized.starts_with("/home/") {
        let base = home_parent(env)?;
        return Some(join_tail(&normalized[5..], base));
    }
    let b = normalized.as_bytes();
    if normalized.len() >= 6
        && normalized.starts_with("/mnt/")
        && is_drive_letter(b[5])
        && (normalized.len() == 6 || is_separator(b[6]))
    {
        let drive = b[5].to_ascii_uppercase() as char;
        let base = format!("{drive}:\\");
        let rest = if normalized.len() > 6 {
            &normalized[7..]
        } else {
            ""
        };
        return Some(join_tail(rest, base));
    }

    // Everything else (including the bare "/") is relative to the install
    // root.
    Some(join_tail(&normalized[1..], nativeize_base(&root)))
}

/// native_path.cppm:300 shell_tree_to_native_w: resolve a shell-namespace
/// operand through the shell tree, applying the MSYS open order: an
/// existing as-spelled path wins; otherwise the toolset's `X.exe` leaf
/// resolves the bare `/usr/bin/X` spelling (`ls /usr/bin/seq`,
/// `test -f /usr/bin/seq` when only `seq.exe` is on disk).
pub fn shell_tree_to_native(env: &dyn EnvSource, normalized: &str) -> Option<String> {
    let mut mapped = shell_tree_map(env, normalized)?;
    if !mapped.is_empty() && !mapped.ends_with('\\') && !imp::attributes(&mapped).is_some() {
        let exe = format!("{mapped}.exe");
        if imp::attributes(&exe).is_some() {
            mapped = exe;
        }
    }
    Some(mapped)
}

/// native_path.cppm:320 shell_tree_display_w: re-express a native path back
/// in the shell namespace — the canonical spelling of a path under a shell
/// mount is its POSIX mount name. None when no mount names the path.
/// `include_home=false` declines the /home alias: mount displays that must
/// mirror on-disk layout (cygpath -u) never re-spell the user profile as
/// /home.
pub fn shell_tree_display(env: &dyn EnvSource, native: &str, include_home: bool) -> Option<String> {
    let value = native.replace('/', "\\");

    let under = |raw_base: &str, mount: &str| -> Option<String> {
        let base = nativeize_base(raw_base);
        if base.is_empty() || !has_prefix_ci(&value, &base) {
            return None;
        }
        if value.len() > base.len() && !is_separator(value.as_bytes()[base.len()]) {
            return None;
        }
        let mut result = mount.to_string();
        for ch in value[base.len()..].chars() {
            result.push(if ch == '\\' { '/' } else { ch });
        }
        Some(result)
    };

    if let Some(root) = shell_root(env) {
        if let Some(mapped) = under(&root, "") {
            return Some(if mapped.is_empty() {
                "/".to_string()
            } else {
                mapped
            });
        }
    }
    if let Some(tmp) = tmp_base(env) {
        let var_tmp = join_tail("var/tmp", tmp.clone());
        if let Some(mapped) = under(&var_tmp, "/var/tmp") {
            return Some(mapped);
        }
        if let Some(mapped) = under(&tmp, "/tmp") {
            return Some(mapped);
        }
    }
    if !include_home {
        return None;
    }
    under(&home_parent(env)?, "/home")
}

// ---------------------------------------------------------------------------
// The unified operand boundary (native_path.cppm:508 normalize_api_operand_w,
// :1371 canonicalize_path_w's fold chain)
// ---------------------------------------------------------------------------

/// The `normalize_api_operand_w` fold sequence (native_path.cppm:508):
/// pseudo-devices first (verbatim device names), then the prefix fold, the
/// drive dialect, the carrier-colon fold, and finally the shell-tree
/// mapping for the remaining "/"-prefixed spellings. The trailing-
/// separator rule (#1052) needs attribute probes and runs on Windows only:
/// an operand spelled with a trailing separator that does not name a
/// directory keeps the separator so the OS rejects the open/create instead
/// of silently operating on the regular file.
///
/// Relative and already-native operands pass through unchanged (identity),
/// so callers can detect a claimed spelling by comparing with the input.
pub fn normalize_operand(env: &dyn EnvSource, cwd: &Path, path: &str) -> String {
    if let Some(dev) = resolve_pseudo_device(path) {
        return dev.to_string();
    }
    let had_trailing = strip_trailing_separators(path).len() != path.len();
    let mut normalized = strip_trailing_separators(path).to_string();
    if !normalized.is_empty() && !normalized.contains(|c| c != '/') {
        normalized = "/".to_string();
    }
    if let Some(prefix) = fold_posix_drive_prefix(&normalized) {
        normalized = prefix;
    }
    if let Some(folded) = fold_drive_dialect(&normalized) {
        normalized = folded;
    }
    if let Some(carrier) = fold_carrier_colons(&normalized) {
        normalized = carrier;
    }
    // niubash#346: after the folds above, any remaining "/"-prefixed
    // operand is a shell-namespace spelling; resolve it through the shell
    // tree exactly like rubash's shell_path_to_windows.
    if !normalized.is_empty() && is_separator(normalized.as_bytes()[0]) {
        if let Some(mapped) = shell_tree_to_native(env, &normalized) {
            normalized = mapped;
        }
    }
    if had_trailing && imp::trailing_not_directory(cwd, &normalized) {
        normalized.push('\\');
    }
    normalized
}

/// The canonicalize_path_w resolver fold chain (native_path.cppm:1371):
/// strip trailing separators, then the prefix fold, the drive dialect, the
/// carrier-colon fold, or the shell-tree mapping — a folded operand is
/// native-dialect input. This is the entry realpath/readlink/stat -L use
/// (1.x canonicalize_path_w consumed the same folds in the same order).
/// Unlike [`normalize_operand`] it does NOT resolve pseudo-devices: the
/// C++ resolver leaves /dev/* to the shell-tree decline so realpath
/// /dev/null stays spelled "/dev/null" instead of naming the Windows
/// device.
pub fn prepare_operand(env: &dyn EnvSource, operand: &str) -> String {
    let stripped = strip_trailing_separators(operand).to_string();
    let prefix_folded = fold_posix_drive_prefix(&stripped).unwrap_or_else(|| stripped.clone());
    if let Some(folded) = fold_drive_dialect(&prefix_folded) {
        return folded;
    }
    if let Some(carrier) = fold_carrier_colons(&prefix_folded) {
        return carrier;
    }
    if let Some(mapped) = shell_tree_to_native(env, &prefix_folded) {
        return mapped;
    }
    operand.to_string()
}

// ---------------------------------------------------------------------------
// Windows attribute probes (the only FFI here; non-Windows builds stub out)
// ---------------------------------------------------------------------------

#[cfg(windows)]
pub mod imp {
    use super::*;
    use std::ffi::OsStr;
    use std::os::windows::ffi::OsStrExt;

    type DWORD = u32;
    type Handle = *mut std::ffi::c_void;
    const INVALID_HANDLE_VALUE: Handle = -1isize as Handle;
    const INVALID_FILE_ATTRIBUTES: DWORD = 0xFFFF_FFFF;
    const FILE_ATTRIBUTE_DIRECTORY: DWORD = 0x10;
    const GENERIC_READ: DWORD = 0x8000_0000;
    const FILE_SHARE_ALL: DWORD = 0x7;
    const OPEN_EXISTING: DWORD = 3;

    #[link(name = "kernel32")]
    extern "system" {
        fn GetFileAttributesW(name: *const u16) -> DWORD;
        fn CreateFileW(
            name: *const u16,
            access: DWORD,
            share: DWORD,
            security: Handle,
            disposition: DWORD,
            flags: DWORD,
            template: Handle,
        ) -> Handle;
        fn CloseHandle(h: Handle) -> i32;
    }

    fn wide(s: &str) -> Vec<u16> {
        OsStr::new(s)
            .encode_wide()
            .chain(std::iter::once(0))
            .collect()
    }

    /// CreateFileW open-probe for a DOS device name — the existence gate
    /// that replaces the CRT stat / GetFileAttributesExW path, both of
    /// which reject device names outright (1.x native_path
    /// file_attribute_data_w; moved here from winux-cmds::fs_win in
    /// wave3b so every crate shares one probe). Plain GENERIC_READ
    /// without FILE_READ_ATTRIBUTES: the null device answers a read open
    /// but refuses attribute-only access with ACCESS_DENIED (probed
    /// kernel32: CreateFileW("NUL", GENERIC_READ|0x800000) -> gle 5,
    /// GENERIC_READ -> a valid handle).
    pub fn probe_dos_device(name: &str) -> bool {
        let w = wide(name);
        let handle = unsafe {
            CreateFileW(
                w.as_ptr(),
                GENERIC_READ,
                FILE_SHARE_ALL,
                std::ptr::null_mut(),
                OPEN_EXISTING,
                0,
                std::ptr::null_mut(),
            )
        };
        if handle == INVALID_HANDLE_VALUE || handle.is_null() {
            return false;
        }
        unsafe { CloseHandle(handle) };
        true
    }

    /// native_path.cppm:989 attributes_w: probe a path's file attributes.
    /// Device names resolve only in the Win32 device namespace, so they are
    /// probed verbatim; ordinary paths go through the verbatim spelling
    /// (the mapped forms this module produces are already absolute native
    /// paths; the \\?\ extended form matters only beyond MAX_PATH, which
    /// the extended-path boundary in winux-cmds::fs_win owns).
    pub fn attributes(path: &str) -> Option<DWORD> {
        let probe = match resolve_pseudo_device(path) {
            Some(dev) => dev.to_string(),
            None => path.to_string(),
        };
        let probe = strip_trailing_separators(&probe);
        let attrs = unsafe { GetFileAttributesW(wide(probe).as_ptr()) };
        if attrs == INVALID_FILE_ATTRIBUTES {
            None
        } else {
            Some(attrs)
        }
    }

    /// native_path.cppm:1165 attributes_are_directory.
    pub fn attributes_are_directory(attrs: Option<DWORD>) -> bool {
        matches!(attrs, Some(a) if a & FILE_ATTRIBUTE_DIRECTORY != 0)
    }

    /// The trailing-separator rule of normalize_api_operand_w
    /// (native_path.cppm:549, #1052): true when the operand keeps its
    /// separator because the stripped target exists and is NOT a
    /// directory. Missing targets keep the separator too (ENOENT) —
    /// resolve_pseudo_device spellings never carry the rule (a device is
    /// never spelled "NUL/"). Relative probes anchor against `cwd` (the
    /// injected working directory, never the process cwd).
    pub fn trailing_not_directory(cwd: &Path, normalized: &str) -> bool {
        if is_dos_device_name(normalized) {
            return false;
        }
        // anchor a relative spelling against the caller's cwd, and a
        // rooted-on-current-drive spelling ("\x" or "/x" without a shell
        // root) against the cwd's drive like GetFullPathNameW would
        let probe = if win32_root_length(normalized).is_none() {
            let cwd_text = cwd.to_string_lossy().into_owned();
            if normalized.starts_with('/') || normalized.starts_with('\\') {
                match win32_root_length(&cwd_text) {
                    Some(i) => format!("{}{}", &cwd_text[..i], normalized),
                    None => normalized.to_string(),
                }
            } else {
                format!("{}\\{}", cwd_text.trim_end_matches(['\\', '/']), normalized)
            }
        } else {
            normalized.to_string()
        };
        !attributes_are_directory(attributes(&probe))
    }
}

#[cfg(not(windows))]
pub mod imp {
    use super::*;

    /// Non-Windows builds have no Win32 attribute probe; the shell-namespace
    /// mapping itself stays fully functional (all lexical), only the .exe
    /// retry and the trailing-separator rule degrade (no probe -> no retry,
    /// no separator kept).
    pub fn attributes(_path: &str) -> Option<u32> {
        None
    }

    pub fn attributes_are_directory(_attrs: Option<u32>) -> bool {
        false
    }

    pub fn trailing_not_directory(_cwd: &Path, _normalized: &str) -> bool {
        false
    }

    pub fn probe_dos_device(_name: &str) -> bool {
        false
    }
}

// ---------------------------------------------------------------------------
// Table-driven contract tests (ported case families from the C++ boundary)
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::MapEnv;

    fn env(vars: &[(&str, &str)]) -> MapEnv {
        let mut e = MapEnv::new();
        for (k, v) in vars {
            e.set(k, v);
        }
        e
    }

    // ---- root priority (native_path.cppm:141) ----

    #[test]
    fn root_priority_chain() {
        // first non-empty of __RUBASH_SHELL_ROOT > NIU_ROOT > WINUXSH_ROOT
        let e = env(&[
            ("__RUBASH_SHELL_ROOT", r"D:\one"),
            ("NIU_ROOT", r"D:\two"),
            ("WINUXSH_ROOT", r"D:\three"),
            ("RUBASH_ROOT", r"D:\four"),
        ]);
        assert_eq!(shell_root(&e).as_deref(), Some(r"D:\one"));

        let e = env(&[("NIU_ROOT", r"D:\two"), ("WINUXSH_ROOT", r"D:\three")]);
        assert_eq!(shell_root(&e).as_deref(), Some(r"D:\two"));

        let e = env(&[("WINUXSH_ROOT", "D:/three"), ("RUBASH_ROOT", r"D:\four")]);
        assert_eq!(shell_root(&e).as_deref(), Some("D:/three"));

        assert_eq!(shell_root(&env(&[])), None);
    }

    // ---- shell_tree_map: the bare "/" and root-relative tails ----

    #[test]
    fn bare_slash_maps_to_root() {
        let e = env(&[("WINUXSH_ROOT", "D:/shellroot")]);
        assert_eq!(shell_tree_map(&e, "/").as_deref(), Some(r"D:\shellroot"));
        assert_eq!(
            shell_tree_map(&e, "/etc/passwd").as_deref(),
            Some(r"D:\shellroot\etc\passwd")
        );
        assert_eq!(
            shell_tree_map(&e, "/usr/bin/seq").as_deref(),
            Some(r"D:\shellroot\usr\bin\seq")
        );
        // "." and ".." fold lexically against the mount base (#1432): a
        // ".." that reaches the mount base is a no-op, never a pop of a
        // base component ("/no-such-dir/../zzz" stays under the root).
        assert_eq!(
            shell_tree_map(&e, "/no-such-dir/../zzz-target").as_deref(),
            Some(r"D:\shellroot\zzz-target")
        );
        assert_eq!(
            shell_tree_map(&e, "/aaa/./b/c").as_deref(),
            Some(r"D:\shellroot\aaa\b\c")
        );
        assert_eq!(shell_tree_map(&e, "/..").as_deref(), Some(r"D:\shellroot"));
        assert_eq!(
            shell_tree_map(&e, "/../x").as_deref(),
            Some(r"D:\shellroot\x")
        );
    }

    // ---- /tmp, /var/tmp (native_path.cppm:257, :262) ----

    #[test]
    fn tmp_maps_through_tmpdir_temp_tmp() {
        let e = env(&[
            ("WINUXSH_ROOT", r"D:\shellroot"),
            ("TMPDIR", "C:/Users/u/AppData/Local/Temp"),
        ]);
        assert_eq!(
            shell_tree_map(&e, "/tmp").as_deref(),
            Some(r"C:\Users\u\AppData\Local\Temp")
        );
        assert_eq!(
            shell_tree_map(&e, "/tmp/probe/dir").as_deref(),
            Some(r"C:\Users\u\AppData\Local\Temp\probe\dir")
        );
        // /var/tmp lives beside the per-user temp base: <tmp>/var/tmp/<x>
        assert_eq!(
            shell_tree_map(&e, "/var/tmp").as_deref(),
            Some(r"C:\Users\u\AppData\Local\Temp\var\tmp")
        );
        assert_eq!(
            shell_tree_map(&e, "/var/tmp/x").as_deref(),
            Some(r"C:\Users\u\AppData\Local\Temp\var\tmp\x")
        );

        // TMPDIR unset: TEMP answers, then TMP
        let e = env(&[("WINUXSH_ROOT", r"D:\r"), ("TEMP", r"C:\t")]);
        assert_eq!(shell_tree_map(&e, "/tmp").as_deref(), Some(r"C:\t"));
        let e = env(&[("WINUXSH_ROOT", r"D:\r"), ("TMP", r"C:\k")]);
        assert_eq!(shell_tree_map(&e, "/tmp/x").as_deref(), Some(r"C:\k\x"));

        // no tmp base configured: /tmp is NOT part of the shell tree
        let e = env(&[("WINUXSH_ROOT", r"D:\r")]);
        assert_eq!(shell_tree_map(&e, "/tmp"), None);
        assert_eq!(shell_tree_map(&e, "/tmp/x"), None);
    }

    // ---- /home (native_path.cppm:269) ----

    #[test]
    fn home_maps_to_parent_of_user_home() {
        let e = env(&[
            ("WINUXSH_ROOT", r"D:\shellroot"),
            ("HOME", r"D:\shellroot\home\probeuser"),
        ]);
        assert_eq!(
            shell_tree_map(&e, "/home").as_deref(),
            Some(r"D:\shellroot\home")
        );
        assert_eq!(
            shell_tree_map(&e, "/home/u1").as_deref(),
            Some(r"D:\shellroot\home\u1")
        );
        // USERPROFILE answers when HOME is absent
        let e = env(&[("WINUXSH_ROOT", r"D:\r"), ("USERPROFILE", r"C:\Users\u")]);
        assert_eq!(
            shell_tree_map(&e, "/home/u2").as_deref(),
            Some(r"C:\Users\u2")
        );
        // no home known: unmapped
        let e = env(&[("WINUXSH_ROOT", r"D:\r")]);
        assert_eq!(shell_tree_map(&e, "/home/u3"), None);
    }

    // ---- /mnt/X and the drive dialect (native_path.cppm:274, :450) ----

    #[test]
    fn mnt_and_drive_forms() {
        let e = env(&[("WINUXSH_ROOT", r"D:\shellroot")]);
        assert_eq!(shell_tree_map(&e, "/mnt/d/x").as_deref(), Some(r"D:\x"));
        assert_eq!(shell_tree_map(&e, "/mnt/D").as_deref(), Some(r"D:\"));
        assert_eq!(
            shell_tree_map(&e, "/mnt/e/deep/path").as_deref(),
            Some(r"E:\deep\path")
        );
        // bare /mnt, non-drive children and near-misses stay in the tree
        assert_eq!(
            shell_tree_map(&e, "/mnt").as_deref(),
            Some(r"D:\shellroot\mnt")
        );
        assert_eq!(
            shell_tree_map(&e, "/mnt/data").as_deref(),
            Some(r"D:\shellroot\mnt\data")
        );
        assert_eq!(
            shell_tree_map(&e, "/mntcc").as_deref(),
            Some(r"D:\shellroot\mntcc")
        );
        assert_eq!(
            shell_tree_map(&e, "/mnt/123").as_deref(),
            Some(r"D:\shellroot\mnt\123")
        );
        // MSYS single-letter form declines the shell tree (caller folds it)
        assert_eq!(shell_tree_map(&e, "/d/x"), None);
        assert_eq!(shell_tree_map(&e, "/d"), None);
        // cygdrive prefix folds onto the same spelling
        assert_eq!(
            fold_posix_drive_prefix("/cygdrive/d/y").as_deref(),
            Some("/d/y")
        );
        assert_eq!(
            fold_drive_dialect(&fold_posix_drive_prefix("/cygdrive/d/y").unwrap()).as_deref(),
            Some(r"D:\y")
        );
        assert_eq!(fold_drive_dialect("/d/x").as_deref(), Some(r"D:\x"));
        assert_eq!(fold_drive_dialect("/d").as_deref(), Some(r"D:\"));
        assert_eq!(fold_drive_dialect("/usr/bin"), None);
        assert_eq!(fold_drive_dialect("/mntcc/x"), None);
    }

    // ---- declines (native_path.cppm:248-255) ----

    #[test]
    fn shell_tree_declines() {
        let e = env(&[("WINUXSH_ROOT", r"D:\shellroot")]);
        // unconfigured root
        assert_eq!(shell_tree_map(&env(&[]), "/etc"), None);
        // relative / empty
        assert_eq!(shell_tree_map(&e, "rel/path"), None);
        assert_eq!(shell_tree_map(&e, ""), None);
        // double slash
        assert_eq!(shell_tree_map(&e, "//server/share"), None);
        // drive form
        assert_eq!(shell_tree_map(&e, "/d"), None);
        // /dev is the capability namespace
        assert_eq!(shell_tree_map(&e, "/dev"), None);
        assert_eq!(shell_tree_map(&e, "/dev/null"), None);
        assert_eq!(shell_tree_map(&e, "/dev/anything"), None);
    }

    // ---- DOS device tables (native_path.cppm:858, :893, :720) ----

    #[test]
    fn dos_device_tables() {
        // the whole 24-name table, case-insensitively
        for name in DOS_DEVICES {
            assert!(is_dos_device_name(name), "{name}");
            assert!(is_dos_device_name(&name.to_ascii_lowercase()), "{name}");
            assert_eq!(
                resolve_pseudo_device(&name.to_ascii_lowercase()),
                Some(name),
                "{name}"
            );
        }
        // POSIX pseudo-device spellings
        assert_eq!(resolve_pseudo_device("/dev/null"), Some("NUL"));
        assert_eq!(resolve_pseudo_device("/dev/tty"), Some("CONIN$"));
        assert_eq!(resolve_pseudo_device("/dev/stdin"), Some("CONIN$"));
        assert_eq!(resolve_pseudo_device("/dev/stdout"), Some("CONOUT$"));
        assert_eq!(resolve_pseudo_device("/dev/stderr"), Some("CONOUT$"));
        assert_eq!(resolve_pseudo_device("/dev/fd/1"), Some("CONOUT$"));
        assert_eq!(resolve_pseudo_device("/proc/self/fd/0"), Some("CONIN$"));
        assert_eq!(resolve_pseudo_device("/proc/123/fd/2"), Some("CONOUT$"));
        // not devices
        assert!(!is_dos_device_name("NUL.txt"));
        assert!(!is_dos_device_name("ANUL"));
        assert!(!is_dos_device_name("COM0"));
        assert!(!is_dos_device_name("LPTA"));
        assert!(!is_dos_device_name("dir/NUL"));
        assert!(!is_dos_device_name(r"dir\NUL"));
        assert!(!is_dos_device_name(""));
        assert_eq!(resolve_pseudo_device("COM10"), None);
        assert_eq!(resolve_pseudo_device("/dev/null.txt"), None);
        // a device under a directory is an ordinary on-disk entry, but the
        // device-stem-with-extension trap (#1333) still flags NUL.txt
        assert!(has_device_stem_with_extension("dir/NUL.txt"));
        assert!(has_device_stem_with_extension("nul.txt"));
        assert!(!has_device_stem_with_extension("dir/ANUL.txt"));
        assert!(!has_device_stem_with_extension("dir/.NUL"));
        assert!(!has_device_stem_with_extension("dir/NULnoext"));
        assert!(is_character_device("/dev/null"));
        assert!(is_character_device("nul"));
        assert!(!is_character_device("reg"));
    }

    // ---- carrier colons (#1283) ----

    #[test]
    fn carrier_colon_folds() {
        assert_eq!(
            fold_carrier_colons("D:name").as_deref(),
            Some("D\u{F03A}name")
        );
        assert_eq!(
            fold_carrier_colons(r"C:\dir\a:b:c"),
            Some(format!("C:\\dir\\a\u{F03A}b\u{F03A}c"))
        );
        assert_eq!(fold_carrier_colons("D:"), None);
        assert_eq!(fold_carrier_colons(r"C:\dir\nocolon"), None);
        assert_eq!(fold_carrier_colons(r"\\?\D:\a:b"), None);
        assert_eq!(fold_carrier_colons(r"\\.\NUL"), None);
    }

    // ---- UNC roots and trailing separators ----

    #[test]
    fn unc_and_trailing() {
        assert_eq!(unc_root_length(r"\\srv\share\f"), Some(11));
        assert_eq!(unc_root_length(r"\\srv\share"), Some(11));
        assert_eq!(unc_root_length(r"\\srv"), Some(5));
        assert_eq!(unc_root_length(r"C:\x"), None);
        assert_eq!(strip_trailing_separators("a/b///"), "a/b");
        assert_eq!(strip_trailing_separators(r"C:\"), r"C:\");
        assert_eq!(strip_trailing_separators("/"), "/");
        // the UNC share root keeps its separator: stripping stops when the
        // remainder is exactly the root (native_path.cppm:84 root_len+1)
        assert_eq!(strip_trailing_separators(r"\\srv\share\"), r"\\srv\share\");
        assert_eq!(
            strip_trailing_separators(r"\\srv\share\x\"),
            r"\\srv\share\x"
        );
        assert_eq!(strip_trailing_separators("x"), "x");
    }

    // ---- nativeize_base and the display direction ----

    #[test]
    fn nativeize_and_display() {
        assert_eq!(nativeize_base("C:/x/y"), r"C:\x\y");
        assert_eq!(nativeize_base("/c/x"), r"C:\x");
        assert_eq!(nativeize_base(r"D:\tmp"), r"D:\tmp");

        // HOME outside the root tree so the /home mount is distinguishable
        // from the root mount in the display direction.
        let e = env(&[
            ("WINUXSH_ROOT", r"D:\shellroot"),
            ("TMPDIR", r"C:\Users\u\AppData\Local\Temp"),
            ("HOME", r"C:\Users\probeuser"),
        ]);
        assert_eq!(
            shell_tree_display(&e, r"D:\shellroot", true).as_deref(),
            Some("/")
        );
        assert_eq!(
            shell_tree_display(&e, r"D:\shellroot\etc\passwd", true).as_deref(),
            Some("/etc/passwd")
        );
        assert_eq!(
            shell_tree_display(&e, r"C:\Users\u\AppData\Local\Temp\x", true).as_deref(),
            Some("/tmp/x")
        );
        assert_eq!(
            shell_tree_display(&e, r"C:\Users\u\AppData\Local\Temp\var\tmp\x", true).as_deref(),
            Some("/var/tmp/x")
        );
        assert_eq!(
            shell_tree_display(&e, r"C:\Users\u1", true).as_deref(),
            Some("/home/u1")
        );
        // native paths outside every mount stay native
        assert_eq!(shell_tree_display(&e, r"E:\elsewhere", true), None);
        // include_home=false declines the /home alias
        assert_eq!(shell_tree_display(&e, r"C:\Users\u1", false), None);
    }

    // ---- the unified boundary ----

    #[test]
    fn prepare_operand_chain() {
        let e = env(&[("WINUXSH_ROOT", r"D:\shellroot"), ("TMPDIR", r"C:\t")]);
        // devices stay literal (the canonicalize resolver leaves /dev/* to
        // the shell-tree decline; C++ canonicalize_path_w)
        assert_eq!(prepare_operand(&e, "/dev/null"), "/dev/null");
        assert_eq!(prepare_operand(&e, "nul"), "nul");
        // drive dialect
        assert_eq!(prepare_operand(&e, "/e/Data"), r"E:\Data");
        // cygdrive prefix
        assert_eq!(prepare_operand(&e, "/cygdrive/e/f"), r"E:\f");
        // shell tree
        assert_eq!(
            prepare_operand(&e, "/etc/passwd"),
            r"D:\shellroot\etc\passwd"
        );
        assert_eq!(prepare_operand(&e, "/tmp/x"), r"C:\t\x");
        assert_eq!(prepare_operand(&e, "/"), r"D:\shellroot");
        // relative/native identity
        assert_eq!(prepare_operand(&e, "rel/file"), "rel/file");
        assert_eq!(prepare_operand(&e, "a/b/"), "a/b/");
        assert_eq!(prepare_operand(&e, r"C:\native\path"), r"C:\native\path");
    }

    #[cfg(windows)]
    #[test]
    fn normalize_trailing_separator_rule() {
        // #1052: a trailing separator over an existing regular file keeps
        // the separator so the OS rejects the open (POSIX ENOTDIR); over a
        // directory it is stripped.
        let sc = crate::test_support::ScratchCtx::new("path-trailing");
        let dir = sc.dir.clone();
        std::fs::write(dir.join("reg"), "x").unwrap();
        std::fs::create_dir(dir.join("adir")).unwrap();
        let e = env(&[]);
        let cwd = dir.clone();
        let reg = dir.join("reg").to_string_lossy().into_owned();
        let adir = dir.join("adir").to_string_lossy().into_owned();
        assert_eq!(
            normalize_operand(&e, &cwd, &format!("{reg}/")),
            format!("{reg}\\")
        );
        assert_eq!(normalize_operand(&e, &cwd, &format!("{adir}/")), adir);
        assert_eq!(normalize_operand(&e, &cwd, &reg), reg);
    }

    #[cfg(windows)]
    #[test]
    fn shell_tree_exe_retry() {
        // native_path.cppm:300: a missing leaf retries with ".exe" —
        // `test -f /usr/bin/seq` when only `seq.exe` is on disk.
        let sc = crate::test_support::ScratchCtx::new("path-exe");
        let root = sc.dir.join("shellroot");
        let bin = root.join("usr").join("bin");
        std::fs::create_dir_all(&bin).unwrap();
        std::fs::write(bin.join("seq.exe"), "x").unwrap();
        std::fs::write(bin.join("awk"), "x").unwrap();
        let mut e = env(&[]);
        e.set("WINUXSH_ROOT", root.to_string_lossy().as_ref());
        assert_eq!(
            shell_tree_to_native(&e, "/usr/bin/seq").as_deref(),
            Some(bin.join("seq.exe").to_string_lossy().as_ref())
        );
        assert_eq!(
            shell_tree_to_native(&e, "/usr/bin/awk").as_deref(),
            Some(bin.join("awk").to_string_lossy().as_ref())
        );
        // a missing leaf with no .exe sibling keeps the shell-tree spelling
        assert_eq!(
            shell_tree_to_native(&e, "/usr/bin/nothing").as_deref(),
            Some(bin.join("nothing").to_string_lossy().as_ref())
        );
    }
}
