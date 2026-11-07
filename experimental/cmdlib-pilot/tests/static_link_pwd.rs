// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
//! Static-form proof (DOCS/2.0-cmdlib-design.md §3, §7): link the command
//! library as an ordinary Rust crate (rlib here; staticlib shares the same
//! ABI face), inject a FILE handle as stdout — the literal shape of
//! `sort words.txt > o.txt` from #1487 defect A — and assert the output
//! lands in the file, byte-exact, while this test process's own stdout is
//! never written by the command.

use std::ffi::{c_char, c_void, CString};
use std::fs;
use std::os::windows::io::{FromRawHandle, IntoRawHandle};
use std::path::PathBuf;

use wcmd_cmdlib::{wcmd_abi_version, wcmd_pwd, WcmdIoCtx};

#[test]
fn pwd_writes_to_injected_file_handle() {
    let dir: PathBuf = fs::canonicalize(std::env::temp_dir()).unwrap();
    let out = dir.join("wcmd-pilot-static-pwd.out");
    let file = fs::File::create(&out).unwrap();
    let stdout_handle = file.into_raw_handle();

    // Everything the borrowed ctx points into must outlive the call.
    let arg0 = CString::new("pwd").unwrap();
    let argv = [arg0];
    let argv_ptrs: Vec<*const c_char> = argv.iter().map(|c| c.as_ptr()).collect();
    let mut env_block: Vec<u8> = vec![0]; // empty block: only the trailing NUL
    let cwd = CString::new(dir.to_string_lossy().into_owned().as_bytes()).unwrap();
    let cancel: u32 = 0;

    let ctx = WcmdIoCtx {
        stdin: std::ptr::null_mut(),
        stdout: stdout_handle as *mut c_void,
        stderr: std::ptr::null_mut(),
        env_block: env_block.as_ptr() as *const c_char,
        cwd: cwd.as_ptr(),
        argc: argv.len() as i32,
        argv: argv_ptrs.as_ptr(),
        cancel_flag: &cancel,
    };

    assert_eq!(
        wcmd_abi_version(),
        0,
        "ABI version must match the host expectation"
    );
    let rc = wcmd_pwd(&ctx);
    assert_eq!(rc, 0, "pwd via injected file handle must succeed");

    // Re-take ownership so the handle closes and the OS file object is
    // complete before reading it back.
    drop(unsafe { <fs::File as FromRawHandle>::from_raw_handle(stdout_handle) });
    let content = fs::read_to_string(&out).unwrap();

    // canonicalize() output minus the \\?\ verbatim prefix — the same
    // normalization the library applies internally (src/lib.rs
    // strip_verbatim). Windows path compare is case-insensitive.
    let expected = dir.to_string_lossy().replace(r"\\?\", "");
    assert_eq!(
        content.to_lowercase(),
        format!("{expected}\n").to_lowercase(),
        "output must be exactly the injected cwd + newline, inside the file"
    );

    let _ = fs::remove_file(&out);
}
