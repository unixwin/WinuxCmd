//! Streaming stdin for the ABI boundary (#1488, #1550 follow-up).
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

use std::cell::Cell;
use std::ffi::c_void;
use std::io::{self, Read};

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

    // Single-threaded model, structural note: StreamReader holds a raw
    // `*mut c_void`, which per std's blanket impls is `!Send + !Sync`, so
    // the reader cannot cross threads without an unsafe opt-in. There is no
    // stable way to *assert* a negative bound in a test; replacing the raw
    // pointer with an integer type would silently drop the guarantee, which
    // is why the field stays a pointer.
}
