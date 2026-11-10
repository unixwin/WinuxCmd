//! Streaming ABI (v2) unit tests: the host-callback protocol exercised
//! exactly as a foreign host would drive it.
//!
//! * segmented feeding (1 B / 7 B / 64 KiB uneven segments) must be
//!   byte-identical to the v1 whole-buffer entry — across ASCII, multi-byte
//!   UTF-8 (chunks split mid-character at 1 B and 7 B), no-trailing-newline
//!   and empty inputs;
//! * host stops feeding -> status 124 (`STREAM_TIMEOUT_STATUS`, GNU
//!   `timeout(1)` convention);
//! * host read failure -> surfaces as the command's own rc (`tac` reads
//!   errors into rc 1);
//! * NULL reader == empty stdin;
//! * callback contract: never called with `cap == 0`; bytes beyond the
//!   returned length are never read (proven by poisoning the untouched
//!   tail of the host buffer with 0xAB before returning).

use std::ffi::{c_char, c_int, c_void, CString};

use winux_core::stream::{STREAM_IO_ERROR, STREAM_TIMED_OUT};

use crate::{winuxcmd_free_buf, winuxcmd_run, winuxcmd_run_stream, CBuf};

// ---------- host-side feeder ----------

/// Replays `data` in segments of at most `seg` bytes (the last segment
/// partial — the "uneven segmentation" the contract must tolerate). After
/// `stop_after` successful calls it returns `stop_code` instead, modeling
/// a host that stops feeding.
struct Feeder {
    data: Vec<u8>,
    pos: usize,
    seg: usize,
    stop_after: Option<usize>,
    stop_code: i64,
    calls: usize,
    min_cap_seen: usize,
}

impl Feeder {
    fn new(data: Vec<u8>, seg: usize) -> Self {
        assert!(seg >= 1);
        Feeder {
            data,
            pos: 0,
            seg,
            stop_after: None,
            stop_code: 0,
            calls: 0,
            min_cap_seen: usize::MAX,
        }
    }

    /// Model a host that stops feeding: after `calls` successful callback
    /// invocations, return `code` (STREAM_TIMED_OUT / STREAM_IO_ERROR).
    fn stop_after(self, calls: usize, code: i64) -> Self {
        Feeder {
            stop_after: Some(calls),
            stop_code: code,
            ..self
        }
    }
}

unsafe extern "C" fn feed(ud: *mut c_void, buf: *mut u8, cap: usize) -> i64 {
    let f = &mut *(ud as *mut Feeder);
    f.calls += 1;
    f.min_cap_seen = f.min_cap_seen.min(cap);
    if let Some(k) = f.stop_after {
        if f.calls > k {
            return f.stop_code;
        }
    }
    if f.pos >= f.data.len() {
        return 0;
    }
    let n = cap.min(f.seg).min(f.data.len() - f.pos);
    let buf = std::slice::from_raw_parts_mut(buf, cap);
    // Poison everything beyond the returned length: byte-equality of the
    // outputs proves the wrapper copied exactly `n` bytes (no over-read,
    // no garbage passing through).
    for b in &mut buf[n..] {
        *b = 0xAB;
    }
    std::ptr::copy_nonoverlapping(f.data.as_ptr().add(f.pos), buf.as_mut_ptr(), n);
    f.pos += n;
    n as i64
}

// ---------- harness ----------

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

fn drain(buf: &mut CBuf) -> Vec<u8> {
    unsafe {
        let slice = if buf.ptr.is_null() {
            &[][..]
        } else {
            std::slice::from_raw_parts(buf.ptr, buf.len)
        };
        let v = slice.to_vec();
        winuxcmd_free_buf(buf);
        v
    }
}

fn cwd_cstr() -> CString {
    CString::new(std::env::temp_dir().to_string_lossy().replace('\\', "/")).unwrap()
}

/// ABI v1: whole stdin buffer.
fn run_v1(name: &str, args: &[&str], stdin: &[u8]) -> (i32, Vec<u8>, Vec<u8>) {
    let cname = CString::new(name).unwrap();
    let argv = Argv::new(args);
    let mut out = CBuf::empty();
    let mut err = CBuf::empty();
    let status = unsafe {
        winuxcmd_run(
            cname.as_ptr(),
            args.len() as c_int,
            argv.ptrs.as_ptr(),
            std::ptr::null(),
            cwd_cstr().as_ptr(),
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
    (status, drain(&mut out), drain(&mut err))
}

/// ABI v2: stdin through the host feeder callback.
fn run_v2(name: &str, args: &[&str], feeder: &mut Feeder) -> (i32, Vec<u8>, Vec<u8>) {
    let cname = CString::new(name).unwrap();
    let argv = Argv::new(args);
    let mut out = CBuf::empty();
    let mut err = CBuf::empty();
    let status = unsafe {
        winuxcmd_run_stream(
            cname.as_ptr(),
            args.len() as c_int,
            argv.ptrs.as_ptr(),
            std::ptr::null(),
            cwd_cstr().as_ptr(),
            Some(feed),
            feeder as *mut Feeder as *mut c_void,
            &mut out,
            &mut err,
        )
    };
    (status, drain(&mut out), drain(&mut err))
}

/// ABI v2 with a NULL reader (empty stdin).
fn run_v2_none(name: &str, args: &[&str]) -> (i32, Vec<u8>, Vec<u8>) {
    let cname = CString::new(name).unwrap();
    let argv = Argv::new(args);
    let mut out = CBuf::empty();
    let mut err = CBuf::empty();
    let status = unsafe {
        winuxcmd_run_stream(
            cname.as_ptr(),
            args.len() as c_int,
            argv.ptrs.as_ptr(),
            std::ptr::null(),
            cwd_cstr().as_ptr(),
            None,
            std::ptr::null_mut(),
            &mut out,
            &mut err,
        )
    };
    (status, drain(&mut out), drain(&mut err))
}

// ---------- inputs (small mirrors of the 64 MiB script kinds) ----------

fn ascii() -> Vec<u8> {
    b"alpha\nbeta\ngamma\ndelta\n".to_vec()
}

fn utf8() -> Vec<u8> {
    "中文行一\n中文行二\n🎉🚀 emoji line\néüñ mixed tail\n"
        .as_bytes()
        .to_vec()
}

fn notrail() -> Vec<u8> {
    b"one\ntwo\nthree with NO trailing newline".to_vec()
}

fn empty() -> Vec<u8> {
    Vec::new()
}

// ---------- equivalence: stream == whole buffer ----------

#[test]
fn tac_stream_matches_whole_block_all_segments() {
    for (label, input) in [
        ("ascii", ascii()),
        ("utf8", utf8()),
        ("notrail", notrail()),
        ("empty", empty()),
    ] {
        let want = run_v1("tac", &[], &input);
        for seg in [1usize, 7, 64 * 1024] {
            let mut f = Feeder::new(input.clone(), seg);
            let got = run_v2("tac", &[], &mut f);
            assert_eq!(got.0, want.0, "rc tac seg={seg} input={label}");
            assert_eq!(got.1, want.1, "stdout tac seg={seg} input={label}");
            assert_eq!(got.2, want.2, "stderr tac seg={seg} input={label}");
            // Callback contract: never a zero-capacity call.
            assert!(f.min_cap_seen >= 1, "host called with cap=0 (seg={seg})");
        }
    }
}

#[test]
fn base64_stream_matches_whole_block_all_segments() {
    for (label, input) in [("ascii", ascii()), ("utf8", utf8())] {
        let want = run_v1("base64", &[], &input);
        for seg in [1usize, 7, 64 * 1024] {
            let mut f = Feeder::new(input.clone(), seg);
            let got = run_v2("base64", &[], &mut f);
            assert_eq!(got.0, want.0, "rc base64 seg={seg} input={label}");
            assert_eq!(got.1, want.1, "stdout base64 seg={seg} input={label}");
            assert_eq!(got.2, want.2, "stderr base64 seg={seg} input={label}");
        }
    }
}

#[test]
fn base64_decode_roundtrip_through_stream() {
    // encode once (v1), then feed the encoded bytes back through both
    // entries with -d: the streaming side must decode identically.
    let (_, encoded, _) = run_v1("base64", &[], &ascii());
    let want = run_v1("base64", &["-d"], &encoded);
    assert_eq!(want.1, ascii());
    for seg in [1usize, 7, 64 * 1024] {
        let mut f = Feeder::new(encoded.clone(), seg);
        let got = run_v2("base64", &["-d"], &mut f);
        assert_eq!(got.0, want.0);
        assert_eq!(got.1, want.1, "decoded bytes seg={seg}");
    }
}

#[test]
fn utf8_multibyte_split_across_every_byte() {
    // Explicit case for the class the matrix covers: 3- and 4-byte UTF-8
    // sequences split at arbitrary byte boundaries (seg=1 sends the whole
    // input one byte per callback). tac is byte-oriented; equality here
    // proves the streaming wrapper introduces no character-boundary
    // assumption anywhere in the pipe.
    let input = utf8();
    let want = run_v1("tac", &[], &input);
    let mut f = Feeder::new(input, 1);
    let got = run_v2("tac", &[], &mut f);
    assert_eq!(got.1, want.1);
    assert_eq!(got.0, want.0);
}

#[test]
fn null_reader_is_empty_stdin() {
    let want = run_v1("tac", &[], &[]);
    let got = run_v2_none("tac", &[]);
    assert_eq!(got, want);
    assert_eq!(got.0, 0);
    assert!(got.1.is_empty());
}

// ---------- stop-feeding conventions ----------

#[test]
fn host_stops_feeding_is_timeout_124() {
    let input = ascii();
    let mut f = Feeder::new(input, 2).stop_after(3, STREAM_TIMED_OUT);
    let (status, out, _err) = run_v2("tac", &[], &mut f);
    assert_eq!(
        status,
        winux_core::stream::STREAM_TIMEOUT_STATUS as i32,
        "host stopping feeding must surface as 124 (GNU timeout convention)"
    );
    // tac writes nothing before EOF, so nothing leaks as partial output.
    assert!(out.is_empty());
}

#[test]
fn host_io_error_surfaces_as_command_rc() {
    // tac maps a stdin read error to rc 1 with no output (io::copy err ->
    // failed flag); the ABI keeps that contract for STREAM_IO_ERROR.
    let mut f = Feeder::new(ascii(), 2).stop_after(0, STREAM_IO_ERROR);
    let (status, out, _err) = run_v2("tac", &[], &mut f);
    assert_eq!(status, 1);
    assert!(out.is_empty());
}

#[test]
fn timeout_beats_command_status_even_when_swallowed() {
    // Same swallow path as above, but with the TIMED_OUT code: the 124
    // convention is tracked at the wrapper, not inferred from the command
    // status, so tac's internal rc-1 must NOT mask it.
    let mut f = Feeder::new(ascii(), 2).stop_after(2, STREAM_TIMED_OUT);
    let (status, _out, _err) = run_v2("tac", &[], &mut f);
    assert_eq!(status, 124);
}

#[test]
fn stream_timeout_status_constant_is_gnu_convention() {
    // coreutils timeout(1) exits 124 for a timed-out child.
    assert_eq!(winux_core::stream::STREAM_TIMEOUT_STATUS, 124);
    assert_eq!(STREAM_TIMED_OUT, -1);
    assert_eq!(STREAM_IO_ERROR, -2);
}

#[test]
fn bigger_than_cap_return_is_clamped_not_overread() {
    // A host claiming MORE than `cap` must never make the wrapper consume
    // more than `cap` bytes: the clamp bounds every read to the buffer.
    // The feeder below fills every requested buffer completely but claims
    // cap + 999 (defensible case); the final partial read stays honest,
    // because a host lying about a partial fill cannot be defended — the
    // wrapper cannot know how many bytes were really written (documented
    // contract: that host is broken). Without the clamp, the first call
    // alone would push 999 poison bytes into the command and the output
    // equality below would fail.
    let data: Vec<u8> = (0..20_000u32)
        .map(|i| b"abcdefgh\n"[i as usize % 9])
        .collect();
    let want = run_v1("tac", &[], &data);
    unsafe extern "C" fn lying_feed(ud: *mut c_void, buf: *mut u8, cap: usize) -> i64 {
        let f = &mut *(ud as *mut Feeder);
        if f.pos >= f.data.len() {
            return 0;
        }
        let n = cap.min(f.data.len() - f.pos);
        std::ptr::copy_nonoverlapping(f.data.as_ptr().add(f.pos), buf, n);
        f.pos += n;
        if n == cap {
            // full buffer: over-claim (clamped back to cap by the wrapper)
            n as i64 + 999
        } else {
            n as i64 // honest partial (final chunk)
        }
    }
    let cname = CString::new("tac").unwrap();
    let mut out = CBuf::empty();
    let mut err = CBuf::empty();
    let mut feeder = Feeder::new(data, usize::MAX);
    let status = unsafe {
        winuxcmd_run_stream(
            cname.as_ptr(),
            0,
            std::ptr::null(),
            std::ptr::null(),
            cwd_cstr().as_ptr(),
            Some(lying_feed),
            &mut feeder as *mut Feeder as *mut c_void,
            &mut out,
            &mut err,
        )
    };
    let stdout = drain(&mut out);
    let stderr = drain(&mut err);
    assert_eq!(status, want.0, "clamped run still succeeds");
    assert_eq!(stdout, want.1, "clamped bytes must match whole-buffer tac");
    let _ = stderr;
}
