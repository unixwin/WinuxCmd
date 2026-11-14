// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
// C declarations of the rust2 command-library ABI (WinuxCmd 2.0, #1488).
//
// The definitions live in rust2/crates/winuxcmd-ffi/src/lib.rs and reach
// winuxcmd.exe as the staticlib `winuxcmd_ffi.lib` (CMake option
// WINUXCMD_ENABLE_RUST2). This header is the C++ mirror of that ABI and
// must change in the same commit as lib.rs (bump winuxcmd_abi_version()).
//
// ABI rules (#1488 "ABI 面只传显式句柄/缓冲，不传环境状态"): argv, envp and
// cwd arrive as explicit UTF-8 C strings; stdio crosses as plain bytes —
// v1: stdin as an explicit byte buffer, stdout/stderr as Rust-owned buffers
// freed with winuxcmd_free_buf(); v2: stdin through a host pull callback;
// v3 (winuxcmd_run_io, what the C++ bridge uses): stdin pull + stdout/stderr
// host write callbacks, streamed while the command runs. No CRT fd, no
// process stdio crosses the boundary; a Rust panic is caught at the
// boundary and returned as status 125.
//
// Plain C types only (no std:: names): this header is included from a
// module's global fragment, and the command TUs take their std from the
// PCH + `import std` (see the note in argv_adapters.h).
#pragma once

#include <stddef.h>

extern "C" {

// Mirror of rust2 `#[repr(C)] struct CBuf { ptr, len, cap }`.
struct winuxcmd_cbuf {
  unsigned char* ptr;
  size_t len;
  size_t cap;
};

// ABI surface version; the bridge refuses to run on a mismatch.
int winuxcmd_abi_version(void);

// Run command `name` with `argc` arguments (argv[0] NOT included), a
// NULL-terminated `name=value` envp array, an explicit cwd, and an explicit
// stdin buffer (may be NULL/0). Returns the command's status, or 125 on
// panic / injected-handle IO error / unknown command.
int winuxcmd_run(const char* name, int argc, const char* const* argv,
                 const char* const* envp, const char* cwd,
                 const unsigned char* stdin_buf, size_t stdin_len,
                 winuxcmd_cbuf* stdout_buf, winuxcmd_cbuf* stderr_buf);

// Host-side "fill this buffer" stdin pull callback (ABI v2, #1556; the
// Rust mirror is winux_core::stream::StreamReadFn). The boundary calls it
// synchronously on the caller's thread whenever the command asks for more
// input; `user_data` is passed through opaquely and never dereferenced by
// Rust. `buf` is host-owned and borrowed for the duration of ONE call.
// Returns:
//   > 0   bytes filled (must be <= cap; partial reads are legal, no minimum)
//   0     end of stream
//  -1     host stopped feeding: timeout / interrupt (the run returns 124)
//  -2     host read failure (any other negative is reserved, same as -2)
typedef long long winuxcmd_stream_read_fn(void* user_data, unsigned char* buf,
                                          size_t cap);

// Like winuxcmd_run, but stdin arrives through the host pull callback
// (ABI v2, additive over v1). `stdin_read` may be NULL: empty stdin, same
// as `stdin_buf == NULL` in v1. stdout/stderr keep the v1 shape: Rust-owned
// buffers freed with winuxcmd_free_buf. Returns the command's status; 124
// when the host reported -1 at any point (GNU timeout(1) convention, even
// if the command swallowed the read error); 125 on panic / handle IO error
// / unknown command. A -2 propagates per the command's own contract (e.g.
// tac reports the read error and exits 1). Rust never touches a CRT fd or
// process stdio: the host reads its own console/pipe/file (#1487 red line).
int winuxcmd_run_stream(const char* name, int argc, const char* const* argv,
                        const char* const* envp, const char* cwd,
                        winuxcmd_stream_read_fn* stdin_read,
                        void* stdin_user_data, winuxcmd_cbuf* stdout_buf,
                        winuxcmd_cbuf* stderr_buf);

// Host-side "write these bytes" push callback (ABI v3; the Rust mirror is
// winux_core::stream::StreamWriteFn). Called synchronously on the caller's
// thread while the command runs, only with len >= 1; `buf` is Rust-owned
// and borrowed for the duration of ONE call (the host writes or copies it
// before returning and never stores the pointer). Returns:
//    0   all `len` bytes written
//   -1   broken pipe: the reader is gone (surfaces to the command as
//        io::ErrorKind::BrokenPipe)
//   -2   any other write failure; the host recorded the Win32 cause (any
//        other non-zero value is reserved and treated as -2)
// The first failure is sticky: Rust never calls that stream's callback
// again during the run, so an unbounded producer stops at its next write.
typedef int winuxcmd_stream_write_fn(void* user_data, const unsigned char* buf,
                                     size_t len);

// winuxcmd_io_v3.flags: stdout is line-buffered (one callback per complete
// line) — set by the host iff stdout is an interactive console. Without
// it stdout is block-buffered (64 KiB; larger writes pass straight
// through). stderr is always unbuffered: every command write is one
// callback, so diagnostics reach the host in program order relative to
// already-flushed stdout.
#define WINUXCMD_IO_STDOUT_LINE_BUFFERED 0x1u

// The ABI v3 stdio table (Rust mirror: winuxcmd_ffi::WinuxcmdIoV3).
// NULL stdin_read = empty stdin; NULL stdout_write / stderr_write =
// discard. user_data pointers are passed through opaquely, never
// dereferenced by Rust.
struct winuxcmd_io_v3 {
  winuxcmd_stream_read_fn* stdin_read;
  void* stdin_user_data;
  winuxcmd_stream_write_fn* stdout_write;
  void* stdout_user_data;
  winuxcmd_stream_write_fn* stderr_write;
  void* stderr_user_data;
  unsigned flags;
};

// Run command `name` with every stdio stream through host callbacks (ABI
// v3, additive over v1/v2). `io` may be NULL (all-NULL table). stdout is
// flushed after the command returns (best-effort after a contained panic
// too). Returns the same status as v2: the command's own status; 124 when
// the stdin callback reported -1; 125 on panic / handle IO error /
// unknown command. Write-callback failures do NOT change the returned
// status — the host saw every callback result and applies its own mapping
// (the C++ bridge: a -1 anywhere on stdout -> 141, GNU's SIGPIPE death;
// a -2 -> the closed-stdout "write error" contract, #1368).
int winuxcmd_run_io(const char* name, int argc, const char* const* argv,
                    const char* const* envp, const char* cwd,
                    const struct winuxcmd_io_v3* io);

// Release a buffer handed out by winuxcmd_run / winuxcmd_run_stream (safe
// on an empty buffer).
void winuxcmd_free_buf(winuxcmd_cbuf* buf);

// 1 when `name` is in the rust2 registry, else 0.
int winuxcmd_has_command(const char* name);
}

// Version of the ABI this header describes (lib.rs winuxcmd_abi_version()).
// 2 since the C++ bridge adopted winuxcmd_run_stream (#1556 stream-ABI
// adoption, #1488); 3 since winuxcmd_run_io (streaming output, host write
// callbacks). v1 and v2 symbols and semantics are unchanged; each step is
// additive.
#define WINUXCMD_RUST2_ABI_VERSION 3
