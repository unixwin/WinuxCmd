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
// cwd arrive as explicit UTF-8 C strings, stdin as an explicit byte buffer,
// stdout/stderr come back as Rust-owned buffers freed with
// winuxcmd_free_buf(). No CRT fd, no process stdio crosses the boundary; a
// Rust panic is caught at the boundary and returned as status 125.
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

// Release a buffer handed out by winuxcmd_run / winuxcmd_run_stream (safe
// on an empty buffer).
void winuxcmd_free_buf(winuxcmd_cbuf* buf);

// 1 when `name` is in the rust2 registry, else 0.
int winuxcmd_has_command(const char* name);
}

// Version of the ABI this header describes (lib.rs winuxcmd_abi_version()).
// 2 since the C++ bridge adopted winuxcmd_run_stream (#1556 stream-ABI
// adoption, #1488): v1 symbols and semantics are unchanged, v2 is additive.
#define WINUXCMD_RUST2_ABI_VERSION 2
