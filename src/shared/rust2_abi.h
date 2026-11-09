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

// Release a buffer handed out by winuxcmd_run (safe on an empty buffer).
void winuxcmd_free_buf(winuxcmd_cbuf* buf);

// 1 when `name` is in the rust2 registry, else 0.
int winuxcmd_has_command(const char* name);
}

// Version of the ABI this header describes (lib.rs winuxcmd_abi_version()).
#define WINUXCMD_RUST2_ABI_VERSION 1
