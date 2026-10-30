/*
 * MIT License
 *
 * Copyright (c) 2024-2026 WinuxCmd contributors
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: winux_ffi.h
 *  - WinuxCmd FFI ABI v2 (winuxcore.dll)
 *
 * This header replaces the removed v1 ABI (winux_execute). The v1 shape is
 * deliberately NOT revived: it had no env channel, no stdin channel, a cwd
 * parameter its callers always passed as NULL, full buffer copy-back of
 * stdout/stderr (read-after-finish pipe deadlock), and a synchronous call
 * with no cancellation object. ABI v2 negates each of those by construction:
 *
 *  - argv is carried as a wide-char vector, passed through byte-for-byte;
 *    the host never rewrites, reorders, or annotates arguments.
 *  - env is carried explicitly as a complete wide env block (including any
 *    prefix assignments the executor resolved); the DLL borrows it into the
 *    process environment for the duration of the call and restores it.
 *  - cwd is a required, absolute, non-NULL wide path; the DLL borrows it as
 *    the process current directory for the duration of the call.
 *  - stdin/stdout/stderr are raw OS HANDLEs borrowed from the host. There is
 *    no capture layer, no intermediate pipe, and no copy-back: applets read
 *    and write the host's already-resolved handles directly.
 *  - a cancellation callback lets the host report Ctrl+C-style intent;
 *    applets are only admitted to the FFI tiers when they are short-lived
 *    and interruptible by shape (see tiers.tsv), so this is cooperative.
 *
 * Ownership: every pointer in the context structs is borrowed. The host must
 * keep them alive and unmodified for the duration of the call. The DLL never
 * frees host memory and never retains pointers after a call returns.
 */

#ifndef WINUX_FFI_H
#define WINUX_FFI_H

#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
/* Non-Windows hosts only parse this header for tooling; handles degrade to
 * opaque pointers so the layout stays identical. */
typedef void *HANDLE;
typedef wchar_t WCHAR;
#endif

/* ABI version returned by winux_ffi_abi_version() and required in
 * winux_ffi_init_args.abi_version / winux_ffi_context.abi_version. */
#define WINUX_FFI_ABI_VERSION 2u

/* FFI-layer result codes. Applet exit codes (>= 0) pass through unchanged;
 * FFI-layer failures are always <= WINUX_FFI_ERR_GENERIC so the two ranges
 * can never collide. */
#define WINUX_FFI_ERR_GENERIC (-1000)     /* unspecified FFI-layer failure */
#define WINUX_FFI_ERR_INVALID_ARG (-1001) /* NULL/short/malformed struct */
#define WINUX_FFI_ERR_NOT_INITIALIZED (-1002)
#define WINUX_FFI_ERR_ABI_MISMATCH (-1003)
#define WINUX_FFI_ERR_UNKNOWN_APPLET (-1004)
#define WINUX_FFI_ERR_EXCEPTION (-1005) /* exception caught at the boundary */
#define WINUX_FFI_ERR_CANCELLED (-1006) /* cancel callback already set */

#ifdef __cplusplus
extern "C" {
#endif

/* Cancellation query callback supplied by the host.
 * Returns non-zero when the host wants the in-flight call to stop at the
 * next cooperative checkpoint. user_data is the opaque pointer from the
 * context. May be NULL (no cancellation source). */
typedef int (*winux_ffi_cancel_fn)(void *user_data);

typedef struct winux_ffi_init_args {
  uint32_t struct_size; /* sizeof(winux_ffi_init_args) */
  uint32_t abi_version; /* must equal WINUX_FFI_ABI_VERSION */
  /* Explicit install root of the WinuxCmd tree (absolute, non-NULL). The
   * DLL resolves its own data (i18n catalogs, tiers) from this root or from
   * its own module location, never from the host executable's path and
   * never from the current directory. */
  const WCHAR *install_root;
  /* Locale snapshot taken by the host at init time: the effective value of
   * WINUX_LANG, else LC_ALL, else LANG (may be NULL when all are unset).
   * Calls compare the live env stamp against this snapshot so catalog
   * reloads track the spawn form, which re-reads them per process. */
  const WCHAR *locale_stamp;
} winux_ffi_init_args;

typedef struct winux_ffi_context {
  uint32_t struct_size; /* sizeof(winux_ffi_context) */
  uint32_t abi_version; /* must equal WINUX_FFI_ABI_VERSION */
  /* Applet (command) name, wide, non-NULL. Selects the entry in the
   * CommandRegistry; routing/tiering decisions belong to the host. */
  const WCHAR *applet;
  /* Argument vector EXCLUDING the applet name (same convention as the
   * removed v1 ABI and CommandRegistry::dispatch). Wide strings, passed
   * through byte-for-byte. argv may be NULL when argc == 0. */
  int32_t argc;
  const WCHAR *const *argv;
  /* Complete environment for this call as a wide double-NUL-terminated
   * block of KEY=VALUE entries, including prefix assignments resolved by
   * the executor. Non-NULL; an empty block is a single NUL pair (L"\0").
   * The DLL applies the delta against the process environment under the
   * FFI lock and restores the original afterwards. */
  const WCHAR *env_block;
  /* Working directory for this call: absolute, non-NULL. Borrowed as the
   * process current directory for the duration of the call. */
  const WCHAR *cwd;
  /* Host stdio handles, borrowed. The DLL installs them as the process
   * std handles for the duration of the call (when they differ) and the
   * applet reads/writes them directly. No capturing is performed. Any of
   * them may be NULL, meaning "keep the current process std handle". */
  HANDLE stdin_handle;
  HANDLE stdout_handle;
  HANDLE stderr_handle;
  /* Cooperative cancellation source; may be NULL. */
  winux_ffi_cancel_fn cancel_check;
  void *cancel_user_data;
} winux_ffi_context;

/* Returns WINUX_FFI_ABI_VERSION. Must be checked by the host before any
 * other call. Never fails. */
uint32_t winux_ffi_abi_version(void);

/* One-time initialization. Idempotent for identical arguments; a second
 * call with a different install root or ABI version fails with
 * WINUX_FFI_ERR_ABI_MISMATCH / WINUX_FFI_ERR_INVALID_ARG. Returns 0 on
 * success or a negative WINUX_FFI_ERR_* code. */
int winux_ffi_init(const winux_ffi_init_args *args);

/* Execute one applet in-process. Returns the applet's exit code (>= 0) or
 * a negative WINUX_FFI_ERR_* code when the FFI layer itself failed (the
 * host must then fall back to spawning the external form). The entry point
 * catches every C++ exception at the DLL boundary and reports
 * WINUX_FFI_ERR_EXCEPTION with a note on the call's stderr handle; no
 * exception, and no exit()/abort() by contract, crosses this boundary. */
int winux_ffi_call(const winux_ffi_context *ctx);

#ifdef __cplusplus
}
#endif

#endif /* WINUX_FFI_H */
