// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
//! WinuxCmd 2.0 command-library pilot (issue #1488): `pwd` as a library-form
//! command.
//!
//! Everything the command consumes arrives explicitly through [`WcmdIoCtx`]:
//! stream handles, a UTF-8 environment block, a working directory, argv, and
//! a cancellation flag. The library never queries or writes process-level
//! state — the three defect classes of #1487 cannot be expressed in this
//! shape. Interface contract and hard rules: `DOCS/2.0-cmdlib-design.md`
//! (§2 interface, §6 ban-lint).

use std::ffi::{c_char, c_int, c_void, CStr};
use std::fs::File;
use std::io::Write;
use std::mem::ManuallyDrop;
use std::os::windows::io::FromRawHandle;
use std::path::PathBuf;

/// ABI version of the `WcmdIoCtx` contract (design §2.2 / §3.2). Hosts check
/// this right after loading and refuse mismatched libraries.
pub const WCMD_ABI_VERSION: i32 = 0;

/// Exit code for "the library panicked inside the boundary" (design §2.4).
/// GNU-adjacent (126 = found but cannot execute); hosts treat it specially.
pub const WCMD_EXIT_PANIC: i32 = 126;

/// Exit code for option/usage errors, matching the exe-side pipeline.
pub const WCMD_EXIT_USAGE: i32 = 2;

/// Exit code used when the host requested cancellation before any output
/// was produced (SIGINT-adjacent, same value the exe world reports for it).
pub const WCMD_EXIT_CANCELLED: i32 = 130;

/// Exported ABI probe (design §3.2).
#[no_mangle]
pub extern "C" fn wcmd_abi_version() -> i32 {
    WCMD_ABI_VERSION
}

/// Execution context for a library-form command (ABI v0, design §2.2).
///
/// All pointers are borrowed for the duration of the call only. The callee
/// must not close handles, mutate buffers, or retain any pointer past the
/// return.
#[repr(C)]
pub struct WcmdIoCtx {
    /// Win32 HANDLE for the command's stdin; `NULL` = not provided.
    pub stdin: *mut c_void,
    /// Win32 HANDLE for the command's stdout; `NULL` = not provided.
    pub stdout: *mut c_void,
    /// Win32 HANDLE for the command's stderr; `NULL` = not provided.
    pub stderr: *mut c_void,
    /// Borrowed UTF-8 `"K=V\0…\0\0"` environment block (double NUL
    /// terminated). Replaces every process-environment read.
    pub env_block: *const c_char,
    /// Borrowed UTF-8 NUL-terminated physical working directory. Replaces
    /// every process-cwd read. Host contract: already symlink-resolved.
    pub cwd: *const c_char,
    /// UTF-8 argument vector, `argv[0]` = command name.
    pub argc: c_int,
    /// Borrowed UTF-8 argument pointers, `argc` of them.
    pub argv: *const *const c_char,
    /// Borrowed host-owned cancellation flag; non-zero = terminate early.
    /// Long loops must poll it at checkpoints (#1487 defect B). `NULL` =
    /// host provides no cancellation.
    pub cancel_flag: *const u32,
}

/// `pwd` as a library-form command.
///
/// Ported from `src/commands/pwd.cpp` (parity anchor, #1462 audit):
/// mode default incl. the POSIXLY_CORRECT presence test at pwd.cpp:62-65
/// ([GNU] pwd.c:331), the logical PWD validation chain at pwd.cpp:99-177,
/// the physical canonical form at pwd.cpp:123-150 ([GNU] pwd.c:62,377-386),
/// and the non-option operand warning at pwd.cpp:209-211.
#[no_mangle]
pub extern "C" fn wcmd_pwd(ctx: *const WcmdIoCtx) -> i32 {
    let Some(ctx) = (unsafe { ctx.as_ref() }) else {
        return WCMD_EXIT_PANIC;
    };
    // Panic containment at the boundary (design §2.4): the default panic
    // hook writes to the real process error stream — an implicit channel —
    // so it is swapped for a silent one for the duration of the call and
    // restored afterwards.
    let prev_hook = std::panic::take_hook();
    std::panic::set_hook(Box::new(|_| {}));
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| run_pwd(ctx)));
    std::panic::set_hook(prev_hook);
    match result {
        Ok(code) => code,
        Err(_) => {
            let _ = write_stream(ctx.stderr, b"wcmd: internal panic in pwd\n");
            WCMD_EXIT_PANIC
        }
    }
}

// ======================================================
// Boundary helpers (the only `unsafe` in this library)
// ======================================================

/// Poll the borrowed cancellation flag once. pwd is instantaneous, so its
/// single checkpoint is the call entry; long-running commands poll inside
/// their loops (design §2.2).
fn cancelled(ctx: &WcmdIoCtx) -> bool {
    !ctx.cancel_flag.is_null() && (unsafe { *ctx.cancel_flag }) != 0
}

/// Write `bytes` fully to a borrowed stream handle (design §2.3): plain
/// byte-stream semantics whether the handle is a pipe, file, or console.
/// No buffering survives the call. The handle stays owned by the host —
/// `ManuallyDrop` prevents the closing that dropping a `File` would do.
fn write_stream(handle: *mut c_void, bytes: &[u8]) -> bool {
    if handle.is_null() {
        return false;
    }
    let mut stream = ManuallyDrop::new(unsafe { File::from_raw_handle(handle) });
    stream.write_all(bytes).is_ok()
}

/// Borrowed NUL-terminated UTF-8 string → owned String.
fn cstr_to_string(p: *const c_char) -> String {
    if p.is_null() {
        return String::new();
    }
    unsafe { CStr::from_ptr(p) }.to_string_lossy().into_owned()
}

/// Collect `argc` borrowed UTF-8 argv entries.
fn collect_argv(ctx: &WcmdIoCtx) -> Vec<String> {
    if ctx.argv.is_null() || ctx.argc <= 0 {
        return Vec::new();
    }
    (0..ctx.argc as usize)
        .map(|i| {
            unsafe { CStr::from_ptr(*ctx.argv.add(i)) }
                .to_string_lossy()
                .into_owned()
        })
        .collect()
}

/// Look `key` up in the borrowed UTF-8 environment block (`"K=V\0…\0\0"`).
/// Returns `Some(value)` for a present key — including a present-but-empty
/// one — and `None` for an absent key. The presence test, not the value,
/// is what flips pwd's default mode (pwd.cpp:62-65, [GNU] pwd.c:331).
fn env_get(env: *const c_char, key: &str) -> Option<String> {
    if env.is_null() {
        return None;
    }
    let mut p = env;
    unsafe {
        loop {
            let entry = CStr::from_ptr(p).to_bytes();
            if entry.is_empty() {
                return None; // reached the trailing NUL: key absent
            }
            if let Some(v) = entry
                .strip_prefix(key.as_bytes())
                .and_then(|v| v.strip_prefix(b"="))
            {
                return Some(String::from_utf8_lossy(v).into_owned());
            }
            p = p.add(entry.len() + 1);
        }
    }
}

// ======================================================
// pwd pipeline (pure: no unsafe, no process state)
// ======================================================

enum PathMode {
    Physical,
    Logical,
}

struct ParsedArgs {
    /// Last explicit -L/-P occurrence, if any.
    mode: Option<PathMode>,
    /// Non-option operand count.
    operands: usize,
}

/// getopt-shaped option scan for the pilot option set. Invalid options are
/// SIMPLIFIED (pilot scope): the exe pipeline's full coreutils usage block
/// and exact diagnostics land with the mechanical-replacement differential
/// gate (design §5 step 5); only the shape (fail to stderr + exit 2) is
/// preserved here.
fn parse_args(argv: &[String]) -> Result<ParsedArgs, String> {
    let mut parsed = ParsedArgs {
        mode: None,
        operands: 0,
    };
    let mut options_done = false;
    for arg in argv.iter().skip(1) {
        if options_done || !arg.starts_with('-') || arg == "-" {
            parsed.operands += 1;
            continue;
        }
        if arg == "--" {
            options_done = true;
            continue;
        }
        if let Some(long) = arg.strip_prefix("--") {
            match long {
                "logical" => parsed.mode = Some(PathMode::Logical),
                "physical" => parsed.mode = Some(PathMode::Physical),
                other => return Err(other.to_string()),
            }
            continue;
        }
        for ch in arg.chars().skip(1) {
            // short cluster, e.g. "-LP"
            match ch {
                'L' => parsed.mode = Some(PathMode::Logical),
                'P' => parsed.mode = Some(PathMode::Physical),
                other => return Err(other.to_string()),
            }
        }
    }
    Ok(parsed)
}

/// pwd.cpp:99-111 — drive-absolute (`X:\` / `X:/`), UNC (`\\` / `//`), or
/// rooted (`\…` / `/…`) values count as absolute.
fn is_absolute_pwd_value(path: &str) -> bool {
    let b = path.as_bytes();
    if b.len() >= 3 && b[0].is_ascii_alphabetic() && b[1] == b':' && (b[2] == b'\\' || b[2] == b'/')
    {
        return true;
    }
    if b.len() >= 2 && ((b[0] == b'\\' && b[1] == b'\\') || (b[0] == b'/' && b[1] == b'/')) {
        return true;
    }
    !b.is_empty() && (b[0] == b'\\' || b[0] == b'/')
}

/// pwd.cpp:113-121 — reject `.`/`..` components. Manual split over both
/// separators: Rust `Path` iteration normalizes interior `.` away, while the
/// C++ `std::filesystem::path` iteration (the parity anchor) does not.
fn has_dot_components(path: &str) -> bool {
    path.split(['\\', '/']).any(|c| c == "." || c == "..")
}

/// Strip the `\\?\` (and `\\?\UNC\`) verbatim prefix that Rust
/// `canonicalize` adds, so output keeps the textual shape of the exe side
/// (`std::filesystem::canonical` returns the plain form).
fn strip_verbatim(path: &str) -> String {
    if let Some(rest) = path.strip_prefix(r"\\?\UNC\") {
        format!(r"\\{rest}")
    } else if let Some(rest) = path.strip_prefix(r"\\?\") {
        rest.to_string()
    } else {
        path.to_string()
    }
}

/// pwd.cpp:123-150 — the physical current directory. The process query of
/// the exe side becomes `ctx.cwd` here (host contract: physical path);
/// canonicalization still resolves any remaining junction/symlink transit,
/// falling back to the injected value on failure.
fn physical_dir(cwd: &str) -> String {
    match std::fs::canonicalize(cwd) {
        Ok(p) => strip_verbatim(&p.to_string_lossy()),
        Err(_) => cwd.to_string(),
    }
}

/// pwd.cpp:152-177 — the logical PWD validation chain: absolute, no `.` /
/// `..` components, exists, and names the same directory as the physical
/// cwd. Returns the PWD value verbatim when valid.
///
/// Pilot simplification: same-directory is decided by canonical-form
/// equality (case-insensitive, Windows path semantics) instead of the C++
/// `std::filesystem::equivalent` file-identity check; the differential gate
/// (design §5 step 5) must tighten this before any tiers switch.
fn validated_logical_pwd(env: *const c_char, physical: &str) -> Option<String> {
    let pwd = env_get(env, "PWD")?;
    if pwd.is_empty() || !is_absolute_pwd_value(&pwd) || has_dot_components(&pwd) {
        return None;
    }
    let pwd_path = PathBuf::from(&pwd);
    if !pwd_path.exists() {
        return None;
    }
    match (
        std::fs::canonicalize(&pwd_path),
        std::fs::canonicalize(physical),
    ) {
        (Ok(a), Ok(b)) => {
            let a = strip_verbatim(&a.to_string_lossy());
            let b = strip_verbatim(&b.to_string_lossy());
            if a.eq_ignore_ascii_case(&b) {
                Some(pwd)
            } else {
                None
            }
        }
        _ => None,
    }
}

fn run_pwd(ctx: &WcmdIoCtx) -> i32 {
    // Entry checkpoint of the cancellation protocol (see `cancelled`).
    if cancelled(ctx) {
        return WCMD_EXIT_CANCELLED;
    }

    let argv = collect_argv(ctx);
    let parsed = match parse_args(&argv) {
        Ok(p) => p,
        Err(bad) => {
            // SIMPLIFIED diagnostics — see parse_args doc comment.
            let _ = write_stream(
                ctx.stderr,
                format!("pwd: invalid option -- '{bad}'\n").as_bytes(),
            );
            let _ = write_stream(ctx.stderr, b"Try 'pwd --help' for more information.\n");
            return WCMD_EXIT_USAGE;
        }
    };

    // pwd.cpp:209-211 — operands are ignored with a warning on stderr.
    if parsed.operands > 0 {
        let _ = write_stream(ctx.stderr, b"pwd: ignoring non-option arguments\n");
    }

    // pwd.cpp:123-150 — physical path first: the -L validation chain
    // compares against it.
    let physical = physical_dir(&cstr_to_string(ctx.cwd));

    // pwd.cpp:58-83 — default is physical; a present POSIXLY_CORRECT (any
    // value, empty included) flips the default to logical; explicit -L/-P
    // override, last occurrence wins.
    let mut mode = if env_get(ctx.env_block, "POSIXLY_CORRECT").is_some() {
        PathMode::Logical
    } else {
        PathMode::Physical
    };
    if let Some(m) = parsed.mode {
        mode = m;
    }

    let out = match mode {
        PathMode::Logical => validated_logical_pwd(ctx.env_block, &physical).unwrap_or(physical),
        PathMode::Physical => physical,
    };

    let mut bytes = out.into_bytes();
    bytes.push(b'\n');
    if write_stream(ctx.stdout, &bytes) {
        0
    } else {
        let _ = write_stream(ctx.stderr, b"pwd: write error\n");
        1
    }
}
