//! winuxcmd-driver: the thin CLI adapter used by the differential gate.
//!
//! This is the ONLY place process state is read: argv comes from
//! `env::args_os`, env from `ProcessEnv`, cwd from `env::current_dir`,
//! stdio from the process handles. The command itself runs through the same
//! `winux_cmds::dispatch` the cdylib ABI uses, so the gate exercises the
//! library semantics, not a separate implementation.
//!
//! Modes (first argument selects; the gate uses only the default):
//! * default `driver <cmd> args...` — direct dispatch through
//!   `winux_cmds::dispatch`, process stdio as the injected handles.
//! * `driver --abi <cmd> args...` — go through the ABI v1 entry
//!   (`winuxcmd_run`): stdin is slurped into one whole buffer, exactly the
//!   shape a v1 host produces, so the streaming comparison has a baseline.
//! * `driver --abi-stream [--feed-chunk N] <cmd> args...` — go through the
//!   ABI v2 entry (`winuxcmd_run_stream`): process stdin is fed to the
//!   command through the host pull callback. `--feed-chunk N` clamps every
//!   callback return to N bytes (uneven-segmentation exercise; default is
//!   whatever the underlying read returns).
//! * `--mem-report FILE` — after the run, write the process's
//!   `PeakWorkingSetSize` / `PeakPagefileUsage` (GetProcessMemoryInfo) to
//!   FILE; the 64 MiB streaming ledger (`gate/stream-check.sh`) compares
//!   whole-buffer vs streaming peaks with it.

use std::ffi::{c_char, c_int, c_void, CString};
use std::io::{Read, Write};

use winux_cmds::tail::{StdinKind, StdinMeta};
use winux_core::stream::StreamReadFn;
use winux_core::{Ctx, EnvSource, Io, ProcessEnv};
use winuxcmd_ffi::{winuxcmd_free_buf, winuxcmd_run, winuxcmd_run_stream, CBuf};

/// tail's stdin classification, composed from the process stdin handle.
///
/// The driver is the only place process state is read (the shape
/// contract's sanctioned process-IO tier): the library receives the
/// result as injected `tail::StdinMeta` — GNU learns the same facts from
/// `fstat(0)` — and never touches the process handle itself. A redirected
/// regular file also yields its final path so `tail -f -` can follow the
/// file by descriptor like GNU's fd 0; the consumed offset is not part of
/// the metadata (the command derives it from its own injected reads).
#[cfg(windows)]
fn tail_stdin_meta() -> StdinMeta {
    use std::os::windows::io::AsRawHandle;
    type Handle = *mut c_void;
    #[link(name = "kernel32")]
    extern "system" {
        fn GetFileType(h: Handle) -> u32;
        fn GetConsoleMode(h: Handle, mode: *mut u32) -> i32;
        fn GetFinalPathNameByHandleW(h: Handle, buf: *mut u16, cap: u32, flags: u32) -> u32;
    }
    const FILE_TYPE_DISK: u32 = 1;
    const FILE_TYPE_CHAR: u32 = 2;
    const FILE_TYPE_PIPE: u32 = 3;
    let h = std::io::stdin().as_raw_handle() as Handle;
    if h.is_null() {
        return StdinMeta {
            kind: StdinKind::Unknown,
            follow_path: None,
        };
    }
    match unsafe { GetFileType(h) } {
        FILE_TYPE_DISK => {
            let mut buf = vec![0u16; 32768];
            let n = unsafe { GetFinalPathNameByHandleW(h, buf.as_mut_ptr(), 32768, 0) };
            let follow_path = if n != 0 && (n as usize) < buf.len() {
                Some(std::path::PathBuf::from(String::from_utf16_lossy(
                    &buf[..n as usize],
                )))
            } else {
                None
            };
            StdinMeta {
                kind: StdinKind::Disk,
                follow_path,
            }
        }
        FILE_TYPE_PIPE => StdinMeta::pipe_stream(),
        FILE_TYPE_CHAR => {
            let mut mode = 0u32;
            let console = unsafe { GetConsoleMode(h, &mut mode) } != 0;
            StdinMeta {
                kind: if console {
                    StdinKind::Console
                } else {
                    StdinKind::CharDev
                },
                follow_path: None,
            }
        }
        _ => StdinMeta {
            kind: StdinKind::Unknown,
            follow_path: None,
        },
    }
}

#[cfg(not(windows))]
fn tail_stdin_meta() -> StdinMeta {
    StdinMeta::pipe_stream()
}

fn main() {
    let mut argv: Vec<std::ffi::OsString> = std::env::args_os().skip(1).collect();
    if argv.is_empty() {
        eprintln!("winuxcmd-driver: missing command name");
        std::process::exit(2);
    }

    // Mode flags.
    let mut abi_v1 = false;
    let mut abi_stream = false;
    let mut feed_chunk: Option<usize> = None;
    let mut mem_report: Option<std::path::PathBuf> = None;
    while !argv.is_empty() {
        let flag = argv[0].to_string_lossy().into_owned();
        match flag.as_str() {
            "--abi" => {
                abi_v1 = true;
                argv.remove(0);
            }
            "--abi-stream" => {
                abi_stream = true;
                argv.remove(0);
            }
            "--feed-chunk" => {
                if argv.len() < 2 {
                    eprintln!("winuxcmd-driver: --feed-chunk needs a value");
                    std::process::exit(2);
                }
                feed_chunk = Some(argv[1].to_string_lossy().parse().unwrap_or_else(|_| {
                    eprintln!("winuxcmd-driver: bad --feed-chunk value");
                    std::process::exit(2);
                }));
                argv.remove(0);
                argv.remove(0);
            }
            "--mem-report" => {
                if argv.len() < 2 {
                    eprintln!("winuxcmd-driver: --mem-report needs a path");
                    std::process::exit(2);
                }
                mem_report = Some(std::path::PathBuf::from(&argv[1]));
                argv.remove(0);
                argv.remove(0);
            }
            _ => break,
        }
    }
    if (abi_v1 && abi_stream) || (feed_chunk.is_some() && !abi_stream) {
        eprintln!("winuxcmd-driver: conflicting mode flags");
        std::process::exit(2);
    }

    let status = if abi_v1 {
        run_abi_v1(&argv)
    } else if abi_stream {
        run_abi_stream(&argv, feed_chunk)
    } else {
        run_direct(&argv)
    };

    if let Some(path) = mem_report {
        write_mem_report(&path);
    }
    let _ = std::io::stdout().flush();
    let _ = std::io::stderr().flush();
    std::process::exit(status);
}

/// Default gate path: direct dispatch, process stdio injected as-is.
fn run_direct(argv: &[std::ffi::OsString]) -> i32 {
    if argv.is_empty() {
        eprintln!("winuxcmd-driver: missing command name");
        return 2;
    }
    let name = argv[0].to_string_lossy().into_owned();
    let args: Vec<std::ffi::OsString> = argv[1..].to_vec();

    let env = ProcessEnv;
    let cwd = std::env::current_dir().unwrap_or_default();
    let ctx = Ctx { cwd, env: &env };

    let stdin = std::io::stdin();
    let stdout = std::io::stdout();
    let stderr = std::io::stderr();
    let mut stdin_lock = stdin.lock();
    let mut stdout_lock = stdout.lock();
    let mut stderr_lock = stderr.lock();
    let mut io = Io {
        stdin: &mut stdin_lock,
        stdout: &mut stdout_lock,
        stderr: &mut stderr_lock,
    };

    let status = if name == "tail" {
        // tail's stdin class is input metadata (GNU's fstat(0) facts):
        // composed above from the process handle, injected into the
        // library. Every other command shapes stdin through the Io
        // handles alone.
        let meta = tail_stdin_meta();
        match winux_cmds::tail::tail_with_meta(&args, &mut io, &ctx, &meta) {
            Ok(s) => s as i32,
            Err(_) => winux_core::ABI_FAILURE_STATUS as i32,
        }
    } else {
        match winux_cmds::dispatch(&name, &args, &mut io, &ctx) {
            Some(Ok(s)) => s as i32,
            Some(Err(_)) => winux_core::ABI_FAILURE_STATUS as i32,
            None => {
                let _ = writeln!(stderr_lock, "winuxcmd-driver: unknown command: {}", name);
                127
            }
        }
    };
    let _ = stdout_lock.flush();
    let _ = stderr_lock.flush();
    status
}

fn c_string(s: &str) -> CString {
    CString::new(s.replace('\0', "")).unwrap_or_default()
}

/// The explicit envp block from the process environment (name=value,
/// NULL-terminated), mirroring what the C++ bridge snapshots. `owned`
/// keeps the strings alive for the pointers; it is read only by the ABI.
struct EnvBlock {
    #[allow(dead_code)]
    owned: Vec<CString>,
    ptrs: Vec<*const c_char>,
}

fn env_block() -> EnvBlock {
    let env = ProcessEnv;
    let owned: Vec<CString> = env
        .entries()
        .into_iter()
        .map(|(k, v)| c_string(&format!("{}={}", k.to_string_lossy(), v.to_string_lossy())))
        .collect();
    let mut ptrs: Vec<*const c_char> = owned.iter().map(|c| c.as_ptr()).collect();
    ptrs.push(std::ptr::null());
    EnvBlock { owned, ptrs }
}

fn drain_cbuf(buf: &mut CBuf) -> Vec<u8> {
    let slice = unsafe {
        if (*buf).ptr.is_null() {
            &[][..]
        } else {
            std::slice::from_raw_parts((*buf).ptr, (*buf).len)
        }
    };
    let v = slice.to_vec();
    unsafe { winuxcmd_free_buf(buf) };
    v
}

/// ABI v1 path: slurp process stdin into one whole buffer and hand it over
/// — the exact shape a v1 host produces.
fn run_abi_v1(argv: &[std::ffi::OsString]) -> i32 {
    if argv.is_empty() {
        eprintln!("winuxcmd-driver: missing command name");
        return 2;
    }
    let name = c_string(&argv[0].to_string_lossy());
    let arg_c: Vec<CString> = argv[1..]
        .iter()
        .map(|a| c_string(&a.to_string_lossy()))
        .collect();
    let arg_ptrs: Vec<*const c_char> = arg_c.iter().map(|c| c.as_ptr()).collect();
    let env = env_block();
    let cwd = c_string(
        &std::env::current_dir()
            .unwrap_or_default()
            .to_string_lossy(),
    );

    let mut input = Vec::new();
    if std::io::stdin().lock().read_to_end(&mut input).is_err() {
        eprintln!("winuxcmd-driver: stdin read error");
        return 125;
    }

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
            name.as_ptr(),
            arg_c.len() as c_int,
            arg_ptrs.as_ptr(),
            env.ptrs.as_ptr(),
            cwd.as_ptr(),
            if input.is_empty() {
                std::ptr::null()
            } else {
                input.as_ptr()
            },
            input.len(),
            &mut out,
            &mut err,
        )
    };
    emit_outputs(drain_cbuf(&mut out), drain_cbuf(&mut err));
    status
}

struct FeedState {
    stdin: std::io::StdinLock<'static>,
    chunk: Option<usize>,
    failed: bool,
}

/// The host pull callback: one read from process stdin, clamped to the
/// requested --feed-chunk. Raw-pointer context is fine here: the callback
/// runs only inside the single winuxcmd_run_stream call on this thread.
unsafe extern "C" fn feed_stdin(ud: *mut c_void, buf: *mut u8, cap: usize) -> i64 {
    let st = &mut *(ud as *mut FeedState);
    if cap == 0 {
        return 0;
    }
    let want = st.chunk.map_or(cap, |c| c.min(cap)).max(1);
    let slice = std::slice::from_raw_parts_mut(buf, want);
    match st.stdin.read(slice) {
        Ok(0) => 0,
        Ok(n) => n as i64,
        Err(_) => {
            st.failed = true;
            winux_core::stream::STREAM_IO_ERROR
        }
    }
}

/// ABI v2 path: process stdin flows through the streaming pull callback;
/// nothing is slurped.
fn run_abi_stream(argv: &[std::ffi::OsString], feed_chunk: Option<usize>) -> i32 {
    if argv.is_empty() {
        eprintln!("winuxcmd-driver: missing command name");
        return 2;
    }
    let name = c_string(&argv[0].to_string_lossy());
    let arg_c: Vec<CString> = argv[1..]
        .iter()
        .map(|a| c_string(&a.to_string_lossy()))
        .collect();
    let arg_ptrs: Vec<*const c_char> = arg_c.iter().map(|c| c.as_ptr()).collect();
    let env = env_block();
    let cwd = c_string(
        &std::env::current_dir()
            .unwrap_or_default()
            .to_string_lossy(),
    );

    // 'static lock: process stdin lives as long as the process; the
    // callback context pointer is only dereferenced inside the one
    // winuxcmd_run_stream call below.
    let stdin: std::io::StdinLock<'static> = std::io::stdin().lock();
    let mut state = FeedState {
        stdin,
        chunk: feed_chunk,
        failed: false,
    };

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
        winuxcmd_run_stream(
            name.as_ptr(),
            arg_c.len() as c_int,
            arg_ptrs.as_ptr(),
            env.ptrs.as_ptr(),
            cwd.as_ptr(),
            Some(feed_stdin as StreamReadFn),
            &mut state as *mut FeedState as *mut c_void,
            &mut out,
            &mut err,
        )
    };
    emit_outputs(drain_cbuf(&mut out), drain_cbuf(&mut err));
    if state.failed && status == 0 {
        // The host read failed and the command did not notice (read into
        // the void); do not report success for a truncated stream.
        return winux_core::ABI_FAILURE_STATUS as i32;
    }
    status
}

/// Raw bytes out; no CRLF translation anywhere (Rust std stdout does none).
fn emit_outputs(out: Vec<u8>, err: Vec<u8>) {
    let mut so = std::io::stdout().lock();
    let mut se = std::io::stderr().lock();
    let _ = so.write_all(&out);
    let _ = se.write_all(&err);
    let _ = so.flush();
    let _ = se.flush();
}

// ---------- memory peak report (Windows; coarse by design) ----------

#[cfg(windows)]
mod mem {
    use std::ffi::c_void;

    #[repr(C)]
    pub struct ProcessMemoryCounters {
        pub cb: u32,
        pub page_fault_count: u32,
        pub peak_working_set: usize,
        pub working_set: usize,
        pub quota_peak_paged_pool: usize,
        pub quota_paged_pool: usize,
        pub quota_peak_non_paged_pool: usize,
        pub quota_non_paged_pool: usize,
        pub pagefile_usage: usize,
        pub peak_pagefile_usage: usize,
    }

    #[link(name = "kernel32")]
    extern "system" {
        fn GetCurrentProcess() -> *mut c_void;
        fn K32GetProcessMemoryInfo(
            process: *mut c_void,
            counters: *mut ProcessMemoryCounters,
            cb: u32,
        ) -> i32;
    }

    /// Returns (peak_working_set_bytes, peak_pagefile_bytes) for this
    /// process, or None if the query fails.
    pub fn peaks() -> Option<(usize, usize)> {
        unsafe {
            let mut pmc = ProcessMemoryCounters {
                cb: std::mem::size_of::<ProcessMemoryCounters>() as u32,
                page_fault_count: 0,
                peak_working_set: 0,
                working_set: 0,
                quota_peak_paged_pool: 0,
                quota_paged_pool: 0,
                quota_peak_non_paged_pool: 0,
                quota_non_paged_pool: 0,
                pagefile_usage: 0,
                peak_pagefile_usage: 0,
            };
            if K32GetProcessMemoryInfo(
                GetCurrentProcess(),
                &mut pmc,
                std::mem::size_of::<ProcessMemoryCounters>() as u32,
            ) != 0
            {
                Some((pmc.peak_working_set, pmc.peak_pagefile_usage))
            } else {
                None
            }
        }
    }
}

/// Write the coarse memory-peak report after the run (peaks are monotonic,
/// so sampling once at the end captures the whole run).
fn write_mem_report(path: &std::path::Path) {
    let mut body = String::new();
    #[cfg(windows)]
    {
        if let Some((ws, pf)) = mem::peaks() {
            body.push_str(&format!("peak_working_set_bytes: {ws}\n"));
            body.push_str(&format!("peak_pagefile_bytes: {pf}\n"));
        }
    }
    if body.is_empty() {
        body.push_str("peak_working_set_bytes: unsupported\n");
        body.push_str("peak_pagefile_bytes: unsupported\n");
    }
    if let Ok(mut f) = std::fs::File::create(path) {
        let _ = f.write_all(body.as_bytes());
    }
}
