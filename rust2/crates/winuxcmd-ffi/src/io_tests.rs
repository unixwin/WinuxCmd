//! Streaming output ABI (v3) unit tests: `winuxcmd_run_io` driven exactly
//! as a foreign host would — C strings, a `#[repr(C)]` callback table, raw
//! `user_data` pointers, recording write callbacks.
//!
//! * chunking: line-buffered stdout delivers one callback per complete
//!   line (`seq 3` -> "1\n", "2\n", "3\n"); block-buffered stdout delivers
//!   the same bytes as ONE callback;
//! * stderr is unbuffered: a mid-run diagnostic reaches the host before
//!   the buffered stdout flush;
//! * broken pipe (`-1`): an unbounded producer (`seq inf`) stops promptly
//!   and the host is called exactly once; `-2` likewise;
//! * NULL callbacks discard, NULL table runs, unknown command is 125;
//! * stdin through v3 is the v2 contract (byte-identical to v1, 124 on
//!   host stop).

use std::ffi::{c_char, c_int, c_void, CString};
use std::time::{Duration, Instant};

use winux_core::stream::{
    IO_STDOUT_LINE_BUFFERED, STREAM_TIMED_OUT, STREAM_WRITE_BROKEN_PIPE, STREAM_WRITE_IO_ERROR,
    STREAM_WRITE_OK,
};

use crate::{winuxcmd_free_buf, winuxcmd_run, winuxcmd_run_io, CBuf, WinuxcmdIoV3};

// ---------- recording host ----------

#[derive(Debug, Clone, PartialEq)]
enum Event {
    Out(Vec<u8>),
    Err(Vec<u8>),
}

/// One recorder serves both streams (two user_data views of the same
/// state), so the event log carries the cross-stream callback order.
struct Recorder {
    events: Vec<Event>,
    stdout_rc: c_int,
    stdout_calls: usize,
}

impl Recorder {
    fn new(stdout_rc: c_int) -> Self {
        Recorder {
            events: Vec::new(),
            stdout_rc,
            stdout_calls: 0,
        }
    }
    fn out_chunks(&self) -> Vec<Vec<u8>> {
        self.events
            .iter()
            .filter_map(|e| match e {
                Event::Out(b) => Some(b.clone()),
                _ => None,
            })
            .collect()
    }
    fn out_bytes(&self) -> Vec<u8> {
        self.out_chunks().concat()
    }
    fn err_bytes(&self) -> Vec<u8> {
        self.events
            .iter()
            .filter_map(|e| match e {
                Event::Err(b) => Some(b.clone()),
                _ => None,
            })
            .collect::<Vec<_>>()
            .concat()
    }
}

unsafe extern "C" fn rec_stdout(ud: *mut c_void, buf: *const u8, len: usize) -> c_int {
    let r = &mut *(ud as *mut Recorder);
    assert!(len >= 1, "write callback must never see len == 0");
    r.stdout_calls += 1;
    r.events
        .push(Event::Out(std::slice::from_raw_parts(buf, len).to_vec()));
    r.stdout_rc
}

unsafe extern "C" fn rec_stderr(ud: *mut c_void, buf: *const u8, len: usize) -> c_int {
    let r = &mut *(ud as *mut Recorder);
    assert!(len >= 1, "write callback must never see len == 0");
    r.events
        .push(Event::Err(std::slice::from_raw_parts(buf, len).to_vec()));
    STREAM_WRITE_OK
}

struct MemIn {
    data: Vec<u8>,
    pos: usize,
    stop: bool,
}

unsafe extern "C" fn mem_read(ud: *mut c_void, buf: *mut u8, cap: usize) -> i64 {
    let m = &mut *(ud as *mut MemIn);
    if m.stop {
        return STREAM_TIMED_OUT;
    }
    let n = cap.min(m.data.len() - m.pos).min(5);
    std::ptr::copy_nonoverlapping(m.data.as_ptr().add(m.pos), buf, n);
    m.pos += n;
    n as i64
}

struct Argv {
    ptrs: Vec<*const c_char>,
    #[allow(dead_code)]
    owned: Vec<CString>,
}

impl Argv {
    fn new(args: &[&str]) -> Self {
        let owned: Vec<CString> = args.iter().map(|a| CString::new(*a).unwrap()).collect();
        let ptrs: Vec<*const c_char> = owned.iter().map(|c| c.as_ptr()).collect();
        Argv { ptrs, owned }
    }
}

fn cwd_cstr() -> CString {
    CString::new(std::env::temp_dir().to_string_lossy().replace('\\', "/")).unwrap()
}

fn run_io(
    name: &str,
    args: &[&str],
    rec: &mut Recorder,
    flags: u32,
    stdin: Option<&mut MemIn>,
) -> i32 {
    let cname = CString::new(name).unwrap();
    let argv = Argv::new(args);
    let cwd = cwd_cstr();
    let ud = rec as *mut Recorder as *mut c_void;
    let (rd, rd_ud) = match stdin {
        Some(m) => (
            Some(mem_read as winux_core::stream::StreamReadFn),
            m as *mut MemIn as *mut c_void,
        ),
        None => (None, std::ptr::null_mut()),
    };
    let table = WinuxcmdIoV3 {
        stdin_read: rd,
        stdin_user_data: rd_ud,
        stdout_write: Some(rec_stdout),
        stdout_user_data: ud,
        stderr_write: Some(rec_stderr),
        stderr_user_data: ud,
        flags,
    };
    unsafe {
        winuxcmd_run_io(
            cname.as_ptr(),
            args.len() as c_int,
            argv.ptrs.as_ptr(),
            std::ptr::null(),
            cwd.as_ptr(),
            &table,
        )
    }
}

fn run_v1(name: &str, args: &[&str], stdin: &[u8]) -> (i32, Vec<u8>, Vec<u8>) {
    let cname = CString::new(name).unwrap();
    let argv = Argv::new(args);
    let cwd = cwd_cstr();
    let mut out = CBuf {
        ptr: std::ptr::null_mut(),
        len: 0,
        cap: 0,
    };
    let mut err = CBuf {
        ptr: std::ptr::null_mut(),
        len: 0,
        cap: 0,
    };
    let status = unsafe {
        winuxcmd_run(
            cname.as_ptr(),
            args.len() as c_int,
            argv.ptrs.as_ptr(),
            std::ptr::null(),
            cwd.as_ptr(),
            if stdin.is_empty() {
                std::ptr::null()
            } else {
                stdin.as_ptr()
            },
            stdin.len(),
            &mut out,
            &mut err,
        )
    };
    let take = |b: &mut CBuf| unsafe {
        let v = if b.ptr.is_null() {
            Vec::new()
        } else {
            std::slice::from_raw_parts(b.ptr, b.len).to_vec()
        };
        winuxcmd_free_buf(b);
        v
    };
    (status, take(&mut out), take(&mut err))
}

// ---------- chunking ----------

#[test]
fn line_buffered_seq3_is_one_callback_per_line() {
    let mut rec = Recorder::new(STREAM_WRITE_OK);
    let rc = run_io("seq", &["3"], &mut rec, IO_STDOUT_LINE_BUFFERED, None);
    assert_eq!(rc, 0);
    assert_eq!(
        rec.out_chunks(),
        vec![b"1\n".to_vec(), b"2\n".to_vec(), b"3\n".to_vec()]
    );
}

#[test]
fn block_buffered_seq3_is_one_callback() {
    let mut rec = Recorder::new(STREAM_WRITE_OK);
    let rc = run_io("seq", &["3"], &mut rec, 0, None);
    assert_eq!(rc, 0);
    assert_eq!(rec.out_chunks(), vec![b"1\n2\n3\n".to_vec()]);
}

#[test]
fn block_buffered_output_is_byte_identical_to_v1() {
    for (name, args) in [
        ("seq", vec!["1", "20000"]),
        ("seq", vec!["-s", ",", "1", "0.5", "50"]),
        ("echo", vec!["-e", "a\\tb\\nc"]),
        ("basename", vec!["/a/b/c.txt", ".txt"]),
    ] {
        let (v1_rc, v1_out, v1_err) = run_v1(name, &args, b"");
        for flags in [0, IO_STDOUT_LINE_BUFFERED] {
            let mut rec = Recorder::new(STREAM_WRITE_OK);
            let rc = run_io(name, &args, &mut rec, flags, None);
            assert_eq!(rc, v1_rc, "{name} {args:?} flags={flags}");
            assert_eq!(rec.out_bytes(), v1_out, "{name} {args:?} flags={flags}");
            assert_eq!(rec.err_bytes(), v1_err, "{name} {args:?} flags={flags}");
        }
    }
}

// ---------- stderr ordering ----------

#[test]
fn stderr_reaches_host_before_buffered_stdout_flush() {
    // realpath resolves "." (stdout, block-buffered) and then fails on a
    // missing parent (stderr, unbuffered) — the diagnostic must arrive
    // first, the buffered stdout only at the post-dispatch flush.
    let mut rec = Recorder::new(STREAM_WRITE_OK);
    let rc = run_io("realpath", &[".", "no-such-dir-v3/x"], &mut rec, 0, None);
    assert_eq!(rc, 1);
    assert!(
        matches!(rec.events.first(), Some(Event::Err(_))),
        "first event must be stderr: {:?}",
        rec.events
    );
    assert!(matches!(rec.events.last(), Some(Event::Out(_))));
    assert!(!rec.out_bytes().is_empty());
    assert!(String::from_utf8_lossy(&rec.err_bytes()).starts_with("realpath: "));
}

// ---------- write failures ----------

#[test]
fn broken_pipe_stops_unbounded_producer_with_one_host_call() {
    for flags in [0, IO_STDOUT_LINE_BUFFERED] {
        let mut rec = Recorder::new(STREAM_WRITE_BROKEN_PIPE);
        let t0 = Instant::now();
        let rc = run_io("seq", &["inf"], &mut rec, flags, None);
        assert!(
            t0.elapsed() < Duration::from_secs(10),
            "seq inf did not stop"
        );
        assert_eq!(rec.stdout_calls, 1, "flags={flags}");
        // seq propagates the write error with `?` -> CmdError::Io -> 125;
        // the host maps the broken pipe itself (bridge: 141).
        assert_eq!(rc, 125, "flags={flags}");
        assert!(rec.err_bytes().is_empty());
    }
}

#[test]
fn host_write_error_stops_producer_with_one_host_call() {
    for code in [STREAM_WRITE_IO_ERROR, -9] {
        let mut rec = Recorder::new(code);
        let rc = run_io("seq", &["inf"], &mut rec, 0, None);
        assert_eq!(rec.stdout_calls, 1);
        assert_eq!(rc, 125);
    }
}

#[test]
fn failure_at_final_flush_keeps_command_status() {
    // Small output stays in the block buffer until the boundary flush: the
    // command already returned 0, the host learns about the failure
    // through its own callback result.
    let mut rec = Recorder::new(STREAM_WRITE_IO_ERROR);
    let rc = run_io("seq", &["3"], &mut rec, 0, None);
    assert_eq!(rc, 0);
    assert_eq!(rec.stdout_calls, 1);
}

// ---------- NULL handling ----------

#[test]
fn null_table_and_null_callbacks() {
    let cname = CString::new("seq").unwrap();
    let argv = Argv::new(&["5"]);
    let cwd = cwd_cstr();
    let rc = unsafe {
        winuxcmd_run_io(
            cname.as_ptr(),
            1,
            argv.ptrs.as_ptr(),
            std::ptr::null(),
            cwd.as_ptr(),
            std::ptr::null(),
        )
    };
    assert_eq!(rc, 0);
    let table = WinuxcmdIoV3 {
        stdin_read: None,
        stdin_user_data: std::ptr::null_mut(),
        stdout_write: None,
        stdout_user_data: std::ptr::null_mut(),
        stderr_write: None,
        stderr_user_data: std::ptr::null_mut(),
        flags: IO_STDOUT_LINE_BUFFERED,
    };
    let rc = unsafe {
        winuxcmd_run_io(
            cname.as_ptr(),
            1,
            argv.ptrs.as_ptr(),
            std::ptr::null(),
            cwd.as_ptr(),
            &table,
        )
    };
    assert_eq!(rc, 0);
}

#[test]
fn unknown_command_is_125_and_writes_nothing() {
    let mut rec = Recorder::new(STREAM_WRITE_OK);
    let rc = run_io("no-such-command-v3", &[], &mut rec, 0, None);
    assert_eq!(rc, 125);
    assert!(rec.events.is_empty());
}

#[test]
fn null_name_is_125() {
    let cwd = cwd_cstr();
    let rc = unsafe {
        winuxcmd_run_io(
            std::ptr::null(),
            0,
            std::ptr::null(),
            std::ptr::null(),
            cwd.as_ptr(),
            std::ptr::null(),
        )
    };
    assert_eq!(rc, 125);
}

// ---------- stdin through v3 ----------

#[test]
fn stdin_pull_through_v3_matches_v1() {
    let input = b"one\ntwo\nthree\nno-newline".to_vec();
    for name in ["tac", "base64"] {
        let (v1_rc, v1_out, v1_err) = run_v1(name, &[], &input);
        let mut rec = Recorder::new(STREAM_WRITE_OK);
        let mut m = MemIn {
            data: input.clone(),
            pos: 0,
            stop: false,
        };
        let rc = run_io(name, &[], &mut rec, 0, Some(&mut m));
        assert_eq!(rc, v1_rc, "{name}");
        assert_eq!(rec.out_bytes(), v1_out, "{name}");
        assert_eq!(rec.err_bytes(), v1_err, "{name}");
    }
}

#[test]
fn stdin_host_stop_is_124() {
    let mut rec = Recorder::new(STREAM_WRITE_OK);
    let mut m = MemIn {
        data: Vec::new(),
        pos: 0,
        stop: true,
    };
    let rc = run_io("tac", &[], &mut rec, 0, Some(&mut m));
    assert_eq!(rc, 124);
}
