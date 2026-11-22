//! Panic containment at the ABI boundary (#1488 FFI coverage): a command
//! that panics must never unwind into the foreign host. Both entries turn
//! the panic into status 125 (`ABI_FAILURE_STATUS`), leave the host's
//! output slots untouched (partial output is dropped, never handed over as
//! a half-written buffer), and stay usable for the next call.
//!
//! The registry has no panicking command, so the boundary is driven
//! through test-only names intercepted in `dispatch_and_collect` just
//! before `winux_cmds::dispatch` (see [`maybe_panic`]). They exist only
//! under `cfg(test)` and are not registry names.

use std::ffi::{c_char, c_int, c_void, CString};
use std::io::{Read, Write};

use winux_core::stream::{STREAM_IO_ERROR, STREAM_TIMED_OUT};
use winux_core::Io;

use crate::{winuxcmd_free_buf, winuxcmd_has_command, winuxcmd_run, winuxcmd_run_stream, CBuf};

/// Writes partial stdout/stderr, drains stdin, then panics with a `&str`.
const PANIC_STR: &str = "__winuxcmd_test_panic_str__";
/// Same, but with a `String` payload built at runtime.
const PANIC_STRING: &str = "__winuxcmd_test_panic_string__";
/// Same, but with a non-string payload (`panic_any(42u32)`).
const PANIC_ANY: &str = "__winuxcmd_test_panic_any__";

/// Test hook called by `dispatch_and_collect` before dispatch.
pub(crate) fn maybe_panic(name: &str, io: &mut Io) {
    if name != PANIC_STR && name != PANIC_STRING && name != PANIC_ANY {
        return;
    }
    let _ = io.stdout.write_all(b"partial stdout\n");
    let _ = io.stderr.write_all(b"partial stderr\n");
    let mut sink = Vec::new();
    let _ = io.stdin.read_to_end(&mut sink);
    match name {
        PANIC_STR => panic!("test panic at the ABI boundary"),
        PANIC_STRING => panic!("test panic after {} stdin bytes", sink.len()),
        _ => std::panic::panic_any(42u32),
    }
}

struct Argv {
    ptrs: Vec<*const c_char>,
    _owned: Vec<CString>,
}

impl Argv {
    fn new(args: &[&str]) -> Self {
        let owned: Vec<CString> = args.iter().map(|a| CString::new(*a).unwrap()).collect();
        let ptrs = owned.iter().map(|c| c.as_ptr()).collect();
        Argv {
            ptrs,
            _owned: owned,
        }
    }
}

static SENTINEL: [u8; 3] = *b"abc";

/// A host-initialized slot the ABI must not overwrite on failure.
fn sentinel_slot() -> CBuf {
    CBuf {
        ptr: SENTINEL.as_ptr() as *mut u8,
        len: SENTINEL.len(),
        cap: 0,
    }
}

fn is_sentinel(b: &CBuf) -> bool {
    b.ptr == SENTINEL.as_ptr() as *mut u8 && b.len == SENTINEL.len() && b.cap == 0
}

fn drain(buf: &mut CBuf) -> Vec<u8> {
    let v = if buf.ptr.is_null() {
        Vec::new()
    } else {
        unsafe { std::slice::from_raw_parts(buf.ptr, buf.len).to_vec() }
    };
    unsafe { winuxcmd_free_buf(buf) };
    v
}

fn cwd() -> CString {
    CString::new(std::env::temp_dir().to_string_lossy().replace('\\', "/")).unwrap()
}

/// v1 call with sentinel-initialized slots; returns (status, out, err).
fn run_v1(name: &str, args: &[&str], stdin: &[u8]) -> (i32, CBuf, CBuf) {
    let cname = CString::new(name).unwrap();
    let argv = Argv::new(args);
    let cwd = cwd();
    let mut out = sentinel_slot();
    let mut err = sentinel_slot();
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
    (status, out, err)
}

/// Feeds `data` in one segment, or returns `stop` once data is consumed.
struct Feeder {
    data: Vec<u8>,
    pos: usize,
    stop: i64,
    calls: usize,
}

unsafe extern "C" fn feed(ud: *mut c_void, buf: *mut u8, cap: usize) -> i64 {
    let f = &mut *(ud as *mut Feeder);
    f.calls += 1;
    if f.pos >= f.data.len() {
        return f.stop;
    }
    let n = cap.min(f.data.len() - f.pos);
    std::ptr::copy_nonoverlapping(f.data.as_ptr().add(f.pos), buf, n);
    f.pos += n;
    n as i64
}

/// v2 call; `feeder == None` passes a NULL reader.
fn run_v2(name: &str, feeder: Option<&mut Feeder>) -> (i32, CBuf, CBuf) {
    let cname = CString::new(name).unwrap();
    let argv = Argv::new(&[]);
    let cwd = cwd();
    let mut out = sentinel_slot();
    let mut err = sentinel_slot();
    let (read_fn, ud) = match feeder {
        Some(f) => (
            Some(feed as winux_core::stream::StreamReadFn),
            f as *mut Feeder as *mut c_void,
        ),
        None => (None, std::ptr::null_mut()),
    };
    let status = unsafe {
        winuxcmd_run_stream(
            cname.as_ptr(),
            0,
            argv.ptrs.as_ptr(),
            std::ptr::null(),
            cwd.as_ptr(),
            read_fn,
            ud,
            &mut out,
            &mut err,
        )
    };
    (status, out, err)
}

fn assert_contained(status: i32, out: &CBuf, err: &CBuf) {
    assert_eq!(status, winux_core::ABI_FAILURE_STATUS as i32);
    assert!(is_sentinel(out), "stdout slot was written on panic");
    assert!(is_sentinel(err), "stderr slot was written on panic");
}

#[test]
fn panic_names_are_not_registry_commands() {
    for name in [PANIC_STR, PANIC_STRING, PANIC_ANY] {
        let c = CString::new(name).unwrap();
        assert_eq!(winuxcmd_has_command(c.as_ptr()), 0, "{name}");
    }
}

#[test]
fn v1_panic_is_125_and_slots_untouched() {
    for name in [PANIC_STR, PANIC_STRING, PANIC_ANY] {
        let (status, out, err) = run_v1(name, &["x"], b"stdin bytes\n");
        assert_contained(status, &out, &err);
    }
}

#[test]
fn v1_panic_with_empty_stdin_is_125() {
    let (status, out, err) = run_v1(PANIC_STR, &[], b"");
    assert_contained(status, &out, &err);
}

#[test]
fn v2_panic_with_null_reader_is_125() {
    for name in [PANIC_STR, PANIC_STRING, PANIC_ANY] {
        let (status, out, err) = run_v2(name, None);
        assert_contained(status, &out, &err);
    }
}

#[test]
fn v2_panic_after_host_fed_stdin_is_125() {
    let mut f = Feeder {
        data: b"alpha\nbeta\n".to_vec(),
        pos: 0,
        stop: 0,
        calls: 0,
    };
    let (status, out, err) = run_v2(PANIC_STRING, Some(&mut f));
    assert_contained(status, &out, &err);
    assert_eq!(f.pos, f.data.len(), "stdin was consumed before the panic");
    assert!(f.calls >= 2, "the EOF call happened inside the guarded run");
}

#[test]
fn v2_panic_beats_host_timeout() {
    // The host stopped feeding (-1) and the command then panicked: the
    // panic decides the status (125), not the 124 timeout convention.
    let mut f = Feeder {
        data: b"x".to_vec(),
        pos: 0,
        stop: STREAM_TIMED_OUT,
        calls: 0,
    };
    let (status, out, err) = run_v2(PANIC_STR, Some(&mut f));
    assert_contained(status, &out, &err);
}

#[test]
fn v2_panic_after_host_io_error_is_125() {
    let mut f = Feeder {
        data: Vec::new(),
        pos: 0,
        stop: STREAM_IO_ERROR,
        calls: 0,
    };
    let (status, out, err) = run_v2(PANIC_ANY, Some(&mut f));
    assert_contained(status, &out, &err);
}

#[test]
fn boundary_is_reusable_after_a_contained_panic() {
    for _ in 0..3 {
        let (status, out, err) = run_v1(PANIC_STR, &[], b"");
        assert_contained(status, &out, &err);

        let (status, mut out, mut err) = run_v1("basename", &["a/b/c"], b"");
        assert_eq!(status, 0);
        assert_eq!(drain(&mut out), b"c\n");
        assert!(drain(&mut err).is_empty());

        let (status, _, _) = run_v2(PANIC_ANY, None);
        assert_eq!(status, winux_core::ABI_FAILURE_STATUS as i32);

        let mut f = Feeder {
            data: b"1\n2\n".to_vec(),
            pos: 0,
            stop: 0,
            calls: 0,
        };
        let (status, mut out, mut err) = run_v2("tac", Some(&mut f));
        assert_eq!(status, 0);
        assert_eq!(drain(&mut out), b"2\n1\n");
        assert!(drain(&mut err).is_empty());
    }
}

#[test]
fn panic_on_a_worker_thread_is_contained() {
    // A host calling from a worker thread gets the same containment, and
    // the thread stays usable for its next call.
    let h = std::thread::spawn(|| {
        let (status, out, err) = run_v1(PANIC_STR, &[], b"");
        assert_contained(status, &out, &err);
        let (status, mut out, _) = run_v1("basename", &["x/y"], b"");
        assert_eq!(status, 0);
        drain(&mut out)
    });
    assert_eq!(h.join().expect("panic escaped the ABI"), b"y\n");
}
