//! winuxcmd-ffi: the C ABI surface for in-process (host-side) invocation.
//!
//! ABI rules per #1488 ("ABI 面只传显式句柄/缓冲，不传环境状态"):
//! * everything the command sees arrives as arguments: argv array, an
//!   explicit `envp` array (`name=value`), an explicit cwd string, and
//!   explicit buffers (v1: stdin whole-buffer; v2: a streaming pull
//!   callback, see [`winuxcmd_run_stream`]) for stdin/stdout/stderr bytes;
//! * the boundary is `catch_unwind`-guarded: a panicking command is
//!   converted to status [`winux_core::ABI_FAILURE_STATUS`] (125) instead of
//!   unwinding into a foreign caller;
//! * no CRT fd binding, no process stdio access, no thread-locals — the
//!   #1487 lesson. The streaming entry (v2) keeps the same red line: the
//!   host reads its own console/pipe and hands over plain byte buffers
//!   through the callback; Rust sees only buffer contents.
//!
//! Status contract: `Ok(n)` -> n; `Err(CmdError::Io)` -> 125 as well (the
//! host injected a broken handle); panic -> 125; v2 host-stops-feeding ->
//! 124 (`winux_core::stream::STREAM_TIMEOUT_STATUS`).
//!
//! Versioning: `winuxcmd_abi_version()` stays 1 while v2 is additive —
//! the C++ bridge refuses on a version mismatch, so the bump to 2 happens
//! in the commit that adopts `winuxcmd_run_stream` on the C++ side
//! (rust2_abi.h + rust2_bridge.cppm together). See rust2/README.md.

use std::ffi::{c_char, c_int, CStr};
use std::io::Read;
use std::panic::{catch_unwind, AssertUnwindSafe};
use std::path::PathBuf;

use winux_core::stream::{StreamReadFn, StreamReader};
use winux_core::{Ctx, Io, MapEnv};

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

/// ABI surface version, bumped on signature changes. 1 while
/// `winuxcmd_run_stream` is additive (see the module docs).
#[no_mangle]
pub extern "C" fn winuxcmd_abi_version() -> c_int {
    1
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

/// Run the command with the given stdin reader, collect stdout/stderr into
/// the host's CBuf slots. `Err(())` = unknown command.
fn dispatch_and_collect(
    h: &HostInputs,
    stdin: &mut dyn Read,
    stdout_buf: *mut CBuf,
    stderr_buf: *mut CBuf,
) -> Result<u32, ()> {
    let ctx = Ctx {
        cwd: h.cwd.clone(),
        env: &h.env,
    };
    let mut out: Vec<u8> = Vec::new();
    let mut err_out: Vec<u8> = Vec::new();
    let status = {
        let mut io = Io {
            stdin,
            stdout: &mut out,
            stderr: &mut err_out,
        };
        match winux_cmds::dispatch(&h.name, &h.args, &mut io, &ctx) {
            None => return Err(()),
            Some(Ok(s)) => s,
            Some(Err(winux_core::CmdError::Io(_))) => winux_core::ABI_FAILURE_STATUS,
        }
    };
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
