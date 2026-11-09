//! In-process ABI smoke tests: exercise winuxcmd_run / winuxcmd_free_buf /
//! winuxcmd_has_command exactly as a foreign (non-Rust) host would — C
//! strings, raw pointers, explicit buffers, catch_unwind boundary.

use std::ffi::{c_char, c_int, CString};

use crate::{winuxcmd_abi_version, winuxcmd_free_buf, winuxcmd_has_command, winuxcmd_run, CBuf};

struct Argv {
    ptrs: Vec<*const c_char>,
    owned: Vec<CString>,
}

impl Argv {
    fn new(args: &[&str]) -> Self {
        let owned: Vec<CString> = args
            .iter()
            .map(|a| CString::new(a.replace('\0', "")).unwrap())
            .collect();
        let ptrs: Vec<*const c_char> = owned.iter().map(|c| c.as_ptr()).collect();
        Argv { ptrs, owned }
    }
}

fn run_case(name: &str, args: &[&str], env: &[&str], cwd: &str) -> (i32, Vec<u8>, Vec<u8>) {
    let cname = CString::new(name).unwrap();
    let c_cwd = CString::new(cwd.replace('\\', "/")).unwrap();
    let argv = Argv::new(args);
    let env_owned: Vec<CString> = env
        .iter()
        .map(|e| CString::new(e.replace('\0', "")).unwrap())
        .collect();
    let mut env_ptrs: Vec<*const c_char> = env_owned.iter().map(|c| c.as_ptr()).collect();
    env_ptrs.push(std::ptr::null());

    let mut out = CBuf::empty();
    let mut err = CBuf::empty();
    let status = unsafe {
        winuxcmd_run(
            cname.as_ptr(),
            args.len() as c_int,
            argv.ptrs.as_ptr(),
            env_ptrs.as_ptr(),
            c_cwd.as_ptr(),
            std::ptr::null(),
            0,
            &mut out,
            &mut err,
        )
    };
    let out_bytes = unsafe {
        let slice = if out.ptr.is_null() {
            &[][..]
        } else {
            std::slice::from_raw_parts(out.ptr, out.len)
        };
        let v = slice.to_vec();
        winuxcmd_free_buf(&mut out);
        v
    };
    let err_bytes = unsafe {
        let slice = if err.ptr.is_null() {
            &[][..]
        } else {
            std::slice::from_raw_parts(err.ptr, err.len)
        };
        let v = slice.to_vec();
        winuxcmd_free_buf(&mut err);
        v
    };
    (status, out_bytes, err_bytes)
}

#[test]
fn abi_version_is_one() {
    assert_eq!(winuxcmd_abi_version(), 1);
}

#[test]
fn registry_lookup() {
    let n = CString::new("basename").unwrap();
    assert_eq!(winuxcmd_has_command(n.as_ptr()), 1);
    let n = CString::new("nosuchcmd").unwrap();
    assert_eq!(winuxcmd_has_command(n.as_ptr()), 0);
}

#[test]
fn basename_through_abi() {
    let cwd = std::env::temp_dir().to_string_lossy().replace('\\', "/");
    let (status, out, err) = run_case("basename", &["a/b/c"], &[], &cwd);
    assert_eq!(status, 0);
    assert_eq!(out, b"c\n");
    assert!(err.is_empty());
}

#[test]
fn test_builtin_status_through_abi() {
    let cwd = std::env::temp_dir().to_string_lossy().replace('\\', "/");
    let (status, _, _) = run_case("test", &["1", "-eq", "2"], &[], &cwd);
    assert_eq!(status, 1);
    let (status, _, _) = run_case("[", &["1", "-eq", "2", "]"], &[], &cwd);
    assert_eq!(status, 1);
    // missing `]` is a syntax error (rc 2)
    let (status, _, _) = run_case("[", &["1", "-eq", "2"], &[], &cwd);
    assert_eq!(status, 2);
}

#[test]
fn printenv_explicit_env_through_abi() {
    let cwd = std::env::temp_dir().to_string_lossy().replace('\\', "/");
    let (status, out, _) = run_case("printenv", &["GFoo"], &["GFoo=bar"], &cwd);
    assert_eq!(status, 0);
    assert_eq!(out, b"bar\n");
}

#[test]
fn unknown_command_is_abi_failure() {
    let cwd = std::env::temp_dir().to_string_lossy().replace('\\', "/");
    let (status, _, _) = run_case("no-such-command", &[], &[], &cwd);
    assert_eq!(status, 125); // ABI_FAILURE_STATUS
}
