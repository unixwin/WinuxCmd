//! winuxcmd-ffi: the C ABI surface for in-process (host-side) invocation.
//!
//! ABI rules per #1488 ("ABI 面只传显式句柄/缓冲，不传环境状态"):
//! * everything the command sees arrives as arguments: argv array, an
//!   explicit `envp` array (`name=value`), an explicit cwd string, and
//!   explicit buffers (v1: stdin whole-buffer; v2: a streaming pull
//!   callback, see [`winuxcmd_run_stream`]; v3: pull callback for stdin
//!   plus push callbacks for stdout/stderr, see [`winuxcmd_run_io`]) for
//!   stdin/stdout/stderr bytes;
//! * the boundary is `catch_unwind`-guarded: a panicking command is
//!   converted to status [`winux_core::ABI_FAILURE_STATUS`] (125) instead of
//!   unwinding into a foreign caller;
//! * no CRT fd binding, no process stdio access, no thread-locals — the
//!   #1487 lesson. The streaming entries (v2, v3) keep the same red line:
//!   the host reads and writes its own console/pipe and exchanges plain
//!   byte buffers through the callbacks; Rust sees only buffer contents.
//!
//! Status contract: `Ok(n)` -> n; `Err(CmdError::Io)` -> 125 as well (the
//! host injected a broken handle); panic -> 125; v2 host-stops-feeding ->
//! 124 (`winux_core::stream::STREAM_TIMEOUT_STATUS`).
//!
//! Versioning: `winuxcmd_abi_version()` stayed 1 while v2 was additive; it
//! became 2 when the C++ bridge adopted `winuxcmd_run_stream` (#1488
//! streaming ABI v2 bridge adoption), and is 3 since the streaming output
//! entry `winuxcmd_run_io` (host write callbacks) was added — rust2_abi.h
//! (`WINUXCMD_RUST2_ABI_VERSION`) bumped in the same commit, per the
//! header's own rule. v1 `winuxcmd_run` and v2 `winuxcmd_run_stream` stay
//! exported with unchanged semantics. See rust2/README.md.

use std::ffi::{c_char, c_int, c_void, CStr};
use std::io::{Read, Write};
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;

use winux_core::stream::{HostWriter, StreamReadFn, StreamReader, StreamWriteFn};
use winux_core::{Ctx, Io, MapEnv};

#[cfg(test)]
mod io_tests;
#[cfg(test)]
mod stream_tests;
#[cfg(test)]
mod tests;

/// Opaque byte buffer handed back to the host. Freed with
/// `winuxcmd_free_buf`.
#[repr(C)]
pub struct CBuf {
    pub ptr: *mut u8,
    pub len: usize,
    pub cap: usize,
}

impl CBuf {
    fn empty() -> CBuf {
        CBuf {
            ptr: std::ptr::null_mut(),
            len: 0,
            cap: 0,
        }
    }

    fn from_vec(mut v: Vec<u8>) -> CBuf {
        if v.is_empty() {
            return CBuf::empty();
        }
        let cap = v.capacity();
        let ptr = v.as_mut_ptr();
        let len = v.len();
        std::mem::forget(v);
        CBuf { ptr, len, cap }
    }
}

/// Host-supplied strings parsed once, shared by both ABI entries.
struct HostInputs {
    name: String,
    args: Vec<std::ffi::OsString>,
    env: MapEnv,
    cwd: PathBuf,
}

/// Mirror of the C `struct winuxcmd_io_v3` (src/shared/rust2_abi.h): the
/// ABI v3 stdio callback table. Every callback may be NULL (stdin: empty;
/// stdout/stderr: discard). `flags` carries
/// [`winux_core::stream::IO_STDOUT_LINE_BUFFERED`].
#[repr(C)]
pub struct WinuxcmdIoV3 {
    pub stdin_read: Option<StreamReadFn>,
    pub stdin_user_data: *mut c_void,
    pub stdout_write: Option<StreamWriteFn>,
    pub stdout_user_data: *mut c_void,
    pub stderr_write: Option<StreamWriteFn>,
    pub stderr_user_data: *mut c_void,
    pub flags: u32,
}

/// ABI surface version, bumped on signature changes. 3 since the streaming
/// output entry `winuxcmd_run_io` was added (see the module docs).
#[no_mangle]
pub extern "C" fn winuxcmd_abi_version() -> c_int {
    3
}

/// Parse the explicit host strings into owned Rust data. `Err(())` on a
/// NULL name/cwd or a NULL argv slot.
unsafe fn parse_host_inputs(
    name: *const c_char,
    argc: c_int,
    argv: *const *const c_char,
    envp: *const *const c_char,
    cwd: *const c_char,
) -> Result<HostInputs, ()> {
    if name.is_null() || cwd.is_null() {
        return Err(());
    }
    let name = CStr::from_ptr(name).to_string_lossy().into_owned();
    let mut args: Vec<std::ffi::OsString> = Vec::new();
    for i in 0..argc as isize {
        let p = *argv.offset(i);
        if p.is_null() {
            return Err(());
        }
        args.push(std::ffi::OsString::from(
            CStr::from_ptr(p).to_string_lossy().into_owned(),
        ));
    }
    let mut env = MapEnv::new();
    if !envp.is_null() {
        let mut i: isize = 0;
        loop {
            let p = *envp.offset(i);
            if p.is_null() {
                break;
            }
            let kv = CStr::from_ptr(p).to_string_lossy().into_owned();
            if let Some(eq) = kv.find('=') {
                env.vars.push((
                    std::ffi::OsString::from(&kv[..eq]),
                    std::ffi::OsString::from(&kv[eq + 1..]),
                ));
            }
            i += 1;
        }
    }
    let cwd = PathBuf::from(CStr::from_ptr(cwd).to_string_lossy().into_owned());
    Ok(HostInputs {
        name,
        args,
        env,
        cwd,
    })
}

/// Run the command over the given stdio handles. `Err(())` = unknown
/// command; `CmdError::Io` (a broken injected handle) maps to 125.
fn dispatch_io(
    h: &HostInputs,
    stdin: &mut dyn Read,
    stdout: &mut dyn Write,
    stderr: &mut dyn Write,
) -> Result<u32, ()> {
    let ctx = Ctx::new(h.cwd.clone(), &h.env);
    let mut io = Io {
        stdin,
        stdout,
        stderr,
    };
    match winux_cmds::dispatch(&h.name, &h.args, &mut io, &ctx) {
        None => Err(()),
        Some(Ok(s)) => Ok(s),
        Some(Err(winux_core::CmdError::Io(_))) => Ok(winux_core::ABI_FAILURE_STATUS),
    }
}

/// Run the command with the given stdin reader, collect stdout/stderr into
/// the host's CBuf slots. `Err(())` = unknown command.
fn dispatch_and_collect(
    h: &HostInputs,
    stdin: &mut dyn Read,
    stdout_buf: *mut CBuf,
    stderr_buf: *mut CBuf,
) -> Result<u32, ()> {
    let mut out: Vec<u8> = Vec::new();
    let mut err_out: Vec<u8> = Vec::new();
    let status = dispatch_io(h, stdin, &mut out, &mut err_out)?;
    unsafe {
        *stdout_buf = CBuf::from_vec(out);
        *stderr_buf = CBuf::from_vec(err_out);
    }
    Ok(status)
}

/// Run command `name` with explicit argv/env/cwd and an explicit whole
/// stdin buffer (ABI v1).
///
/// * `argv` has `argc` C strings (the command's own arguments, argv[0] not
///   included).
/// * `envp` is a NULL-terminated array of `name=value` C strings (may be
///   NULL for an empty environment).
/// * `cwd` is the working directory the command assumes.
/// * `stdin_buf`/`stdin_len` is the input bytes (may be NULL/0).
/// * `stdout_buf`/`stderr_buf` receive the collected bytes; free them with
///   `winuxcmd_free_buf`.
///
/// Returns the command's status code, or 125 on panic / handle IO error /
/// unknown command.
#[no_mangle]
pub unsafe extern "C" fn winuxcmd_run(
    name: *const c_char,
    argc: c_int,
    argv: *const *const c_char,
    envp: *const *const c_char,
    cwd: *const c_char,
    stdin_buf: *const u8,
    stdin_len: usize,
    stdout_buf: *mut CBuf,
    stderr_buf: *mut CBuf,
) -> c_int {
    let run = || -> Result<i32, ()> {
        if stdout_buf.is_null() || stderr_buf.is_null() {
            return Err(());
        }
        let h = parse_host_inputs(name, argc, argv, envp, cwd)?;
        let input: Vec<u8> = if stdin_buf.is_null() || stdin_len == 0 {
            Vec::new()
        } else {
            std::slice::from_raw_parts(stdin_buf, stdin_len).to_vec()
        };
        let mut reader = std::io::Cursor::new(input);
        let status = dispatch_and_collect(&h, &mut reader, stdout_buf, stderr_buf)?;
        Ok(status as i32)
    };

    match catch_unwind(AssertUnwindSafe(run)) {
        Ok(Ok(s)) => s,
        Ok(Err(())) => winux_core::ABI_FAILURE_STATUS as c_int,
        Err(_) => winux_core::ABI_FAILURE_STATUS as c_int, // panic contained
    }
}

/// Run command `name` with stdin arriving through a host pull callback
/// (ABI v2, additive; #1488 #1550). See the module docs and
/// rust2/README.md "Streaming stdin ABI" for the full contract.
///
/// * `stdin_read`/`stdin_user_data` — the host's "fill this buffer"
///   callback and its opaque context. NULL `stdin_read` means empty stdin
///   (same as `stdin_buf == NULL` in v1). The callback returns bytes
///   filled (partial reads legal), 0 on EOF, -1 when the host stops
///   feeding (timeout), -2 on host read failure.
/// * stdout/stderr keep the v1 shape: Rust-owned buffers freed with
///   `winuxcmd_free_buf`.
///
/// Status: the command's status code; **124 when the host returned -1**
/// (GNU `timeout(1)` convention) even if the command swallowed the read
/// error; 125 on panic / handle IO error / unknown command.
///
/// Buffer ownership: the callback's `buf` is host-owned and borrowed only
/// for the duration of one invocation; Rust never stores the pointer.
/// Threading: the callback is invoked synchronously on the caller's thread
/// (single-threaded model; the wrapper is `!Send + !Sync`).
#[no_mangle]
pub unsafe extern "C" fn winuxcmd_run_stream(
    name: *const c_char,
    argc: c_int,
    argv: *const *const c_char,
    envp: *const *const c_char,
    cwd: *const c_char,
    stdin_read: Option<StreamReadFn>,
    stdin_user_data: *mut std::ffi::c_void,
    stdout_buf: *mut CBuf,
    stderr_buf: *mut CBuf,
) -> c_int {
    let run = || -> Result<(i32, bool), ()> {
        if stdout_buf.is_null() || stderr_buf.is_null() {
            return Err(());
        }
        let h = parse_host_inputs(name, argc, argv, envp, cwd)?;
        let mut empty: &[u8] = &[];
        let mut reader = stdin_read.map(|f| StreamReader::new(f, stdin_user_data));
        let stdin: &mut dyn Read = match reader.as_mut() {
            Some(r) => r,
            None => &mut empty,
        };
        let status = dispatch_and_collect(&h, stdin, stdout_buf, stderr_buf)?;
        // The flag is read AFTER dispatch: the callback fires while the
        // command reads, so nothing can be known before the run.
        let timed_out = reader.as_ref().map(|r| r.timed_out()).unwrap_or(false);
        Ok((status as i32, timed_out))
    };

    match catch_unwind(AssertUnwindSafe(run)) {
        Ok(Ok((s, timed_out))) => {
            if timed_out {
                winux_core::stream::STREAM_TIMEOUT_STATUS as c_int
            } else {
                s
            }
        }
        Ok(Err(())) => winux_core::ABI_FAILURE_STATUS as c_int,
        Err(_) => winux_core::ABI_FAILURE_STATUS as c_int, // panic contained
    }
}

/// Run command `name` with every stdio stream through host callbacks
/// (ABI v3, additive; #1488). See rust2/README.md "Streaming output ABI
/// (v3)" for the full contract.
///
/// * `io` — the callback table ([`WinuxcmdIoV3`]); NULL `io` behaves like
///   an all-NULL table (empty stdin, discarded output).
/// * stdin: the v2 pull callback, same contract (`-1` -> status 124).
/// * stdout: the host write callback behind a line buffer when
///   `flags & IO_STDOUT_LINE_BUFFERED` (one callback per complete line),
///   else a 64 KiB block buffer; flushed after the command returns (and
///   best-effort after a contained panic).
/// * stderr: unbuffered — every command write is one callback.
/// * A write callback returning `-1` surfaces to the command as
///   `io::ErrorKind::BrokenPipe`, `-2` as `io::Error::other`; the failure
///   is sticky and the host is never called again on that stream.
///
/// Status: same as v2 — the command's own status, 124 when the stdin
/// callback reported `-1`, 125 on panic / handle IO error / unknown
/// command. The write-failure status mapping (SIGPIPE-death 141, the
/// closed-stdout "write error" diagnostic) is the HOST's: it saw every
/// callback result.
///
/// Ownership: the bytes handed to a write callback are Rust-owned and
/// borrowed for the duration of that one call; the host copies or writes
/// them before returning and never stores the pointer. Threading: every
/// callback is invoked synchronously on the caller's thread.
#[no_mangle]
pub unsafe extern "C" fn winuxcmd_run_io(
    name: *const c_char,
    argc: c_int,
    argv: *const *const c_char,
    envp: *const *const c_char,
    cwd: *const c_char,
    io: *const WinuxcmdIoV3,
) -> c_int {
    let (stdin_read, stdin_ud, stdout_write, stdout_ud, stderr_write, stderr_ud, flags) =
        if io.is_null() {
            (
                None,
                std::ptr::null_mut(),
                None,
                std::ptr::null_mut(),
                None,
                std::ptr::null_mut(),
                0u32,
            )
        } else {
            let t = &*io;
            (
                t.stdin_read,
                t.stdin_user_data,
                t.stdout_write,
                t.stdout_user_data,
                t.stderr_write,
                t.stderr_user_data,
                t.flags,
            )
        };
    // The writers live OUTSIDE the unwind guard so a contained panic can
    // still flush the stdout buffer best-effort below.
    let mut stdout = winux_core::stream::stdout_writer(stdout_write, stdout_ud, flags);
    let mut stderr = HostWriter::new(stderr_write, stderr_ud);
    let mut reader = stdin_read.map(|f| StreamReader::new(f, stdin_ud));

    let run = || -> Result<(i32, bool), ()> {
        let h = parse_host_inputs(name, argc, argv, envp, cwd)?;
        let mut empty: &[u8] = &[];
        let stdin: &mut dyn Read = match reader.as_mut() {
            Some(r) => r,
            None => &mut empty,
        };
        let status = dispatch_io(&h, stdin, &mut stdout, &mut stderr);
        // Flush before reporting, unknown command included (nothing was
        // written then; the flush is a no-op). A failed flush was already
        // reported to the host through the callback's own return value.
        let _ = stdout.flush();
        let status = status?;
        let timed_out = reader.as_ref().map(|r| r.timed_out()).unwrap_or(false);
        Ok((status as i32, timed_out))
    };

    match catch_unwind(AssertUnwindSafe(run)) {
        Ok(Ok((s, timed_out))) => {
            if timed_out {
                winux_core::stream::STREAM_TIMEOUT_STATUS as c_int
            } else {
                s
            }
        }
        Ok(Err(())) => winux_core::ABI_FAILURE_STATUS as c_int,
        Err(_) => {
            // Panic contained: hand the host what the command had already
            // produced, best-effort (a panic inside the flush is contained
            // too and ignored).
            let _ = catch_unwind(AssertUnwindSafe(|| {
                let _ = stdout.flush();
            }));
            winux_core::ABI_FAILURE_STATUS as c_int
        }
    }
}

/// Free a buffer handed out by `winuxcmd_run` / `winuxcmd_run_stream`.
#[no_mangle]
pub unsafe extern "C" fn winuxcmd_free_buf(buf: *mut CBuf) {
    if buf.is_null() {
        return;
    }
    let b = &mut *buf;
    if !b.ptr.is_null() && b.cap > 0 {
        let vec = Vec::from_raw_parts(b.ptr, b.len, b.cap);
        drop(vec);
    }
    b.ptr = std::ptr::null_mut();
    b.len = 0;
    b.cap = 0;
}

/// Convenience for hosts: check whether a command name exists in the
/// registry.
#[no_mangle]
pub extern "C" fn winuxcmd_has_command(name: *const c_char) -> c_int {
    if name.is_null() {
        return 0;
    }
    let name = unsafe { CStr::from_ptr(name).to_string_lossy().into_owned() };
    let names: Vec<&str> = winux_cmds::registry().into_iter().map(|(n, _)| n).collect();
    if names.contains(&name.as_str()) {
        1
    } else {
        0
    }
}
