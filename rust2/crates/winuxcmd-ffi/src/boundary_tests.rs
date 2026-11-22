//! Host-boundary contract tests for the C ABI, driven as a foreign host
//! would: raw pointers, NULL slots, explicit env/cwd, both run entries.
//!
//! * NULL name / cwd / argv slot / out-slot -> 125 (ABI_FAILURE_STATUS),
//!   never a crash or a write through the NULL slot;
//! * the explicit `envp` is the whole environment: NULL envp is empty (the
//!   test process environment never leaks in), entries without `=` are
//!   dropped, values keep embedded `=`;
//! * the explicit cwd is what relative operands resolve against;
//! * stdout and stderr are drained into separate buffers on one run;
//! * multi-MiB stdout/stdin round-trips identically through v1 and v2;
//! * `winuxcmd_has_command` agrees with `registry()` and rejects
//!   NULL/empty/case-folded names;
//! * `winuxcmd_grep_uses_perl` follows grep's getopt grammar;
//! * `winuxcmd_free_buf` is NULL- and double-free-safe.

use std::ffi::{c_char, c_int, c_void, CString};

use crate::{
    winuxcmd_free_buf, winuxcmd_grep_uses_perl, winuxcmd_has_command, winuxcmd_run,
    winuxcmd_run_stream, CBuf,
};

const ABI_FAILURE: i32 = 125;

struct CStrs {
    owned: Vec<CString>,
    ptrs: Vec<*const c_char>,
}

impl CStrs {
    fn new(items: &[&[u8]], null_terminated: bool) -> Self {
        let owned: Vec<CString> = items.iter().map(|a| CString::new(*a).unwrap()).collect();
        let mut ptrs: Vec<*const c_char> = owned.iter().map(|c| c.as_ptr()).collect();
        if null_terminated {
            ptrs.push(std::ptr::null());
        }
        CStrs { owned, ptrs }
    }

    fn strs(items: &[&str], null_terminated: bool) -> Self {
        let bytes: Vec<&[u8]> = items.iter().map(|s| s.as_bytes()).collect();
        Self::new(&bytes, null_terminated)
    }

    fn len(&self) -> c_int {
        self.owned.len() as c_int
    }
}

fn drain(buf: &mut CBuf) -> Vec<u8> {
    unsafe {
        let v = if buf.ptr.is_null() {
            assert_eq!(buf.len, 0, "NULL buffer must report len 0");
            Vec::new()
        } else {
            std::slice::from_raw_parts(buf.ptr, buf.len).to_vec()
        };
        winuxcmd_free_buf(buf);
        v
    }
}

fn temp_cwd() -> String {
    std::env::temp_dir().to_string_lossy().replace('\\', "/")
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Entry {
    V1,
    V2,
}

struct Call<'a> {
    name: &'a str,
    args: &'a [&'a str],
    env: Option<&'a [&'a str]>,
    cwd: String,
    stdin: &'a [u8],
}

impl<'a> Call<'a> {
    fn new(name: &'a str, args: &'a [&'a str]) -> Self {
        Call {
            name,
            args,
            env: Some(&[]),
            cwd: temp_cwd(),
            stdin: &[],
        }
    }
}

struct SliceFeeder<'a> {
    data: &'a [u8],
    pos: usize,
}

unsafe extern "C" fn slice_feed(ud: *mut c_void, buf: *mut u8, cap: usize) -> i64 {
    let f = &mut *(ud as *mut SliceFeeder);
    let n = (f.data.len() - f.pos).min(cap).min(65536);
    std::ptr::copy_nonoverlapping(f.data.as_ptr().add(f.pos), buf, n);
    f.pos += n;
    n as i64
}

fn run(entry: Entry, c: &Call) -> (i32, Vec<u8>, Vec<u8>) {
    let name = CString::new(c.name).unwrap();
    let argv = CStrs::strs(c.args, false);
    let env = c.env.map(|e| CStrs::strs(e, true));
    let envp = env.as_ref().map_or(std::ptr::null(), |e| e.ptrs.as_ptr());
    let cwd = CString::new(c.cwd.as_str()).unwrap();
    let mut out = CBuf::empty();
    let mut err = CBuf::empty();
    let status = unsafe {
        match entry {
            Entry::V1 => winuxcmd_run(
                name.as_ptr(),
                argv.len(),
                argv.ptrs.as_ptr(),
                envp,
                cwd.as_ptr(),
                if c.stdin.is_empty() {
                    std::ptr::null()
                } else {
                    c.stdin.as_ptr()
                },
                c.stdin.len(),
                &mut out,
                &mut err,
            ),
            Entry::V2 => {
                let mut feeder = SliceFeeder {
                    data: c.stdin,
                    pos: 0,
                };
                winuxcmd_run_stream(
                    name.as_ptr(),
                    argv.len(),
                    argv.ptrs.as_ptr(),
                    envp,
                    cwd.as_ptr(),
                    Some(slice_feed),
                    &mut feeder as *mut SliceFeeder as *mut c_void,
                    &mut out,
                    &mut err,
                )
            }
        }
    };
    (status, drain(&mut out), drain(&mut err))
}

fn both(c: &Call) -> (i32, Vec<u8>, Vec<u8>) {
    let v1 = run(Entry::V1, c);
    let v2 = run(Entry::V2, c);
    assert_eq!(v1, v2, "v1 and v2 disagree for {} {:?}", c.name, c.args);
    v1
}

/// Raw call with caller-chosen (possibly NULL) pointers for every slot.
unsafe fn raw_run(
    entry: Entry,
    name: *const c_char,
    argc: c_int,
    argv: *const *const c_char,
    cwd: *const c_char,
    out: *mut CBuf,
    err: *mut CBuf,
) -> i32 {
    match entry {
        Entry::V1 => winuxcmd_run(
            name,
            argc,
            argv,
            std::ptr::null(),
            cwd,
            std::ptr::null(),
            0,
            out,
            err,
        ),
        Entry::V2 => winuxcmd_run_stream(
            name,
            argc,
            argv,
            std::ptr::null(),
            cwd,
            None,
            std::ptr::null_mut(),
            out,
            err,
        ),
    }
}

// ---------- NULL slots ----------

#[test]
fn null_host_pointers_are_abi_failure() {
    let name = CString::new("true").unwrap();
    let cwd = CString::new(temp_cwd()).unwrap();
    let argv = CStrs::strs(&[], false);
    for entry in [Entry::V1, Entry::V2] {
        let mut out = CBuf::empty();
        let mut err = CBuf::empty();
        unsafe {
            // control: the same call with every slot valid succeeds
            let rc = raw_run(
                entry,
                name.as_ptr(),
                0,
                argv.ptrs.as_ptr(),
                cwd.as_ptr(),
                &mut out,
                &mut err,
            );
            assert_eq!(rc, 0, "{entry:?} control");
            let rc = raw_run(
                entry,
                std::ptr::null(),
                0,
                argv.ptrs.as_ptr(),
                cwd.as_ptr(),
                &mut out,
                &mut err,
            );
            assert_eq!(rc, ABI_FAILURE, "{entry:?} NULL name");
            let rc = raw_run(
                entry,
                name.as_ptr(),
                0,
                argv.ptrs.as_ptr(),
                std::ptr::null(),
                &mut out,
                &mut err,
            );
            assert_eq!(rc, ABI_FAILURE, "{entry:?} NULL cwd");
            let rc = raw_run(
                entry,
                name.as_ptr(),
                0,
                argv.ptrs.as_ptr(),
                cwd.as_ptr(),
                std::ptr::null_mut(),
                &mut err,
            );
            assert_eq!(rc, ABI_FAILURE, "{entry:?} NULL stdout slot");
            let rc = raw_run(
                entry,
                name.as_ptr(),
                0,
                argv.ptrs.as_ptr(),
                cwd.as_ptr(),
                &mut out,
                std::ptr::null_mut(),
            );
            assert_eq!(rc, ABI_FAILURE, "{entry:?} NULL stderr slot");
        }
        assert!(drain(&mut out).is_empty());
        assert!(drain(&mut err).is_empty());
    }
}

#[test]
fn null_argv_slot_is_abi_failure() {
    let name = CString::new("echo").unwrap();
    let cwd = CString::new(temp_cwd()).unwrap();
    let a = CString::new("x").unwrap();
    let argv: [*const c_char; 2] = [a.as_ptr(), std::ptr::null()];
    for entry in [Entry::V1, Entry::V2] {
        let mut out = CBuf::empty();
        let mut err = CBuf::empty();
        let rc = unsafe {
            raw_run(
                entry,
                name.as_ptr(),
                2,
                argv.as_ptr(),
                cwd.as_ptr(),
                &mut out,
                &mut err,
            )
        };
        assert_eq!(rc, ABI_FAILURE, "{entry:?}");
        // the command never ran, so nothing was produced
        assert!(drain(&mut out).is_empty());
        assert!(drain(&mut err).is_empty());
    }
}

#[test]
fn zero_argc_accepts_null_argv() {
    let name = CString::new("true").unwrap();
    let cwd = CString::new(temp_cwd()).unwrap();
    for entry in [Entry::V1, Entry::V2] {
        let mut out = CBuf::empty();
        let mut err = CBuf::empty();
        let rc = unsafe {
            raw_run(
                entry,
                name.as_ptr(),
                0,
                std::ptr::null(),
                cwd.as_ptr(),
                &mut out,
                &mut err,
            )
        };
        assert_eq!(rc, 0, "{entry:?}");
        assert!(drain(&mut out).is_empty());
        assert!(drain(&mut err).is_empty());
    }
}

// ---------- argv ----------

#[test]
fn argv_is_passed_verbatim_without_argv0() {
    let args = ["-n", "a b", "", "--", "-e", "\t"];
    let (rc, out, err) = both(&Call::new("echo", &args));
    assert_eq!(rc, 0);
    // echo -n: no trailing newline; every operand (including the empty one,
    // "--" and "-e" after an operand) is echoed in order
    assert_eq!(out, b"a b  -- -e \t");
    assert!(err.is_empty());
}

#[test]
fn non_utf8_argv_is_lossy_not_fatal() {
    let name = CString::new("basename").unwrap();
    let cwd = CString::new(temp_cwd()).unwrap();
    let argv = CStrs::new(&[b"dir/a\xffb"], false);
    let mut out = CBuf::empty();
    let mut err = CBuf::empty();
    let rc = unsafe {
        raw_run(
            Entry::V1,
            name.as_ptr(),
            1,
            argv.ptrs.as_ptr(),
            cwd.as_ptr(),
            &mut out,
            &mut err,
        )
    };
    assert_eq!(rc, 0);
    assert_eq!(drain(&mut out), "a\u{FFFD}b\n".as_bytes());
    assert!(drain(&mut err).is_empty());
}

// ---------- envp ----------

#[test]
fn null_envp_is_an_empty_environment() {
    // The test process certainly has PATH; the ABI must not consult it.
    assert!(std::env::var_os("PATH").is_some());
    let mut c = Call::new("printenv", &["PATH"]);
    c.env = None;
    let (rc, out, _) = both(&c);
    assert_eq!(rc, 1);
    assert!(out.is_empty());

    let mut c = Call::new("printenv", &[]);
    c.env = None;
    let (rc, out, err) = both(&c);
    assert_eq!(rc, 0);
    assert!(
        out.is_empty(),
        "printenv saw {:?}",
        String::from_utf8_lossy(&out)
    );
    assert!(err.is_empty());
}

#[test]
fn envp_is_the_whole_environment() {
    let env = ["WXA=1", "NOEQUALS", "WXB=x=y=z", "WXEMPTY="];
    let mut c = Call::new("printenv", &[]);
    c.env = Some(&env);
    let (rc, out, _) = both(&c);
    assert_eq!(rc, 0);
    assert_eq!(out, b"WXA=1\nWXB=x=y=z\nWXEMPTY=\n");

    let mut c = Call::new("printenv", &["WXB", "WXEMPTY", "NOEQUALS"]);
    c.env = Some(&env);
    let (rc, out, _) = both(&c);
    // the entry without '=' is not a variable: one name missing -> rc 1
    assert_eq!(rc, 1);
    assert_eq!(out, b"x=y=z\n\n");
}

// ---------- cwd ----------

#[test]
fn explicit_cwd_anchors_relative_operands() {
    let base = std::env::temp_dir().join(format!("wx-ffi-cwd-{}", std::process::id()));
    let sub = base.join("sub");
    std::fs::create_dir_all(&sub).unwrap();
    std::fs::write(sub.join("f.txt"), b"x").unwrap();
    let cwd = base.to_string_lossy().replace('\\', "/");

    let mut c = Call::new("readlink", &["-e", "sub/f.txt"]);
    c.cwd = cwd.clone();
    let (rc, out, err) = both(&c);
    assert_eq!(rc, 0, "stderr: {}", String::from_utf8_lossy(&err));
    let got = String::from_utf8_lossy(&out)
        .trim_end()
        .replace('\\', "/")
        .to_lowercase();
    assert!(got.ends_with("/sub/f.txt"), "got {got}");

    // the same relative operand from a different cwd does not exist
    let mut c = Call::new("readlink", &["-e", "sub/f.txt"]);
    c.cwd = sub.to_string_lossy().replace('\\', "/");
    let (rc, out, _) = both(&c);
    assert_eq!(rc, 1);
    assert!(out.is_empty());

    let _ = std::fs::remove_dir_all(&base);
}

// ---------- stdout / stderr draining ----------

#[test]
fn stdout_and_stderr_drain_separately_in_one_run() {
    let mut c = Call::new("tac", &["-", "wx-ffi-no-such-file"]);
    c.stdin = b"one\ntwo\n";
    let (rc, out, err) = both(&c);
    assert_eq!(rc, 1);
    assert_eq!(out, b"two\none\n");
    let err = String::from_utf8_lossy(&err);
    assert!(err.starts_with("tac: "), "stderr: {err}");
    assert!(err.contains("wx-ffi-no-such-file"), "stderr: {err}");
}

#[test]
fn usage_error_goes_to_stderr_only() {
    let (rc, out, err) = both(&Call::new("basename", &[]));
    assert_eq!(rc, 1);
    assert!(out.is_empty());
    let err = String::from_utf8_lossy(&err);
    assert!(
        err.starts_with("basename: missing operand\n"),
        "stderr: {err}"
    );
    assert!(
        err.contains("Try 'basename --help' for more information."),
        "stderr: {err}"
    );
}

#[test]
fn empty_output_is_a_null_buffer() {
    let (rc, out, err) = both(&Call::new("true", &[]));
    assert_eq!(rc, 0);
    assert!(out.is_empty() && err.is_empty());
}

// ---------- large payloads ----------

#[test]
fn multi_mib_stdout_is_returned_whole() {
    let (rc, out, err) = both(&Call::new("seq", &["400000"]));
    assert_eq!(rc, 0);
    assert!(err.is_empty());
    let expected: String = (1..=400000).map(|i| format!("{i}\n")).collect();
    assert_eq!(out.len(), expected.len());
    assert!(out == expected.as_bytes());
}

#[test]
fn multi_mib_stdin_round_trips() {
    let mut input = Vec::new();
    for i in 0..300000u32 {
        input.extend_from_slice(format!("line {i}\n").as_bytes());
    }
    let mut c = Call::new("tac", &[]);
    c.stdin = &input;
    let (rc, out, err) = both(&c);
    assert_eq!(rc, 0);
    assert!(err.is_empty());
    let mut expected = Vec::with_capacity(input.len());
    for line in input.split_inclusive(|b| *b == b'\n').rev() {
        expected.extend_from_slice(line);
    }
    assert!(out == expected);
}

// ---------- winuxcmd_has_command ----------

#[test]
fn has_command_matches_registry() {
    let names: Vec<&str> = winux_cmds::registry().into_iter().map(|(n, _)| n).collect();
    assert!(!names.is_empty());
    for n in &names {
        let c = CString::new(*n).unwrap();
        assert_eq!(winuxcmd_has_command(c.as_ptr()), 1, "{n}");
    }
    assert_eq!(winuxcmd_has_command(std::ptr::null()), 0);
    for bad in ["", "LS", "ls ", " ls", "ls.exe", "winuxcmd", "link"] {
        let c = CString::new(bad).unwrap();
        assert_eq!(winuxcmd_has_command(c.as_ptr()), 0, "{bad:?}");
    }
}

#[test]
fn has_command_agrees_with_run() {
    for n in ["true", "no-such-command"] {
        let (rc, _, _) = both(&Call::new(n, &[]));
        let c = CString::new(n).unwrap();
        let known = winuxcmd_has_command(c.as_ptr()) == 1;
        assert_eq!(rc == ABI_FAILURE, !known, "{n}");
    }
}

// ---------- winuxcmd_grep_uses_perl ----------

fn uses_perl(args: &[&str], posixly_correct: bool) -> c_int {
    let argv = CStrs::strs(args, false);
    winuxcmd_grep_uses_perl(argv.len(), argv.ptrs.as_ptr(), posixly_correct as c_int)
}

#[test]
fn grep_uses_perl_predicate() {
    let cases: &[(&[&str], bool, c_int)] = &[
        (&["-P", "x"], false, 1),
        (&["--perl-regexp", "x"], false, 1),
        (&["--perl", "x"], false, 1),
        (&["-X", "perl", "x"], false, 1),
        (&["-Xperl", "x"], false, 1),
        (&["-iP", "x"], false, 1),
        (&["-E", "x"], false, 0),
        (&["-F", "x"], false, 0),
        (&["x"], false, 0),
        (&[], false, 0),
        // -e takes the next word as its pattern: "-P" there is not an option
        (&["-e", "-P"], false, 0),
        (&["--", "-P"], false, 0),
        // GNU permutation finds -P after an operand unless POSIXLY_CORRECT
        (&["x", "file", "-P"], false, 1),
        (&["x", "file", "-P"], true, 0),
        (&["-P", "x"], true, 1),
        (&["-X", "egrep", "x"], false, 0),
    ];
    for (args, posix, want) in cases {
        assert_eq!(uses_perl(args, *posix), *want, "{args:?} posix={posix}");
    }
}

#[test]
fn grep_uses_perl_null_argv_slot_is_false() {
    let argv: [*const c_char; 1] = [std::ptr::null()];
    assert_eq!(winuxcmd_grep_uses_perl(1, argv.as_ptr(), 0), 0);
    assert_eq!(winuxcmd_grep_uses_perl(0, std::ptr::null(), 0), 0);
}

// ---------- winuxcmd_free_buf ----------

#[test]
fn free_buf_is_null_and_double_free_safe() {
    unsafe {
        winuxcmd_free_buf(std::ptr::null_mut());
    }
    let name = CString::new("echo").unwrap();
    let cwd = CString::new(temp_cwd()).unwrap();
    let argv = CStrs::strs(&["hi"], false);
    let mut out = CBuf::empty();
    let mut err = CBuf::empty();
    let rc = unsafe {
        raw_run(
            Entry::V1,
            name.as_ptr(),
            1,
            argv.ptrs.as_ptr(),
            cwd.as_ptr(),
            &mut out,
            &mut err,
        )
    };
    assert_eq!(rc, 0);
    assert!(!out.ptr.is_null() && out.len == 3 && out.cap >= 3);
    unsafe {
        winuxcmd_free_buf(&mut out);
        assert!(out.ptr.is_null() && out.len == 0 && out.cap == 0);
        winuxcmd_free_buf(&mut out);
        winuxcmd_free_buf(&mut err);
    }
}
