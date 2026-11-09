//! winuxcmd-ffi: the C ABI surface for in-process (host-side) invocation.
//!
//! ABI rules per #1488 ("ABI 面只传显式句柄/缓冲，不传环境状态"):
//! * everything the command sees arrives as arguments: argv array, an
//!   explicit `envp` array (`name=value`), an explicit cwd string, and
//!   explicit buffers for stdin/stdout/stderr bytes;
//! * the boundary is `catch_unwind`-guarded: a panicking command is
//!   converted to status [`winux_core::ABI_FAILURE_STATUS`] (125) instead of
//!   unwinding into a foreign caller;
//! * no CRT fd binding, no process stdio access, no thread-locals.
//!
//! Status contract: `Ok(n)` -> n; `Err(CmdError::Io)` -> 125 as well (the
//! host injected a broken handle); panic -> 125.

use std::ffi::{c_char, c_int, CStr};
use std::panic::{catch_unwind, AssertUnwindSafe};

use winux_core::{Ctx, Io, MapEnv};

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

/// ABI surface version, bumped on signature changes.
#[no_mangle]
pub extern "C" fn winuxcmd_abi_version() -> c_int {
    1
}

/// Run command `name` with explicit argv/env/cwd and explicit buffers.
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
        if name.is_null() || stdout_buf.is_null() || stderr_buf.is_null() {
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
        let cwd_path = if cwd.is_null() {
            return Err(());
        } else {
            std::path::PathBuf::from(CStr::from_ptr(cwd).to_string_lossy().into_owned())
        };
        let ctx = Ctx {
            cwd: cwd_path,
            env: &env,
        };

        let input: Vec<u8> = if stdin_buf.is_null() || stdin_len == 0 {
            Vec::new()
        } else {
            std::slice::from_raw_parts(stdin_buf, stdin_len).to_vec()
        };

        let mut reader = std::io::Cursor::new(input);
        let mut out: Vec<u8> = Vec::new();
        let mut err_out: Vec<u8> = Vec::new();
        let status = {
            let mut io = Io {
                stdin: &mut reader,
                stdout: &mut out,
                stderr: &mut err_out,
            };
            match winux_cmds::dispatch(&name, &args, &mut io, &ctx) {
                None => return Err(()),
                Some(Ok(s)) => s,
                Some(Err(winux_core::CmdError::Io(_))) => winux_core::ABI_FAILURE_STATUS,
            }
        };
        *stdout_buf = CBuf::from_vec(out);
        *stderr_buf = CBuf::from_vec(err_out);
        Ok(status as i32)
    };

    match catch_unwind(AssertUnwindSafe(run)) {
        Ok(Ok(s)) => s,
        Ok(Err(())) => winux_core::ABI_FAILURE_STATUS as c_int,
        Err(_) => winux_core::ABI_FAILURE_STATUS as c_int, // panic contained
    }
}

/// Free a buffer handed out by `winuxcmd_run`.
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
