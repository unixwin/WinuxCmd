//! Streaming stdin (ABI v2) and streaming output (ABI v3) for the ABI
//! boundary (#1488, #1550 follow-up).
//!
//! A host that owns a real stream (console, pipe, file) registers one
//! pull callback — [`StreamReadFn`] — and [`winuxcmd_run_stream`]
//! (winuxcmd-ffi) wraps it as a [`std::io::Read`] so commands consume it
//! through the ordinary `Io.stdin` handle. Nothing in a command changes;
//! streaming is purely an ABI/core property.
//!
//! Contract (full text in rust2/README.md "Streaming stdin ABI"):
//! * callback fills a host-owned `buf` (`cap >= 1`; Rust never calls with
//!   `cap == 0`) and returns the number of bytes filled — partial reads are
//!   legal, `0` is EOF, [`STREAM_TIMED_OUT`] means the host stopped feeding
//!   (the run then returns [`STREAM_TIMEOUT_STATUS`], GNU `timeout(1)`'s
//!   convention), [`STREAM_IO_ERROR`] (and any other negative, reserved)
//!   means host read failure and surfaces as `io::Error`.
//! * **Ownership / lifetime:** the buffer is borrowed for the duration of
//!   one callback invocation; Rust never stores the pointer and never
//!   dereferences `user_data`. The reader lives only inside one
//!   `winuxcmd_run_stream` call.
//! * **Windows red line (#1487 lesson):** Rust never touches a CRT fd,
//!   process stdio, or a console/pipe HANDLE — the host reads its own
//!   stream and hands over plain byte buffers.
//! * **Threading model: single-threaded.** The callback is invoked
//!   synchronously on the caller's thread. [`StreamReader`] holds a raw
//!   pointer, so it is `!Send + !Sync` by construction.
//!
//! Output side (ABI v3, full text in rust2/README.md "Streaming output ABI
//! (v3)"): the host registers one push callback per output stream —
//! [`StreamWriteFn`] — and [`HostWriter`] wraps it as [`std::io::Write`].
//! `0` = all bytes written, [`STREAM_WRITE_BROKEN_PIPE`] (`-1`) = the
//! reader is gone (`io::ErrorKind::BrokenPipe`), [`STREAM_WRITE_IO_ERROR`]
//! (`-2`, any other non-zero value is treated the same) = other host write
//! failure (`io::Error::other`). The first failure is sticky: every later
//! write fails at once WITHOUT calling the host, so an unbounded producer
//! (`seq inf`, `yes`-class loops) stops at its next write. The buffering
//! policy (line-buffered vs 64 KiB block-buffered stdout, unbuffered
//! stderr) lives in [`stdout_writer`] / [`StdoutWriter`].

use std::cell::Cell;
use std::ffi::{c_int, c_void};
use std::io::{self, BufWriter, LineWriter, Read, Write};

/// Host-side "fill this buffer" callback (the C form is
/// `long long (*)(void* user_data, unsigned char* buf, size_t cap)`).
///
/// Return: bytes filled (`> 0`, `<= cap`, partial legal), `0` for EOF,
/// [`STREAM_TIMED_OUT`] (`-1`), [`STREAM_IO_ERROR`] (`-2`; any other
/// negative is reserved and treated the same).
pub type StreamReadFn =
    unsafe extern "C" fn(user_data: *mut c_void, buf: *mut u8, cap: usize) -> i64;

/// Callback return: the host stopped feeding (timeout / interrupt).
pub const STREAM_TIMED_OUT: i64 = -1;

/// Callback return: host read failure. Other negative values are reserved
/// and treated identically.
pub const STREAM_IO_ERROR: i64 = -2;

/// Status returned by `winuxcmd_run_stream` when the host reported
/// [`STREAM_TIMED_OUT`]. 124 is GNU coreutils `timeout(1)`'s exit status
/// for a timed-out child.
pub const STREAM_TIMEOUT_STATUS: u32 = 124;

/// Wrap a host [`StreamReadFn`] + opaque `user_data` as [`Read`].
///
/// The instance is valid only inside one streaming run: it borrows the
/// host's buffer per callback call, never retains the pointer, and never
/// dereferences `user_data`. Raw pointer members make it `!Send + !Sync`
/// (single-threaded model, enforced by the compiler).
pub struct StreamReader {
    read_fn: StreamReadFn,
    user_data: *mut c_void,
    timed_out: Cell<bool>,
}

impl StreamReader {
    /// The caller guarantees `read_fn` is a valid C-callable function that
    /// stays valid for every call made through this reader.
    pub fn new(read_fn: StreamReadFn, user_data: *mut c_void) -> Self {
        StreamReader {
            read_fn,
            user_data,
            timed_out: Cell::new(false),
        }
    }

    /// Whether the host reported [`STREAM_TIMED_OUT`] at any point. Checked
    /// by the ABI boundary after dispatch: the timeout status is tracked at
    /// the wrapper, not inferred from the command's own status (a command
    /// may have swallowed the read error internally).
    pub fn timed_out(&self) -> bool {
        self.timed_out.get()
    }
}

impl Read for StreamReader {
    fn read(&mut self, buf: &mut [u8]) -> io::Result<usize> {
        if buf.is_empty() {
            // Never bother the host with a zero-capacity call.
            return Ok(0);
        }
        let n = unsafe { (self.read_fn)(self.user_data, buf.as_mut_ptr(), buf.len()) };
        if n > 0 {
            // Out-of-contract n > cap is clamped defensively; a host doing
            // that is already broken, but it must not turn into a panic or
            // an out-of-bounds read here.
            Ok(n.min(buf.len() as i64) as usize)
        } else if n == 0 {
            Ok(0)
        } else if n == STREAM_TIMED_OUT {
            self.timed_out.set(true);
            Err(io::Error::new(
                io::ErrorKind::TimedOut,
                "winuxcmd stream: host stopped feeding (timed out)",
            ))
        } else {
            Err(io::Error::new(
                io::ErrorKind::Other,
                "winuxcmd stream: host read error",
            ))
        }
    }
}

// ---------------------------------------------------------------------
// Output side: ABI v3 host write callbacks.
// ---------------------------------------------------------------------

/// Host-side "write these bytes" callback (ABI v3; the C form is
/// `int (*)(void* user_data, const unsigned char* buf, size_t len)`).
///
/// Called only with `len >= 1`. Return: [`STREAM_WRITE_OK`] (`0`) when all
/// `len` bytes were written, [`STREAM_WRITE_BROKEN_PIPE`] (`-1`) when the
/// reader is gone, [`STREAM_WRITE_IO_ERROR`] (`-2`) on any other write
/// failure (the host records the cause; any other non-zero value is
/// reserved and treated as `-2`).
pub type StreamWriteFn =
    unsafe extern "C" fn(user_data: *mut c_void, buf: *const u8, len: usize) -> c_int;

/// Write callback return: every byte was written.
pub const STREAM_WRITE_OK: c_int = 0;

/// Write callback return: the reader is gone (broken pipe). Surfaces as
/// `io::ErrorKind::BrokenPipe`.
pub const STREAM_WRITE_BROKEN_PIPE: c_int = -1;

/// Write callback return: other host write failure (the host recorded the
/// Win32 cause). Surfaces as `io::Error::other`.
pub const STREAM_WRITE_IO_ERROR: c_int = -2;

/// `winuxcmd_io_v3.flags` bit: stdout is line-buffered (the host sets it
/// when stdout is an interactive console), else block-buffered.
pub const IO_STDOUT_LINE_BUFFERED: u32 = 0x1;

/// Block-buffer capacity for a non-interactive stdout (and the line
/// buffer's capacity for an over-long partial line).
pub const STDOUT_BUFFER_CAPACITY: usize = 64 * 1024;

/// Wrap a host [`StreamWriteFn`] + opaque `user_data` as [`Write`].
///
/// `None` (a NULL callback) discards every byte successfully. The first
/// host failure is sticky: later writes fail with the same error kind and
/// never call the host again. `flush` is a no-op (the host owns its
/// handle's flushing). Raw pointer members make it `!Send + !Sync`
/// (single-threaded model, same as [`StreamReader`]).
pub struct HostWriter {
    write_fn: Option<StreamWriteFn>,
    user_data: *mut c_void,
    failed: Cell<Option<io::ErrorKind>>,
    calls: Cell<u64>,
}

impl HostWriter {
    /// The caller guarantees `write_fn` (when `Some`) is a valid C-callable
    /// function that stays valid for every call made through this writer.
    pub fn new(write_fn: Option<StreamWriteFn>, user_data: *mut c_void) -> Self {
        HostWriter {
            write_fn,
            user_data,
            failed: Cell::new(None),
            calls: Cell::new(0),
        }
    }

    /// The sticky failure, if the host ever reported one.
    pub fn failure(&self) -> Option<io::ErrorKind> {
        self.failed.get()
    }

    /// How many times the host callback was invoked.
    pub fn host_calls(&self) -> u64 {
        self.calls.get()
    }

    fn sticky_error(kind: io::ErrorKind) -> io::Error {
        if kind == io::ErrorKind::BrokenPipe {
            io::Error::new(
                io::ErrorKind::BrokenPipe,
                "winuxcmd stream: host reader gone (broken pipe)",
            )
        } else {
            io::Error::other("winuxcmd stream: host write error")
        }
    }
}

impl Write for HostWriter {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        if let Some(kind) = self.failed.get() {
            return Err(Self::sticky_error(kind));
        }
        if buf.is_empty() {
            return Ok(0);
        }
        let Some(f) = self.write_fn else {
            return Ok(buf.len()); // NULL callback: discard
        };
        self.calls.set(self.calls.get() + 1);
        let rc = unsafe { f(self.user_data, buf.as_ptr(), buf.len()) };
        if rc == STREAM_WRITE_OK {
            return Ok(buf.len());
        }
        let kind = if rc == STREAM_WRITE_BROKEN_PIPE {
            io::ErrorKind::BrokenPipe
        } else {
            io::ErrorKind::Other
        };
        self.failed.set(Some(kind));
        Err(Self::sticky_error(kind))
    }

    fn flush(&mut self) -> io::Result<()> {
        Ok(())
    }
}

/// Line-mode inner writer: hands every complete line to the host as its
/// own callback (a multi-line `write` — seq's chunked output, a joined
/// record batch — becomes one callback per line), so an interactive
/// console sees each line the moment it is complete, exactly what C
/// stdio's `_IOLBF` promises per line. A trailing partial line is passed
/// on as one more callback (the outer [`LineWriter`] only hands one over
/// when its buffer is full or on flush).
pub struct LineSplit(pub HostWriter);

impl Write for LineSplit {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        let mut done = 0usize;
        while done < buf.len() {
            let rest = &buf[done..];
            let end = match rest.iter().position(|&b| b == b'\n') {
                Some(i) => i + 1,
                None => rest.len(),
            };
            match self.0.write(&rest[..end]) {
                Ok(n) => done += n,
                // Report what reached the host; the sticky failure fails
                // the caller's next write without another host call.
                Err(e) if done == 0 => return Err(e),
                Err(_) => break,
            }
        }
        Ok(done)
    }

    fn flush(&mut self) -> io::Result<()> {
        self.0.flush()
    }
}

/// The boundary's stdout: line-buffered for an interactive console
/// ([`IO_STDOUT_LINE_BUFFERED`]), else block-buffered with
/// [`STDOUT_BUFFER_CAPACITY`]. Writes larger than the block buffer go
/// straight to the host (std `BufWriter` bypass), so a chunked producer
/// pays no extra copy.
pub enum StdoutWriter {
    Line(LineWriter<LineSplit>),
    Block(BufWriter<HostWriter>),
}

/// Build the boundary's stdout writer for `flags` (see [`StdoutWriter`]).
pub fn stdout_writer(
    write_fn: Option<StreamWriteFn>,
    user_data: *mut c_void,
    flags: u32,
) -> StdoutWriter {
    let host = HostWriter::new(write_fn, user_data);
    if flags & IO_STDOUT_LINE_BUFFERED != 0 {
        StdoutWriter::Line(LineWriter::with_capacity(
            STDOUT_BUFFER_CAPACITY,
            LineSplit(host),
        ))
    } else {
        StdoutWriter::Block(BufWriter::with_capacity(STDOUT_BUFFER_CAPACITY, host))
    }
}

impl StdoutWriter {
    /// The underlying host writer (failure state, call count).
    pub fn host(&self) -> &HostWriter {
        match self {
            StdoutWriter::Line(w) => &w.get_ref().0,
            StdoutWriter::Block(w) => w.get_ref(),
        }
    }
}

impl Write for StdoutWriter {
    fn write(&mut self, buf: &[u8]) -> io::Result<usize> {
        match self {
            StdoutWriter::Line(w) => w.write(buf),
            StdoutWriter::Block(w) => w.write(buf),
        }
    }

    fn write_all(&mut self, buf: &[u8]) -> io::Result<()> {
        match self {
            StdoutWriter::Line(w) => w.write_all(buf),
            StdoutWriter::Block(w) => w.write_all(buf),
        }
    }

    fn flush(&mut self) -> io::Result<()> {
        match self {
            StdoutWriter::Line(w) => w.flush(),
            StdoutWriter::Block(w) => w.flush(),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    static BYTE: u8 = b'x';

    unsafe extern "C" fn one_byte(_ud: *mut c_void, buf: *mut u8, _cap: usize) -> i64 {
        unsafe {
            *buf = BYTE;
        }
        1
    }

    unsafe extern "C" fn stop_now(_ud: *mut c_void, _buf: *mut u8, _cap: usize) -> i64 {
        STREAM_TIMED_OUT
    }

    unsafe extern "C" fn io_fail(_ud: *mut c_void, _buf: *mut u8, _cap: usize) -> i64 {
        STREAM_IO_ERROR
    }

    #[test]
    fn partial_reads_and_eof() {
        let mut r = StreamReader::new(one_byte, std::ptr::null_mut());
        let mut b = [0u8; 4];
        assert_eq!(r.read(&mut b).unwrap(), 1);
        assert_eq!(&b[..1], b"x");
    }

    #[test]
    fn empty_request_never_calls_host() {
        let mut r = StreamReader::new(stop_now, std::ptr::null_mut());
        let mut b = [0u8; 0];
        // Would return TimedOut if the host were called.
        assert_eq!(r.read(&mut b).unwrap(), 0);
        assert!(!r.timed_out());
    }

    #[test]
    fn timed_out_sets_flag_and_error_kind() {
        let mut r = StreamReader::new(stop_now, std::ptr::null_mut());
        let mut b = [0u8; 8];
        let e = r.read(&mut b).unwrap_err();
        assert_eq!(e.kind(), io::ErrorKind::TimedOut);
        assert!(r.timed_out());
        assert_eq!(STREAM_TIMEOUT_STATUS, 124);
    }

    #[test]
    fn io_error_maps_to_other() {
        let mut r = StreamReader::new(io_fail, std::ptr::null_mut());
        let mut b = [0u8; 8];
        assert_eq!(r.read(&mut b).unwrap_err().kind(), io::ErrorKind::Other);
        assert!(!r.timed_out());
    }

    // ---- output side (ABI v3) ----

    #[derive(Default)]
    struct Sink {
        chunks: Vec<Vec<u8>>,
        rc: c_int,
    }

    unsafe extern "C" fn sink_write(ud: *mut c_void, buf: *const u8, len: usize) -> c_int {
        let s = unsafe { &mut *(ud as *mut Sink) };
        let bytes = unsafe { std::slice::from_raw_parts(buf, len) };
        s.chunks.push(bytes.to_vec());
        s.rc
    }

    #[test]
    fn host_writer_ok_and_empty_write_never_calls_host() {
        let mut sink = Sink::default();
        let mut w = HostWriter::new(Some(sink_write), &mut sink as *mut Sink as *mut c_void);
        assert_eq!(w.write(b"").unwrap(), 0);
        assert_eq!(w.host_calls(), 0);
        w.write_all(b"abc").unwrap();
        assert_eq!(w.host_calls(), 1);
        drop(w);
        assert_eq!(sink.chunks, vec![b"abc".to_vec()]);
    }

    #[test]
    fn host_writer_null_callback_discards() {
        let mut w = HostWriter::new(None, std::ptr::null_mut());
        assert_eq!(w.write(b"discard me").unwrap(), 10);
        assert!(w.failure().is_none());
    }

    #[test]
    fn host_writer_broken_pipe_is_sticky_without_host_calls() {
        let mut sink = Sink {
            rc: STREAM_WRITE_BROKEN_PIPE,
            ..Sink::default()
        };
        let mut w = HostWriter::new(Some(sink_write), &mut sink as *mut Sink as *mut c_void);
        assert_eq!(w.write(b"x").unwrap_err().kind(), io::ErrorKind::BrokenPipe);
        for _ in 0..100 {
            assert_eq!(w.write(b"y").unwrap_err().kind(), io::ErrorKind::BrokenPipe);
        }
        assert_eq!(w.host_calls(), 1);
        assert_eq!(w.failure(), Some(io::ErrorKind::BrokenPipe));
        drop(w);
        assert_eq!(sink.chunks.len(), 1);
    }

    #[test]
    fn host_writer_io_error_maps_to_other_and_reserved_codes_too() {
        for rc in [STREAM_WRITE_IO_ERROR, -7, 3] {
            let mut sink = Sink {
                rc,
                ..Sink::default()
            };
            let mut w = HostWriter::new(Some(sink_write), &mut sink as *mut Sink as *mut c_void);
            assert_eq!(w.write(b"x").unwrap_err().kind(), io::ErrorKind::Other);
            assert_eq!(w.write(b"x").unwrap_err().kind(), io::ErrorKind::Other);
            assert_eq!(w.host_calls(), 1);
        }
    }

    #[test]
    fn line_mode_delivers_one_callback_per_line() {
        let mut sink = Sink::default();
        let mut w = stdout_writer(
            Some(sink_write),
            &mut sink as *mut Sink as *mut c_void,
            IO_STDOUT_LINE_BUFFERED,
        );
        w.write_all(b"1\n2\n3\npartial").unwrap();
        assert_eq!(w.host().host_calls(), 3);
        w.flush().unwrap();
        drop(w);
        assert_eq!(
            sink.chunks,
            vec![
                b"1\n".to_vec(),
                b"2\n".to_vec(),
                b"3\n".to_vec(),
                b"partial".to_vec()
            ]
        );
    }

    #[test]
    fn block_mode_coalesces_until_flush_and_bypasses_big_writes() {
        let mut sink = Sink::default();
        let mut w = stdout_writer(Some(sink_write), &mut sink as *mut Sink as *mut c_void, 0);
        w.write_all(b"1\n").unwrap();
        w.write_all(b"2\n").unwrap();
        assert_eq!(w.host().host_calls(), 0);
        w.flush().unwrap();
        assert_eq!(w.host().host_calls(), 1);
        let big = vec![b'z'; STDOUT_BUFFER_CAPACITY + 1];
        w.write_all(&big).unwrap();
        assert_eq!(w.host().host_calls(), 2);
        drop(w);
        assert_eq!(sink.chunks[0], b"1\n2\n".to_vec());
        assert_eq!(sink.chunks[1].len(), STDOUT_BUFFER_CAPACITY + 1);
    }

    #[test]
    fn line_mode_failure_mid_batch_stops_at_the_failing_line() {
        let mut sink = Sink {
            rc: STREAM_WRITE_BROKEN_PIPE,
            ..Sink::default()
        };
        let mut w = stdout_writer(
            Some(sink_write),
            &mut sink as *mut Sink as *mut c_void,
            IO_STDOUT_LINE_BUFFERED,
        );
        assert_eq!(
            w.write_all(b"1\n2\n3\n").unwrap_err().kind(),
            io::ErrorKind::BrokenPipe
        );
        assert_eq!(w.host().host_calls(), 1);
        let _ = w.flush();
        assert_eq!(w.host().host_calls(), 1);
    }

    // Single-threaded model, structural note: StreamReader holds a raw
    // `*mut c_void`, which per std's blanket impls is `!Send + !Sync`, so
    // the reader cannot cross threads without an unsafe opt-in. There is no
    // stable way to *assert* a negative bound in a test; replacing the raw
    // pointer with an integer type would silently drop the guarantee, which
    // is why the field stays a pointer.
}
