//! Shared child-process resolution for the spawner commands (nohup,
//! timeout, ...): a Rust port of the product's childproc helper
//! (src/utils/childproc.cppm), extracted from timeout.rs so every spawner
//! shares one resolver. See the per-item doc comments for the GNU execvp
//! semantics each branch preserves.

use winux_core::{win32_env_get, Ctx};

/// execvp-shaped child resolution mirroring the product's shared childproc
/// helper (src/utils/childproc.cppm), which every WinuxCmd child spawner
/// (env, timeout, xargs, ...) shares:
///
/// * PATH-only search over NORMALIZED entries -- the PATH value POSIX
///   shells hand down mixes '/c/...' drive spellings and ':' separators
///   into the ';'-separated Windows form, which a plain `split(';')`
///   cannot parse; a bare name never falls back to the application
///   directory, the current directory or System32 (where.exe et al.
///   collide), and an UNSET PATH falls back to the system directories
///   (execvp's _CS_PATH analog).
/// * PATHEXT completion for extensionless operands, with .cmd/.bat
///   candidates routed through the command interpreter (CreateProcess
///   cannot launch a batch file directly).
/// * an operand containing a separator is an explicit path, used as given
///   (PATHEXT completion only when it carries no extension); a trailing
///   separator over a directory is NotADirectory (126), not ENOENT.
#[derive(Debug, PartialEq)]
pub enum ChildResolve {
    Found { path: String, batch: bool },
    NotFound,
    NotExecutable,
    NotADirectory,
}

fn is_drive_letter(ch: char) -> bool {
    ch.is_ascii_alphabetic()
}

/// One POSIX '/c/...' (or '/cygdrive/c/...') spelling into 'C:\...' form;
/// non-POSIX entries unchanged apart from separator normalization.
fn normalize_entry(entry: &str) -> String {
    let entry = entry.trim_matches(|c| c == '"' || c == ' ' || c == '\t');
    let bytes: Vec<char> = entry.chars().collect();
    let mut out = String::new();
    if let Some(rest) = entry.strip_prefix("/cygdrive/") {
        let mut rc = rest.chars();
        if let Some(drive) = rc.next() {
            if is_drive_letter(drive) && (rest.len() == 1 || rc.next() == Some('/')) {
                out.push(drive.to_ascii_uppercase());
                out.push(':');
                out.push_str(&rest[1..]);
            }
        }
    }
    if out.is_empty() && bytes.len() >= 2 && bytes[0] == '/' && is_drive_letter(bytes[1]) {
        out.push(bytes[1].to_ascii_uppercase());
        out.push(':');
        out.extend(bytes[2..].iter());
    }
    if out.is_empty() {
        out.push_str(entry);
    }
    // normalize separators, preserving a leading UNC '\\'
    let unc = out.starts_with("\\\\") || out.starts_with("//");
    let chars: Vec<char> = out.chars().collect();
    let mut norm = String::new();
    for (i, c) in chars.iter().enumerate() {
        if *c == '/' && (i >= 2 || !unc) {
            norm.push('\\');
        } else {
            norm.push(*c);
        }
    }
    norm
}

/// Splits a PATH value that may mix ';' and ':' separators (a ':' is only a
/// drive separator right after a single drive letter) into normalized
/// Windows directory entries; empty entries are dropped.
fn normalize_path_entries(raw: &str) -> Vec<String> {
    let chars: Vec<char> = raw.chars().collect();
    let mut entries = Vec::new();
    let mut segment = String::new();
    let mut flush = |segment: &mut String, entries: &mut Vec<String>| {
        let entry = normalize_entry(segment);
        if !entry.is_empty() {
            entries.push(entry);
        }
        segment.clear();
    };
    for (i, c) in chars.iter().enumerate() {
        let split_semicolon = *c == ';';
        let split_colon = *c == ':'
            && !(segment.chars().count() == 1
                && segment.chars().next().is_some_and(is_drive_letter)
                && chars.get(i + 1) != Some(&':'));
        if split_semicolon || split_colon {
            flush(&mut segment, &mut entries);
        } else {
            segment.push(*c);
        }
    }
    flush(&mut segment, &mut entries);
    entries
}

/// PATHEXT candidates for extensionless operands (lowercased, dot-prefixed),
/// falling back to .com/.exe/.bat/.cmd when unset or empty.
fn pathext_candidates(ctx: &Ctx) -> Vec<String> {
    let mut exts: Vec<String> = win32_env_get(ctx.env, "PATHEXT")
        .map(|v| v.to_string_lossy().into_owned())
        .unwrap_or_default()
        .split(';')
        .filter(|e| !e.is_empty())
        .map(|e| {
            let mut e = e.to_ascii_lowercase();
            if !e.starts_with('.') {
                e.insert(0, '.');
            }
            e
        })
        .collect();
    if exts.is_empty() {
        exts = [".com", ".exe", ".bat", ".cmd"]
            .iter()
            .map(|s| s.to_string())
            .collect();
    }
    exts
}

fn lowercase_extension(path: &str) -> String {
    let slash = path.rfind(['/', '\\']);
    match path.rfind('.') {
        Some(dot) if slash.is_none_or(|s| dot > s) => path[dot..].to_ascii_lowercase(),
        _ => String::new(),
    }
}

fn is_regular_file(path: &str) -> bool {
    std::fs::metadata(path)
        .map(|m| m.is_file())
        .unwrap_or(false)
}

/// Scans one base path (dir + name, or an explicit operand) for the first
/// regular-file candidate; `explicit_extension` suppresses PATHEXT
/// completion. Reports NotExecutable on a directory hit as the fallback.
fn scan_candidates(
    base: &str,
    explicit_extension: bool,
    ctx: &Ctx,
    fallback: &mut ChildResolve,
) -> ChildResolve {
    let mut names = vec![base.to_string()];
    if !explicit_extension {
        for ext in pathext_candidates(ctx) {
            names.push(format!("{base}{ext}"));
        }
    }
    for candidate in &names {
        let Ok(meta) = std::fs::metadata(candidate) else {
            continue;
        };
        if meta.is_dir() {
            if *fallback == ChildResolve::NotFound {
                *fallback = ChildResolve::NotExecutable;
            }
            continue;
        }
        let ext = lowercase_extension(candidate);
        return ChildResolve::Found {
            path: candidate.clone(),
            batch: ext == ".cmd" || ext == ".bat",
        };
    }
    std::mem::replace(fallback, ChildResolve::NotFound)
}

pub fn resolve_child(name: &str, ctx: &Ctx) -> ChildResolve {
    if name.is_empty() {
        return ChildResolve::NotFound;
    }
    let mut fallback = ChildResolve::NotFound;

    if name.contains('/') || name.contains('\\') {
        // explicit (relative or absolute) path
        if name.ends_with('/') || name.ends_with('\\') {
            let stripped = name.trim_end_matches(['/', '\\']);
            if !stripped.is_empty()
                && std::fs::metadata(stripped)
                    .map(|m| m.is_dir())
                    .unwrap_or(false)
            {
                return ChildResolve::NotADirectory;
            }
            return ChildResolve::NotFound;
        }
        let explicit_extension = !lowercase_extension(name).is_empty();
        return scan_candidates(
            &normalize_entry(name),
            explicit_extension,
            ctx,
            &mut fallback,
        );
    }

    // bare name: PATH search with the Windows PE preference the pre-wiring
    // resolver documented ("how Windows actually executes"): extension-bearing
    // candidates across every entry are tried BEFORE the bare name --
    // CreateProcess cannot run an extensionless POSIX script, so `timeout 10
    // cmd /c exit 3` with a shadowing text file named `cmd` still resolves
    // cmd.exe (gate case tm02: GNU rc 3, probed).
    let mut entries = match win32_env_get(ctx.env, "PATH") {
        Some(v) => normalize_path_entries(&v.to_string_lossy()),
        None => {
            // execvp consults a system default path when PATH is unset;
            // the Windows analog of /bin:/usr/bin is System32 + Windows.
            let root = std::env::var("SystemRoot").unwrap_or_else(|_| "C:\\Windows".into());
            vec![format!("{root}\\System32"), root]
        }
    };
    // Same-package sibling fallback (the WinuxCmd /usr/bin analog). GNU
    // timeout execvp's the wrapped command through PATH, and on POSIX the
    // package prefix IS on PATH by construction (/usr/bin), so a GNU timeout
    // can always exec its own siblings. A Windows multicall package is the
    // analog: every entry image (timeout.exe, echo.exe, sleep.exe, ...) is
    // one directory, and Windows' own loader treats that directory as an
    // implicit search entry -- the CreateProcess legacy search consults "the
    // directory from which the application loaded" (Microsoft Learn, CreateProcess
    // search order, entry 1). PATH stays authoritative: the image directory is
    // consulted only after every PATH entry missed, so PATH shadowing and the
    // execvp diagnostics above keep their GNU shapes; the fallback only
    // rescues same-package siblings when the package bin dir is not on PATH
    // (portable runs, test rigs, clean machines -- #1567 wiring regression).
    // std::env::current_exe reads the process image path, which is process
    // state outside the Ctx contract, but it is exactly the state CreateProcess's
    // own loaded-directory rule consumes; the alternative (ABI plumb-through)
    // would widen the v1 surface for a value every Windows host derives the
    // same way.
    if let Ok(image) = std::env::current_exe() {
        if let Some(dir) = image.parent() {
            if !dir.as_os_str().is_empty() {
                entries.push(dir.to_string_lossy().into_owned());
            }
        }
    }
    let explicit_extension = !lowercase_extension(name).is_empty();
    let mut dir_hit = false;
    // pass 1: extension candidates (PATHEXT order when extensionless)
    for dir in &entries {
        let base = format!("{dir}\\{name}");
        let names: Vec<String> = if explicit_extension {
            vec![base]
        } else {
            pathext_candidates(ctx)
                .iter()
                .map(|ext| format!("{base}{ext}"))
                .collect()
        };
        for candidate in names {
            match std::fs::metadata(&candidate) {
                Ok(m) if m.is_file() => {
                    let ext = lowercase_extension(&candidate);
                    return ChildResolve::Found {
                        path: candidate,
                        batch: ext == ".cmd" || ext == ".bat",
                    };
                }
                Ok(m) if m.is_dir() => dir_hit = true,
                _ => {}
            }
        }
    }
    // pass 2: the bare name (an extensionless image; scripts stay dead)
    for dir in &entries {
        let base = format!("{dir}\\{name}");
        match std::fs::metadata(&base) {
            Ok(m) if m.is_file() => {
                return ChildResolve::Found {
                    path: base,
                    batch: false,
                };
            }
            Ok(m) if m.is_dir() => dir_hit = true,
            _ => {}
        }
    }
    if dir_hit {
        // [GNU] execvp keeps searching past EACCES/entry-is-a-directory and
        // reports the access failure only when nothing was executable.
        return ChildResolve::NotExecutable;
    }
    ChildResolve::NotFound
}

/// Canonical Win32 command-line quoting for one argument.
pub fn quote_windows_arg(arg: &str) -> String {
    if !arg.is_empty() && !arg.contains([' ', '\t', '\n', '"']) {
        return arg.to_string();
    }
    let mut out = String::with_capacity(arg.len() + 2);
    out.push('"');
    let mut backslashes = 0usize;
    for c in arg.chars() {
        match c {
            '\\' => backslashes += 1,
            '"' => {
                out.push_str(&"\\".repeat(backslashes * 2 + 1));
                backslashes = 0;
                out.push('"');
            }
            _ => {
                out.push_str(&"\\".repeat(backslashes));
                backslashes = 0;
                out.push(c);
            }
        }
    }
    if backslashes > 0 {
        out.push_str(&"\\".repeat(backslashes * 2));
    }
    out.push('"');
    out
}
