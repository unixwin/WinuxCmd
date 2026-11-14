// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
//! Host demo for the 2.0 cmdlib pilot (issue #1488) — the injection side.
//!
//! This binary deliberately does NOT link the command library: it
//! `LoadLibraryW`s the built cdylib the way a real shell host would, then
//! calls `wcmd_pwd` with a context whose
//!   - stdout / stderr are pipe write-ends created here,
//!   - cwd is a directory DIFFERENT from this process's working directory,
//!   - env block carries a chosen `PWD` / `POSIXLY_CORRECT`.
//!
//! If the library honors the contract (DOCS/2.0-cmdlib-design.md §2), every
//! byte pwd produces lands in the pipes — nothing reaches this process's
//! stdout — and the printed path is the injected cwd. That is exactly the
//! shape that was broken in #1487 defect A (`sort words.txt > o.txt` lost
//! its output to the terminal).
//!
//! Leak proof (run outside): redirect THIS process's stdout to a file; every
//! line in it must start with `[host]`. Any pwd output that leaked into the
//! process stdout appears as a bare path line and fails the grep.

use std::ffi::{c_char, c_void, CString};
use std::fs::{self, File};
use std::io::Read;
use std::os::windows::ffi::OsStrExt;
use std::os::windows::io::FromRawHandle;
use std::path::{Path, PathBuf};

type Handle = *mut c_void;

/// Mirror of `WcmdIoCtx` from `src/lib.rs` (ABI v0). The host owns its own
/// copy of the ABI header — the product form is a shared `wcmd_abi.h`
/// (design §3.1). Keep in sync with `src/lib.rs`.
#[repr(C)]
struct WcmdIoCtx {
    stdin: Handle,
    stdout: Handle,
    stderr: Handle,
    env_block: *const c_char,
    cwd: *const c_char,
    argc: i32,
    argv: *const *const c_char,
    cancel_flag: *const u32,
}

extern "system" {
    fn CreatePipe(read: *mut Handle, write: *mut Handle, sa: *const c_void, size: u32) -> i32;
    fn LoadLibraryW(name: *const u16) -> Handle;
    fn GetProcAddress(module: Handle, name: *const u8) -> *const c_void;
    fn CloseHandle(handle: Handle) -> i32;
}

type PwdFn = unsafe extern "C" fn(*const WcmdIoCtx) -> i32;
type AbiVersionFn = unsafe extern "C" fn() -> i32;

struct Pipe {
    read: Handle,
    write: Handle,
}

fn make_pipe() -> Pipe {
    let mut read: Handle = std::ptr::null_mut();
    let mut write: Handle = std::ptr::null_mut();
    let ok = unsafe { CreatePipe(&mut read, &mut write, std::ptr::null(), 0) };
    assert_eq!(ok, 1, "CreatePipe failed");
    Pipe { read, write }
}

/// Take ownership of the read end, drain it, and close it.
fn drain_and_close(handle: Handle) -> Vec<u8> {
    let mut file = unsafe { File::from_raw_handle(handle) };
    let mut buf = Vec::new();
    let _ = file.read_to_end(&mut buf);
    buf
}

/// Keeps every allocation the borrowed `WcmdIoCtx` pointers point into
/// alive for the duration of the call (design §2.2 borrowing rules).
struct OwnedCtx {
    env: Vec<u8>,
    argv: Vec<CString>,
    argv_ptrs: Vec<*const c_char>,
    cwd: CString,
    cancel: Box<u32>,
}

fn build_ctx(
    argv: &[&str],
    env: &[String],
    cwd: &Path,
    stdout: Handle,
    stderr: Handle,
) -> (WcmdIoCtx, OwnedCtx) {
    let mut env_buf: Vec<u8> = Vec::new();
    for entry in env {
        env_buf.extend_from_slice(entry.as_bytes());
        env_buf.push(0);
    }
    env_buf.push(0); // trailing NUL: end of the double-NUL-terminated block

    let argv_c: Vec<CString> = argv.iter().map(|a| CString::new(*a).unwrap()).collect();
    let argv_ptrs: Vec<*const c_char> = argv_c.iter().map(|c| c.as_ptr()).collect();

    let cwd_c = CString::new(cwd.to_string_lossy().into_owned().as_bytes()).unwrap();
    let cancel = Box::new(0u32);

    let owned = OwnedCtx {
        env: env_buf,
        argv: argv_c,
        argv_ptrs,
        cwd: cwd_c,
        cancel,
    };
    let ctx = WcmdIoCtx {
        stdin: std::ptr::null_mut(), // pwd reads no input: NULL = stream not provided
        stdout,
        stderr,
        env_block: owned.env.as_ptr() as *const c_char,
        cwd: owned.cwd.as_ptr(),
        argc: owned.argv.len() as i32,
        argv: owned.argv_ptrs.as_ptr(),
        cancel_flag: &*owned.cancel,
    };
    (ctx, owned)
}

struct CaseResult {
    rc: i32,
    stdout: String,
    stderr: String,
}

fn run_case(pwd: PwdFn, name: &str, argv: &[&str], env: &[String], cwd: &Path) -> CaseResult {
    let out_pipe = make_pipe();
    let err_pipe = make_pipe();
    let (ctx, _owned) = build_ctx(argv, env, cwd, out_pipe.write, err_pipe.write);
    let rc = unsafe { pwd(&ctx) };
    // The library only borrowed the write ends; closing them here is what
    // makes the pipes readable to EOF.
    unsafe {
        CloseHandle(out_pipe.write);
        CloseHandle(err_pipe.write);
    }
    let stdout = String::from_utf8_lossy(&drain_and_close(out_pipe.read)).into_owned();
    let stderr = String::from_utf8_lossy(&drain_and_close(err_pipe.read)).into_owned();
    println!(
        "[host] case {name}: rc={rc} stdout={}B stderr={}B",
        stdout.len(),
        stderr.len()
    );
    if !stdout.is_empty() {
        println!("[host]   piped stdout: {stdout:?}");
    }
    if !stderr.is_empty() {
        println!("[host]   piped stderr: {stderr:?}");
    }
    CaseResult { rc, stdout, stderr }
}

fn load_library() -> (PathBuf, AbiVersionFn, PwdFn) {
    let dll = match std::env::var_os("WCMD_PILOT_DLL") {
        Some(p) => PathBuf::from(p),
        None => {
            let dir = std::env::current_exe()
                .unwrap()
                .parent()
                .unwrap()
                .to_path_buf();
            dir.join("wcmd_cmdlib.dll")
        }
    };
    assert!(
        dll.exists(),
        "command library not found: {} (build it first, or set WCMD_PILOT_DLL)",
        dll.display()
    );
    let wide: Vec<u16> = dll
        .as_os_str()
        .encode_wide()
        .chain(std::iter::once(0))
        .collect();
    let module = unsafe { LoadLibraryW(wide.as_ptr()) };
    assert!(
        !module.is_null(),
        "LoadLibraryW failed for {}",
        dll.display()
    );
    let abi_addr = unsafe { GetProcAddress(module, c"wcmd_abi_version".as_ptr() as *const u8) };
    let pwd_addr = unsafe { GetProcAddress(module, c"wcmd_pwd".as_ptr() as *const u8) };
    assert!(
        !abi_addr.is_null() && !pwd_addr.is_null(),
        "GetProcAddress missed an export"
    );
    let abi_fn: AbiVersionFn = unsafe { std::mem::transmute(abi_addr) };
    let pwd_fn: PwdFn = unsafe { std::mem::transmute(pwd_addr) };
    (dll, abi_fn, pwd_fn)
}

fn main() {
    let (dll, abi_fn, pwd) = load_library();
    let abi = unsafe { abi_fn() };
    println!(
        "[host] loaded {} (LoadLibraryW, no link-time dependency)",
        dll.display()
    );
    println!("[host] wcmd_abi_version() = {abi}");

    // The injected cwd is a fresh temp directory, deliberately NOT this
    // process's working directory: if the library escaped to the process
    // cwd, the output would be the wrong directory and the check below
    // fails. This targets the third #1487 pit (cached process handles)
    // specifically.
    let mut fake_cwd: PathBuf = std::env::temp_dir().join("wcmd-pilot-cwd");
    let mut n = 0;
    while std::env::current_dir().unwrap() == fake_cwd {
        n += 1;
        fake_cwd = std::env::temp_dir().join(format!("wcmd-pilot-cwd.{n}"));
    }
    fs::create_dir_all(&fake_cwd).unwrap();
    let fake_cwd_s = fake_cwd.to_string_lossy().into_owned();
    let process_cwd = std::env::current_dir().unwrap();
    let env_pwd = format!("PWD={fake_cwd_s}");

    println!("[host] injected cwd : {fake_cwd_s}");
    println!(
        "[host] process  cwd : {} (must NOT appear in any pwd output)",
        process_cwd.display()
    );
    println!("[host] process stdout of THIS host is only [host] marker lines; pwd bytes must all arrive through the injected pipes");

    let mut failures = 0usize;
    let mut check = |ok: bool, what: &str| {
        println!("[host]   {} {what}", if ok { "PASS" } else { "FAIL" });
        if !ok {
            failures += 1;
        }
    };

    // A. Default (physical): output == injected cwd, not the process cwd —
    //    proving no process-cwd read and no process-stdout write.
    let r = run_case(
        pwd,
        "physical-default",
        &["pwd"],
        &[env_pwd.clone()],
        &fake_cwd,
    );
    check(r.rc == 0, "rc == 0");
    check(
        r.stdout.trim_end().eq_ignore_ascii_case(&fake_cwd_s) && r.stdout.ends_with('\n'),
        "piped stdout == injected cwd + newline",
    );
    check(
        !r.stdout
            .to_lowercase()
            .contains(&process_cwd.to_string_lossy().to_lowercase()),
        "piped stdout does NOT contain the process cwd",
    );
    check(r.stderr.is_empty(), "piped stderr empty");

    // B. Explicit -L with a valid injected PWD: prints the env value
    //    verbatim — proving the PWD read went through ctx.env_block.
    let r = run_case(
        pwd,
        "dash-L-valid-pwd",
        &["pwd", "-L"],
        &[env_pwd.clone()],
        &fake_cwd,
    );
    check(
        r.rc == 0 && r.stdout == format!("{fake_cwd_s}\n"),
        "piped stdout == injected PWD value verbatim",
    );

    // C. POSIXLY_CORRECT present but EMPTY still flips the default to
    //    logical (pwd.cpp:62-65 presence test, [GNU] pwd.c:331).
    let r = run_case(
        pwd,
        "posixly-correct-empty-defaults-logical",
        &["pwd"],
        &[env_pwd.clone(), "POSIXLY_CORRECT=".to_string()],
        &fake_cwd,
    );
    check(
        r.rc == 0 && r.stdout == format!("{fake_cwd_s}\n"),
        "empty-but-present POSIXLY_CORRECT defaults to logical",
    );

    // D. Non-option operands: stdout still the directory, the warning goes
    //    to the injected stderr pipe (third handle), rc stays 0.
    let r = run_case(
        pwd,
        "operand-warning-to-injected-stderr",
        &["pwd", "extra"],
        &[env_pwd.clone()],
        &fake_cwd,
    );
    check(r.rc == 0, "rc == 0 with operands");
    check(
        r.stdout.trim_end().eq_ignore_ascii_case(&fake_cwd_s),
        "piped stdout == injected cwd",
    );
    check(
        r.stderr == "pwd: ignoring non-option arguments\n",
        "warning arrived through the injected stderr pipe",
    );

    // E. Invalid option: rc 2, diagnostics on the injected stderr, stdout
    //    untouched (diagnostics text is SIMPLIFIED pilot scope — see
    //    src/lib.rs parse_args).
    let r = run_case(pwd, "invalid-option", &["pwd", "-Z"], &[], &fake_cwd);
    check(r.rc == 2, "rc == 2 for invalid option");
    check(r.stdout.is_empty(), "piped stdout empty on usage error");
    check(
        r.stderr.contains("invalid option"),
        "usage error text on injected stderr",
    );

    println!("[host] {} cases run, {} failure(s)", 5, failures);
    if failures == 0 {
        println!("[host] ALL PASS: every pwd byte went to the injected pipes; nothing touched process stdio, process cwd, or process env");
    }
    // Keep the temp cwd around for post-run inspection; it is recreated on
    // the next run and harmless to leave behind.
    std::process::exit(if failures == 0 { 0 } else { 1 });
}
