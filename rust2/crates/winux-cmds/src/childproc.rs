//! Shared child-process resolution for the spawner commands (nohup,
//! timeout, ...): a Rust port of the product's childproc helper
//! (src/utils/childproc.cppm), extracted from timeout.rs so every spawner
//! shares one resolver. See the per-item doc comments for the GNU execvp
//! semantics each branch preserves.
//!
//! #1587 extends the chain past the image formats CreateProcess can load,
//! to the execvp forms a POSIX spawner must honor when the operand names
//! an existing non-PE file (a shebang script, an npm/pnpm-style shim —
//! these used to die with a silent 126):
//!
//! 1. the operand resolves through the native chain as before: as given,
//!    then PATHEXT variants, then PATH search for bare names;
//! 2. a resolved `.cmd`/`.bat` routes through the command interpreter;
//! 3. a resolved file with `MZ` magic is a native image;
//! 4. a resolved file starting with `#!` launches the way the kernel's
//!    binfmt_script.c does — [interpreter, optional-arg, script, argv...]
//!    — with the interpreter resolved recursively through this chain
//!    (first space/tab splits the interpreter line; the remainder is ONE
//!    optional argument, trailing space/tab trimmed, a CR kept: a CRLF
//!    shebang misses its interpreter, probed ENOENT);
//! 5. a POSIX-rooted spelling the native chain missed (`/bin/sh`,
//!    `/usr/bin/env`) resolves through the product's shell-namespace
//!    mapper (winux-core::path::shell_tree_to_native — the same
//!    accommodation the file commands make), and finally through its leaf
//!    name on PATH: a shebang interpreter is a POSIX-namespace path by
//!    construction, and a Windows host that can run `sh` at all has it
//!    reachable through the install root or PATH;
//! 6. a file that exists but carries neither MZ magic nor `#!` is the
//!    execvp ENOEXEC analog: callers exit 126 with the GNU strerror text
//!    "Exec format error" (C-locale wording, probed WSL coreutils 9.4);
//!    resolution failure of a shebang INTERPRETER maps to ENOENT (GNU
//!    probed: `timeout 5 ./x` with `#!/bin/no-such-interp` -> 127 "No
//!    such file or directory", the kernel's errno).
//!
//! Documented boundary: glibc's execvp retries a non-executable-format
//! file through /bin/sh (probed: an extensionless text file "runs" as a
//! shell script under WSL GNU, an empty file exits 0). That fallback is a
//! libc extension of execvp, not kernel semantics; per the issue contract
//! WinuxCmd reports "Exec format error" for such files instead.

use std::io::Read as _;
use std::os::windows::process::CommandExt as _;

use winux_core::path::{is_drive_form, shell_tree_to_native};
use winux_core::{win32_env_get, Ctx};

/// What a spawner must hand the OS to launch the resolved child — the
/// executable formats the chain recognizes, in resolution order (batch
/// extension, image magic, shebang line).
#[derive(Debug, PartialEq)]
pub enum ChildLaunch {
    /// A native image (MZ magic, which covers .exe/.com and extensionless
    /// PE binaries): CreateProcess launches it directly.
    Image(String),
    /// A .cmd/.bat batch file: CreateProcess cannot launch one directly,
    /// so it routes through COMSPEC as `cmd /d /s /c "<script> <args...>"`.
    Batch(String),
    /// A `#!` script (binfmt_script.c value model): `argv` is the leading
    /// argv the OS prepends before the caller's arguments — the resolved
    /// interpreter, the optional shebang argument, the resolved script
    /// path, and (for a script-chain interpreter) the deeper tokens.
    /// spawn_child routes an interpreter with a .cmd/.bat extension
    /// through COMSPEC and everything else through CreateProcess.
    Script { argv: Vec<String> },
}

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
/// * #1587: a resolved file is classified into a [`ChildLaunch`] — batch,
///   MZ image, or `#!` script — and an existing file with neither magic
///   is [`ChildResolve::BadFormat`].
#[derive(Debug, PartialEq)]
pub enum ChildResolve {
    Found(ChildLaunch),
    NotFound,
    NotExecutable,
    NotADirectory,
    /// The file exists but no format in the chain can run it (no MZ
    /// magic, no `#!` line): the execvp ENOEXEC analog — callers exit
    /// 126 with the GNU "Exec format error" strerror text.
    BadFormat,
}

/// binfmt_script.c reads the shebang line from the same first page the
/// image loaders use (BINPRM_BUF_SIZE): a line that overflows it is not a
/// script.
const HEAD_BYTES: usize = 256;
/// Guard against interpreter cycles (`#!/x` where x is itself `#!...`).
/// The kernel bounds binfmt recursion the same way in spirit; three
/// levels already exceeds every real-world chain.
const SHEBANG_RECURSION_LIMIT: u32 = 3;

fn is_drive_letter(ch: char) -> bool {
    ch.is_ascii_alphabetic()
}

/// One POSIX '/c/...' (or '/cygdrive/c/...') spelling into 'C:\...' form;
/// non-POSIX entries unchanged apart from separator normalization. The
/// drive-spelling shape requires the slash after the single drive letter
/// (native_path.cppm is_drive_form), so "/bin/sh" is NOT a drive spelling
/// and stays in the shell namespace for the mapper pass.
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
    if out.is_empty()
        && bytes.len() >= 2
        && bytes[0] == '/'
        && is_drive_letter(bytes[1])
        && (bytes.len() == 2 || bytes[2] == '/')
    {
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

fn is_batch_extension(ext: &str) -> bool {
    ext == ".cmd" || ext == ".bat"
}

fn is_regular_file(path: &str) -> bool {
    std::fs::metadata(path)
        .map(|m| m.is_file())
        .unwrap_or(false)
}

/// The first HEAD_BYTES of a file, short-read tolerant. 256 matches the
/// kernel's BINPRM_BUF_SIZE: the whole shebang line must fit in it.
fn read_head(path: &str) -> Vec<u8> {
    let Ok(mut file) = std::fs::File::open(path) else {
        return Vec::new();
    };
    let mut buf = vec![0u8; HEAD_BYTES];
    let mut filled = 0usize;
    while filled < buf.len() {
        match file.read(&mut buf[filled..]) {
            Ok(0) => break,
            Ok(n) => filled += n,
            Err(_) => break,
        }
    }
    buf.truncate(filled);
    buf
}

/// binfmt_script.c line split of a `#!` head: the interpreter is the first
/// space/tab token of the line (up to LF or the 256-byte cap); the
/// remainder after the whitespace run is ONE optional argument, trailing
/// space/tab trimmed (the kernel keeps a CR — a CRLF shebang names an
/// interpreter with a trailing CR and misses it, probed ENOENT). `None`
/// where the kernel would give ENOEXEC: `#!` with no interpreter, or a
/// line that fills the buffer with no newline.
fn parse_shebang_line(head: &[u8]) -> Option<(String, Option<String>)> {
    // skip the "#!" magic: the kernel splits the line that FOLLOWS it
    let body = &head[2..];
    let line_end = body.iter().position(|&b| b == b'\n').unwrap_or(body.len());
    if line_end >= HEAD_BYTES {
        return None; // no newline inside the kernel's buffer
    }
    let trimmed: &[u8] = {
        let mut end = line_end;
        while end > 0 && (body[end - 1] == b' ' || body[end - 1] == b'\t') {
            end -= 1;
        }
        &body[..end]
    };
    let mut start = 0usize;
    while start < trimmed.len() && (trimmed[start] == b' ' || trimmed[start] == b'\t') {
        start += 1;
    }
    let mut interp_end = start;
    while interp_end < trimmed.len() && trimmed[interp_end] != b' ' && trimmed[interp_end] != b'\t'
    {
        interp_end += 1;
    }
    if interp_end == start {
        return None; // "#!" alone: the kernel gives ENOEXEC
    }
    let interpreter = String::from_utf8_lossy(&trimmed[start..interp_end]).into_owned();
    let mut arg_start = interp_end;
    while arg_start < trimmed.len() && (trimmed[arg_start] == b' ' || trimmed[arg_start] == b'\t') {
        arg_start += 1;
    }
    let arg = if arg_start < trimmed.len() {
        Some(String::from_utf8_lossy(&trimmed[arg_start..]).into_owned())
    } else {
        None
    };
    Some((interpreter, arg))
}

/// Classifies one resolved regular file into its launch shape (#1587).
/// Resolution order: the batch extension routes through the command
/// interpreter before any magic read; MZ magic is a native image; `#!`
/// resolves the interpreter line recursively; anything else is the
/// ENOEXEC analog.
fn classify_file(path: &str, ctx: &Ctx, depth: u32) -> ChildResolve {
    let ext = lowercase_extension(path);
    if is_batch_extension(&ext) {
        return ChildResolve::Found(ChildLaunch::Batch(path.to_string()));
    }
    let head = read_head(path);
    if head.len() >= 2 && &head[..2] == b"MZ" {
        return ChildResolve::Found(ChildLaunch::Image(path.to_string()));
    }
    if head.len() >= 2 && &head[..2] == b"#!" {
        return resolve_shebang_script(path, &head, ctx, depth);
    }
    ChildResolve::BadFormat
}

/// Launch assembly for a `#!` script: [interpreter, optional-arg, script],
/// with the interpreter resolved through the same chain. Status mapping
/// follows the kernel's errnos (all GNU-probed): a MISSING interpreter is
/// ENOENT (callers report 127 "No such file or directory" against the
/// script operand), an unrunnable interpreter ENOEXEC, a directory
/// EACCES.
fn resolve_shebang_script(script: &str, head: &[u8], ctx: &Ctx, depth: u32) -> ChildResolve {
    let Some((interpreter, arg)) = parse_shebang_line(head) else {
        return ChildResolve::BadFormat;
    };
    if depth >= SHEBANG_RECURSION_LIMIT {
        return ChildResolve::BadFormat;
    }
    // binfmt_script opens the interpreter by PATH-lookup-free open(): a
    // bare interpreter name is a cwd-relative path, never a PATH search
    // (probed WSL GNU: "#!env sh" -> 127). Route bare names through the
    // explicit-path branch so resolve_bare stays out of the way.
    let interpreter = if interpreter.contains('/') || interpreter.contains('\\') {
        interpreter
    } else {
        format!("./{interpreter}")
    };
    match resolve_chain(&interpreter, ctx, depth + 1) {
        ChildResolve::Found(launch) => match launch {
            ChildLaunch::Image(path) | ChildLaunch::Batch(path) => {
                let mut argv = vec![path];
                if let Some(a) = arg {
                    argv.push(a);
                }
                argv.push(script.to_string());
                ChildResolve::Found(ChildLaunch::Script { argv })
            }
            ChildLaunch::Script { argv: inner } => {
                // script-chain interpreter: the OS argv is the inner
                // interpreter's launch followed by the outer script's
                // [interpreter, arg?, script] tokens, then the caller's.
                let mut argv = inner;
                argv.push(interpreter);
                if let Some(a) = arg {
                    argv.push(a);
                }
                argv.push(script.to_string());
                ChildResolve::Found(ChildLaunch::Script { argv })
            }
        },
        ChildResolve::NotFound => ChildResolve::NotFound,
        other => other,
    }
}

/// True when the spelling lives in the POSIX shell namespace (a single
/// leading slash, not a "//x" UNC form and not a "/c/..." drive spelling
/// the native chain already resolves).
fn is_posix_rooted(name: &str) -> bool {
    name.starts_with('/') && !name.starts_with("//") && !is_drive_form(name)
}

/// Shell-namespace pass for POSIX-rooted explicit paths (childproc.cppm
/// has no analog: it predates the mapper). shell_tree_to_native folds the
/// `.exe` leaf the way `/usr/bin/seq` resolves; PATHEXT completes the
/// rest. Reports directory hits through `fallback` like scan_candidates.
fn scan_namespace(name: &str, ctx: &Ctx, fallback: &mut ChildResolve) -> Option<String> {
    if !is_posix_rooted(name) {
        return None;
    }
    let mapped = shell_tree_to_native(ctx.env, name)?;
    if mapped.is_empty() {
        return None;
    }
    let mut names = vec![mapped.clone()];
    for ext in pathext_candidates(ctx) {
        names.push(format!("{mapped}{ext}"));
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
        return Some(candidate.clone());
    }
    None
}

/// Scans one base path (dir + name, or an explicit operand) for the first
/// regular-file candidate; `explicit_extension` suppresses PATHEXT
/// completion. Reports NotExecutable on a directory hit as the fallback.
/// Returns the candidate's native path, or None when nothing matched.
fn scan_candidates(
    base: &str,
    explicit_extension: bool,
    ctx: &Ctx,
    fallback: &mut ChildResolve,
) -> Option<String> {
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
        return Some(candidate.clone());
    }
    None
}

/// Full resolution chain. `depth` counts shebang levels only; the
/// namespace passes are namespace translations, not new script levels.
fn resolve_chain(name: &str, ctx: &Ctx, depth: u32) -> ChildResolve {
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
        // pass A: the native chain, used as given
        if let Some(path) = scan_candidates(
            &normalize_entry(name),
            explicit_extension,
            ctx,
            &mut fallback,
        ) {
            return classify_file(&path, ctx, depth);
        }
        // pass B: the product's shell-namespace mapper for POSIX-rooted
        // spellings ("#!/bin/sh" has no native analog; the file commands
        // resolve the same spelling through this mapper).
        if let Some(path) = scan_namespace(name, ctx, &mut fallback) {
            return classify_file(&path, ctx, depth);
        }
        // pass C: the leaf name through PATH — the last accommodation for
        // a POSIX-rooted spelling on a host with neither an install root
        // nor the mapped prefix, but the tool on PATH (a shebang
        // interpreter's "/bin/..." prefix only exists on POSIX; the leaf
        // is what a Windows host can actually run).
        if is_posix_rooted(name) {
            let leaf = name.rsplit('/').next().unwrap_or("");
            if !leaf.is_empty() {
                return resolve_bare(leaf, ctx, depth);
            }
        }
        return fallback;
    }

    resolve_bare(name, ctx, depth)
}

/// Bare-name PATH search: extension-bearing candidates across every entry
/// are tried BEFORE the bare name (the Windows PE preference the pre-wiring
/// resolver documented, "how Windows actually executes") — CreateProcess
/// cannot run an extensionless POSIX script, so `timeout 10 cmd /c exit 3`
/// with a shadowing text file named `cmd` still resolves cmd.exe (gate
/// case tm02: GNU rc 3, probed). Every found candidate is classified
/// (#1587), so an extensionless `#!` shim now launches.
fn resolve_bare(name: &str, ctx: &Ctx, depth: u32) -> ChildResolve {
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
                Ok(m) if m.is_file() => return classify_file(&candidate, ctx, depth),
                Ok(m) if m.is_dir() => dir_hit = true,
                _ => {}
            }
        }
    }
    // pass 2: the bare name (extensionless image or a `#!` shim)
    for dir in &entries {
        let base = format!("{dir}\\{name}");
        if is_regular_file(&base) {
            return classify_file(&base, ctx, depth);
        }
        if std::fs::metadata(&base)
            .map(|m| m.is_dir())
            .unwrap_or(false)
        {
            dir_hit = true;
        }
    }
    if dir_hit {
        // [GNU] execvp keeps searching past EACCES/entry-is-a-directory and
        // reports the access failure only when nothing was executable.
        return ChildResolve::NotExecutable;
    }
    ChildResolve::NotFound
}

/// execvp-shaped entry point: resolve a spawner's command operand into a
/// [`ChildLaunch`] (or the GNU execvp error analogs).
pub fn resolve_child(name: &str, ctx: &Ctx) -> ChildResolve {
    resolve_chain(name, ctx, 0)
}

/// Canonical Win32 command-line quoting for one argument.
fn quote_windows_arg(arg: &str) -> String {
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

/// The COMSPEC route (childproc.cppm plan_child_launch's batch shape):
/// CreateProcess cannot launch a batch file directly, so it becomes
/// `cmd /d /s /c "<script> <args...>"`.
fn batch_command(script: &str, args: &[String], ctx: &Ctx) -> std::process::Command {
    let comspec = win32_env_get(ctx.env, "COMSPEC")
        .map(|v| v.to_string_lossy().into_owned())
        .filter(|v| !v.is_empty())
        .unwrap_or_else(|| "cmd.exe".to_string());
    let mut inner = quote_windows_arg(script);
    for a in args {
        inner.push(' ');
        inner.push_str(&quote_windows_arg(a));
    }
    let mut c = std::process::Command::new(&comspec);
    c.raw_arg("/d").raw_arg("/s").raw_arg("/c");
    c.raw_arg(format!("\"{inner}\""));
    c
}

/// Builds the Command for a resolved launch — the shared tail of every
/// spawner (timeout, nohup, ...): env/cwd applied, nothing spawned.
/// Exposed for spawners that must configure process-creation flags
/// before spawn (nice's CREATE_SUSPENDED, #1359); [`spawn_child`] is
/// build + spawn.
pub fn build_child_command(
    launch: &ChildLaunch,
    args: &[String],
    ctx: &Ctx,
) -> std::process::Command {
    let mut cmd = match launch {
        ChildLaunch::Image(path) => {
            // native image: spawn the resolved path; argv[0] is the
            // resolved path itself (std has no Windows argv0 control,
            // and the pre-wiring resolver did the same).
            let mut c = std::process::Command::new(path);
            c.args(args);
            c
        }
        ChildLaunch::Batch(path) => batch_command(path, args, ctx),
        ChildLaunch::Script { argv } => {
            // binfmt_script value model: the OS-prepended argv first, then
            // the caller's arguments. A .cmd/.bat interpreter keeps the
            // COMSPEC route (it is still a batch file to CreateProcess).
            let head = argv.first().cloned().unwrap_or_default();
            let mut script_args: Vec<String> = argv.iter().skip(1).cloned().collect();
            script_args.extend(args.iter().cloned());
            let ext = lowercase_extension(&head);
            if is_batch_extension(&ext) {
                batch_command(&head, &script_args, ctx)
            } else {
                let mut c = std::process::Command::new(&head);
                c.args(&script_args);
                c
            }
        }
    };
    // GetEnvironmentStringsW carries "=C:" style pseudo-vars that
    // Command::envs rejects; skip them (the winuxcmd ABI does the same).
    cmd.envs(
        ctx.env
            .entries()
            .iter()
            .filter(|(k, _)| !k.is_empty() && !k.to_string_lossy().starts_with('='))
            .map(|(k, v)| (k.clone(), v.clone())),
    );
    cmd.current_dir(&ctx.cwd);
    cmd
}

/// Spawns the resolved child with inherited stdio handles, the caller's
/// environment and cwd. Only the spawn itself can fail here; resolution
/// failures are the caller's diagnostics (127/126 with GNU wording).
pub fn spawn_child(
    launch: &ChildLaunch,
    args: &[String],
    ctx: &Ctx,
) -> std::io::Result<std::process::Child> {
    build_child_command(launch, args, ctx).spawn()
}

#[cfg(test)]
mod tests {
    use winux_core::test_support::ScratchCtx;

    fn scratch(tag: &str) -> ScratchCtx {
        ScratchCtx::new(&format!("childproc-{tag}"))
    }

    fn write(path: &std::path::Path, bytes: &[u8]) {
        if let Some(parent) = path.parent() {
            std::fs::create_dir_all(parent).unwrap();
        }
        std::fs::write(path, bytes).unwrap();
    }

    fn mz() -> &'static [u8] {
        b"MZ\x90\x00fake-image"
    }

    /// Fixture root for the shell-namespace mapper pass: NIU_ROOT at the
    /// scratch with bin/sh.exe (MZ magic), like a real install root.
    fn root_with_sh(sc: &mut ScratchCtx) -> String {
        let sh = sc.path("bin").join("sh.exe");
        write(&sh, mz());
        sc.env.set("NIU_ROOT", &sc.dir.to_string_lossy());
        sh.to_string_lossy().into_owned()
    }

    #[test]
    fn magic_classes() {
        let sc = scratch("magic");
        let ctx = sc.ctx();
        let img = sc.path("img.bin");
        write(&img, mz());
        assert_eq!(
            super::resolve_child(img.to_str().unwrap(), &ctx),
            super::ChildResolve::Found(super::ChildLaunch::Image(
                img.to_string_lossy().into_owned()
            ))
        );
        let bat = sc.path("run.bat");
        write(&bat, b"@echo off\r\n");
        assert_eq!(
            super::resolve_child(bat.to_str().unwrap(), &ctx),
            super::ChildResolve::Found(super::ChildLaunch::Batch(
                bat.to_string_lossy().into_owned()
            ))
        );
        let txt = sc.path("plain.txt");
        write(&txt, b"just text\n");
        assert_eq!(
            super::resolve_child(txt.to_str().unwrap(), &ctx),
            super::ChildResolve::BadFormat
        );
        let empty = sc.path("empty.bin");
        write(&empty, b"");
        assert_eq!(
            super::resolve_child(empty.to_str().unwrap(), &ctx),
            super::ChildResolve::BadFormat
        );
    }

    #[test]
    fn shebang_script_launches_through_mapper() {
        let mut sc = scratch("shebang-mapper");
        let sh = root_with_sh(&mut sc);
        let ctx = sc.ctx();
        let hi = sc.path("hi.sh");
        write(&hi, b"#!/bin/sh\necho hi\n");
        let script = hi.to_string_lossy().into_owned();
        assert_eq!(
            super::resolve_child(&script, &ctx),
            super::ChildResolve::Found(super::ChildLaunch::Script {
                argv: vec![sh, script],
            })
        );
    }

    #[test]
    fn shebang_interpreter_leaf_falls_back_to_path() {
        // No install root configured (no NIU_ROOT/WINUXSH_ROOT): the leaf
        // "sh" of "/bin/sh" resolves through PATH (issue #1587 chain pass C).
        let mut sc = scratch("shebang-leaf");
        let path = sc.path("pathdir");
        let sh = path.join("sh.exe");
        write(&sh, mz());
        sc.env.set("PATH", &path.to_string_lossy());
        let ctx = sc.ctx();
        let hi = sc.path("hi.sh");
        write(&hi, b"#!/bin/sh\necho hi\n");
        let script = hi.to_string_lossy().into_owned();
        assert_eq!(
            super::resolve_child(&script, &ctx),
            super::ChildResolve::Found(super::ChildLaunch::Script {
                argv: vec![sh.to_string_lossy().into_owned(), script],
            })
        );
    }

    #[test]
    fn shebang_two_token_interpreter_line() {
        // binfmt_script: the remainder after the interpreter is ONE
        // optional argument (`#!/bin/echo -n` -> [echo, -n, script]).
        let mut sc = scratch("shebang-arg");
        let echo = sc.path("bin").join("echo.exe");
        write(&echo, mz());
        sc.env.set("NIU_ROOT", &sc.dir.to_string_lossy());
        let ctx = sc.ctx();
        let pr = sc.path("pr.sh");
        write(&pr, b"#!/bin/echo -n\n");
        let script = pr.to_string_lossy().into_owned();
        assert_eq!(
            super::resolve_child(&script, &ctx),
            super::ChildResolve::Found(super::ChildLaunch::Script {
                argv: vec![echo.to_string_lossy().into_owned(), "-n".into(), script],
            })
        );
    }

    #[test]
    fn shebang_crlf_misses_the_interpreter() {
        // The kernel keeps the CR: "#!/bin/sh\r\n" names interpreter
        // "/bin/sh\r", which misses -> ENOENT (probed GNU 127 wording).
        let mut sc = scratch("shebang-crlf");
        root_with_sh(&mut sc);
        let ctx = sc.ctx();
        let hi = sc.path("hi.sh");
        write(&hi, b"#!/bin/sh\r\necho hi\r\n");
        assert_eq!(
            super::resolve_child(hi.to_str().unwrap(), &ctx),
            super::ChildResolve::NotFound
        );
    }

    #[test]
    fn shebang_missing_interpreter_is_not_found() {
        let mut sc = scratch("shebang-missing");
        root_with_sh(&mut sc);
        let ctx = sc.ctx();
        let bad = sc.path("bad.sh");
        write(&bad, b"#!/bin/no-such-interp\necho never\n");
        assert_eq!(
            super::resolve_child(bad.to_str().unwrap(), &ctx),
            super::ChildResolve::NotFound
        );
    }

    #[test]
    fn shebang_bare_relative_interpreter_stays_cwd_scoped() {
        // The kernel does not PATH-search shebang interpreters: a bare
        // "env" resolves against the cwd only (probed: "#!env sh" -> 127).
        let mut sc = scratch("shebang-bare");
        root_with_sh(&mut sc);
        let ctx = sc.ctx();
        let b = sc.path("b.sh");
        write(&b, b"#!env sh\necho bare\n");
        assert_eq!(
            super::resolve_child(b.to_str().unwrap(), &ctx),
            super::ChildResolve::NotFound
        );
    }

    #[test]
    fn shebang_chain_argv_inner_script_once() {
        // #1595: a shebang interpreter that is itself a script -- the
        // kernel's argv is [sh, -e, inner, -x, outer]: the inner script
        // appears exactly once (as the script argument to its own
        // interpreter), and the outer shebang's arg follows it.
        let mut sc = scratch("shebang-chain");
        let sh = root_with_sh(&mut sc);
        let ctx = sc.ctx();
        let inner = sc.path("inner.sh");
        write(&inner, b"#!/bin/sh -e\necho inner\n");
        let inner_spelling = inner.to_string_lossy().into_owned();
        let outer = sc.path("outer.sh");
        let outer_line = format!("#!{inner_spelling} -x\necho outer\n");
        write(&outer, outer_line.as_bytes());
        let outer_spelling = outer.to_string_lossy().into_owned();
        assert_eq!(
            super::resolve_child(&outer_spelling, &ctx),
            super::ChildResolve::Found(super::ChildLaunch::Script {
                argv: vec![sh, "-e".into(), inner_spelling, "-x".into(), outer_spelling],
            })
        );
    }

    #[test]
    fn shebang_overlong_first_line_is_not_a_script() {
        // #1595: a first line that fills the 256-byte kernel buffer with
        // no newline is ENOEXEC, not a shebang -- the old
        // `line_end >= HEAD_BYTES` guard could never fire (body <= 254).
        let mut head = vec![b'#', b'!'];
        head.extend(std::iter::repeat(b'x').take(254));
        assert_eq!(head.len(), 256);
        assert_eq!(super::parse_shebang_line(&head), None);
        // A short file with no newline still parses: the line ends at
        // EOF inside the buffer.
        assert_eq!(
            super::parse_shebang_line(b"#!/bin/sh"),
            Some(("/bin/sh".to_string(), None))
        );
        // A newline at the last buffer byte still fits: the line parses.
        let mut fitting = vec![b'#', b'!'];
        fitting.extend(std::iter::repeat(b'x').take(253));
        fitting.push(b'\n');
        assert_eq!(fitting.len(), 256);
        assert!(super::parse_shebang_line(&fitting).is_some());
    }

    #[test]
    fn extensionless_path_shim_launches() {
        // The issue's npm-shim shape: an extensionless `#!/bin/sh` file
        // resolved as a bare name from PATH now launches (#1587).
        let mut sc = scratch("shim");
        let path = sc.path("pathdir");
        let sh = path.join("sh.exe");
        write(&sh, mz());
        sc.env.set("PATH", &path.to_string_lossy());
        let ctx = sc.ctx();
        let shim = path.join("agent-browser");
        write(&shim, b"#!/bin/sh\nexec real-agent \"$@\"\n");
        let script = shim.to_string_lossy().into_owned();
        assert_eq!(
            super::resolve_child("agent-browser", &ctx),
            super::ChildResolve::Found(super::ChildLaunch::Script {
                argv: vec![sh.to_string_lossy().into_owned(), script],
            })
        );
    }

    #[test]
    fn shebang_script_via_path_shadowing_prefers_the_pe() {
        // tm02's Windows reality: a shebang shim named `cmd` on PATH is
        // only reached AFTER every PATHEXT candidate missed, so a dir
        // holding both cmd.exe and the shim resolves cmd.exe.
        let mut sc = scratch("pe-preference");
        let path = sc.path("pathdir");
        let cmd = path.join("cmd.exe");
        write(&cmd, mz());
        let shim = path.join("cmd");
        write(&shim, b"#!/bin/sh\nexit 9\n");
        sc.env.set("PATH", &path.to_string_lossy());
        let ctx = sc.ctx();
        assert_eq!(
            super::resolve_child("cmd", &ctx),
            super::ChildResolve::Found(super::ChildLaunch::Image(
                cmd.to_string_lossy().into_owned()
            ))
        );
    }

    #[cfg(windows)]
    #[test]
    fn spawn_child_batch_routes_comspec() {
        let mut sc = scratch("spawn-batch");
        let bat = sc.path("exit7.bat");
        write(&bat, b"@echo off\r\nexit /b 7\r\n");
        sc.env.set(
            "COMSPEC",
            &std::env::var("COMSPEC").unwrap_or_else(|_| "cmd.exe".into()),
        );
        let ctx = sc.ctx();
        let mut child = super::spawn_child(
            &super::ChildLaunch::Batch(bat.to_string_lossy().into_owned()),
            &[],
            &ctx,
        )
        .unwrap();
        let status = child.wait().unwrap();
        assert_eq!(status.code(), Some(7));
    }

    #[cfg(windows)]
    #[test]
    fn spawn_child_script_argv_order() {
        // Script spawn = [interpreter, shebang-arg?, script, caller args...]:
        // env.exe (System32, real PE) printing its arguments verbatim.
        let mut sc = scratch("spawn-script");
        let script = sc.path("noop.sh");
        write(&script, b"#!/usr/bin/env noop-arg\n");
        let ctx = sc.ctx();
        let mut child = super::spawn_child(
            &super::ChildLaunch::Script {
                argv: vec![
                    "C:\\Windows\\System32\\cmd.exe".into(),
                    "/c".into(),
                    "echo".into(),
                    script.to_string_lossy().into_owned(),
                ],
            },
            &["tail-arg".into()],
            &ctx,
        )
        .unwrap();
        let status = child.wait().unwrap();
        assert_eq!(status.code(), Some(0));
    }
}
