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
 *  - File: ffi.cpp
 *  - winuxcore.dll shell: FFI ABI v2 implementation.
 *
 * Thin shell over CommandRegistry::dispatch — command implementations are
 * not modified for FFI. Relative to the removed v1 shell (baseline
 * d62be40^:src/ffi/ffi.cpp) this file differs by construction:
 *   - no OutputCapture, no pipes, no copy-back: ctx stdio HANDLEs are
 *     installed as the process std handles and the CRT std fds are
 *     rebound to them (#1487-A), so applets use them directly;
 *   - C stdio is flushed across the borrow boundary with _flushall, so
 *     buffered output lands on the borrowed handles and prefetched stdin
 *     bytes cannot leak into the next call;
 *   - a watchdog thread polls the host's cancel callback while dispatch
 *     runs (#1487-B); the flag it raises is checked at applet loop
 *     boundaries and the call then reports WINUX_FFI_ERR_CANCELLED;
 *   - env and cwd are explicit ctx fields, borrowed into process state
 *     under the FFI lock for the call and restored afterwards;
 *   - calls are serialized by one process-level mutex (the borrow above
 *     mutates process-global state; see DESIGN §5);
 *   - the invocation argv is borrowed into the CRT globals __argc/__wargv
 *     for the call (see ArgvBorrow below);
 *   - every entry point is wrapped so no C++ exception crosses the DLL
 *     boundary (the dispatch chain is noexcept; an escaping exception
 *     would otherwise std::terminate the host).
 *
 * This translation unit is compiled /EHa (see CMakeLists.txt): the
 * borrow/lock guards below are RAII objects in winux_ffi_call's frame,
 * and an SEH fault raised inside dispatch must unwind THROUGH them so
 * their destructors run. Under /EHsc such an unwind skips the
 * destructors, which once left g_ffi_lock locked for the life of the
 * process (every later call failing "resource deadlock would occur").
 */

#include "winux_ffi.h"

#include <windows.h>

#include <fcntl.h>
#include <io.h>
#include <stdlib.h>

import std;
import core;
import utils;

// _flushall is spelled out instead of going through <stdio.h> / the std
// module: this TU is built with _CRT_DISABLE_PERFCRIT_LOCKS, under which
// the `fflush` token is macro-rewritten to _fflush_nolock (a name the std
// module does not export), so fflush is unspellable here — but _flushall
// is a separate CRT export and needs no header. It flushes every open
// output stream to its current file and clears every open input stream's
// buffer, which is exactly the boundary discipline the borrow below
// needs (see StdBorrow).
extern "C" int _flushall(void);

namespace {

std::mutex g_ffi_lock;
bool g_initialized = false;
std::wstring g_install_root;
std::wstring g_locale_stamp;

void flush_stdio_for_borrow_switch() {
  // C++ stream buffers first (they drain into the C streams), then every
  // C stream. Called while the handles being flushed to are still
  // installed: at apply time the previous call's leftovers go to the OLD
  // handles, at restore time this call's output goes to the BORROWED
  // handles. The input-buffer clear also discards any bytes a previous
  // call prefetched from a different stdin handle (std::cin read-ahead
  // would otherwise leak across calls once stdin is rebound per call).
  std::cout.flush();
  std::cerr.flush();
  _flushall();
}

// Cooperative cancellation (#1487 defect B): dispatch is one synchronous
// call and no DLL code runs while an applet works, so a helper thread
// polls the host's cancel callback and raises the shared flag that
// long-running applets check at loop boundaries. The thread is joined
// before the call returns, so it never outlives the borrowed ctx
// (cancel_user_data) or the in-flight call. Thread creation failure
// degrades to no mid-dispatch polling for that call rather than failing
// it: the pre-dispatch check in winux_ffi_call still applies.
struct CancelWatchdog {
  std::atomic<bool> done{false};
  std::thread worker;

  void start(winux_ffi_cancel_fn fn, void *user_data) {
    try {
      worker = std::thread([this, fn, user_data] {
        while (!done.load(std::memory_order_relaxed)) {
          // A throwing host callback must not terminate the host
          // process (an exception escaping a thread function would);
          // fail closed to "cancel" instead.
          bool cancelled = false;
          try {
            cancelled = fn(user_data) != 0;
          } catch (...) {
            cancelled = true;
          }
          if (cancelled) {
            request_ffi_cancel();
            return;
          }
          std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
      });
    } catch (...) {
      // No watchdog for this call; dispatch still runs.
    }
  }

  void stop() {
    done.store(true, std::memory_order_relaxed);
    if (worker.joinable()) worker.join();
  }

  ~CancelWatchdog() { stop(); }
};

bool is_absolute_wpath(const wchar_t *p) {
  if (!p || !*p) return false;
  if (p[0] == L'\\' && p[1] == L'\\') return true;  // UNC
  return ((p[0] >= L'a' && p[0] <= L'z') || (p[0] >= L'A' && p[0] <= L'Z')) &&
         p[1] == L':' && (p[2] == L'\\' || p[2] == L'/');
}

void write_diag(HANDLE stderr_handle, const char *msg) {
  HANDLE h = stderr_handle ? stderr_handle : GetStdHandle(STD_ERROR_HANDLE);
  if (!h || h == INVALID_HANDLE_VALUE || !msg) return;
  DWORD written = 0;
  WriteFile(h, msg, static_cast<DWORD>(std::strlen(msg)), &written, nullptr);
}

// --- env borrowing -----------------------------------------------------------
// Parse a double-NUL-terminated wide env block into (name, value) pairs.
// Entries without '=' keep an empty value; a leading '=' (drive-letter
// entries like =C:=...) is preserved as part of the name.
std::vector<std::pair<std::wstring, std::wstring>> parse_env_block(
    const wchar_t *block) {
  std::vector<std::pair<std::wstring, std::wstring>> out;
  const wchar_t *p = block;
  while (p && *p) {
    const wchar_t *end = p;
    while (*end) ++end;
    std::wstring entry(p, end);
    const size_t eq = entry.find(L'=', entry.starts_with(L'=') ? 1 : 0);
    if (eq == std::wstring::npos) {
      out.emplace_back(entry, L"");
    } else {
      out.emplace_back(entry.substr(0, eq), entry.substr(eq + 1));
    }
    p = end + 1;
  }
  return out;
}

std::vector<std::pair<std::wstring, std::wstring>> current_env_block() {
  std::vector<std::pair<std::wstring, std::wstring>> out;
  LPWCH raw = GetEnvironmentStringsW();
  if (!raw) return out;
  out = parse_env_block(raw);
  FreeEnvironmentStringsW(raw);
  return out;
}

// Applies the ctx env as the process environment; restores the previous
// process environment on destruction. Spawn-form equivalence: the child
// process would receive exactly the ctx block, so names absent from the
// block are removed for the duration of the call, then put back.
struct EnvBorrow {
  std::vector<std::pair<std::wstring, std::wstring>> saved;
  bool active = false;

  void apply(const wchar_t *block) {
    saved = current_env_block();
    const auto desired = parse_env_block(block);
    auto has = [&desired](const std::wstring &name) {
      return std::any_of(desired.begin(), desired.end(), [&name](auto &kv) {
        return _wcsicmp(kv.first.c_str(), name.c_str()) == 0;
      });
    };
    for (const auto &kv : saved) {
      if (!has(kv.first)) SetEnvironmentVariableW(kv.first.c_str(), nullptr);
    }
    for (const auto &kv : desired) {
      SetEnvironmentVariableW(kv.first.c_str(), kv.second.c_str());
    }
    active = true;
  }

  ~EnvBorrow() {
    if (!active) return;
    // Remove everything now present, then restore the saved block.
    for (const auto &kv : current_env_block()) {
      SetEnvironmentVariableW(kv.first.c_str(), nullptr);
    }
    for (const auto &kv : saved) {
      SetEnvironmentVariableW(kv.first.c_str(), kv.second.c_str());
    }
  }
};

// --- cwd borrowing -----------------------------------------------------------
struct CwdBorrow {
  std::wstring saved;
  bool active = false;

  void apply(const wchar_t *cwd) {
    const DWORD len = GetCurrentDirectoryW(0, nullptr);
    if (len) {
      saved.resize(len);
      const DWORD got = GetCurrentDirectoryW(len, saved.data());
      saved.resize(got);
    }
    active = SetCurrentDirectoryW(cwd) != 0;
  }

  ~CwdBorrow() {
    if (active && !saved.empty()) SetCurrentDirectoryW(saved.c_str());
  }
};

// --- std handle + CRT mode borrowing -----------------------------------------
// Installs the ctx handles as the process std handles (Win32 layer) AND
// rebinds this DLL's CRT std fds (0/1/2) to them, for the call.
//
// The Win32 half alone is NOT enough (defect #1487-A): applets read stdin
// through std::cin and write stdout through std::cout / fwrite(stdout),
// i.e. through the CRT fds — and a CRT fd stays bound to the OS handle it
// had when the CRT started, whatever SetStdHandle does afterwards. With
// only SetStdHandle, `sort < words.txt` read the host's console stdin
// (EOF -> wc printed 0, tee/tr saw nothing) and `sort words.txt > o.txt`
// wrote to the host's console stdout (36MB sprayed to the terminal,
// o.txt empty). The rebind below closes each std fd and re-opens it on a
// duplicate of the ctx handle, so the CRT streams follow the redirect.
//
// _close/_open_osfhandle transfer handle ownership to the fd: _close also
// closes the underlying OS handle. Both sides are therefore duplicated
// first, so the DLL never closes a handle the host owns:
//   - the fd's original handle is duplicated before _close (the original
//     value dies, but the object survives in the duplicate);
//   - the ctx handle is duplicated before _open_osfhandle (the fd owns the
//     duplicate; the host's value is never closed).
// Restore rebinds the fd to the saved original duplicate and restores the
// Win32 slot to that duplicate — same kernel object, different numeric
// value. The host MUST therefore re-query std handles via GetStdHandle
// after each call and never cache a raw HANDLE value across calls (see
// winux_ffi.h). The harness and niubash both build the ctx per call from
// GetStdHandle, so this holds for the in-tree hosts.
//
// Two further per-call resets live here:
//   - the console layer caches GetStdHandle results in thread_locals
//     (getStdOut/getStdErr) and the console-ness verdict; a long-lived
//     host thread poisons that cache with the console handle on its first
//     call, after which every safePrint writes to the stale handle no
//     matter what SetStdHandle says. invalidateCachedHandles() clears it.
//   - the broken-pipe/write-error flags are sticky thread_locals too and
//     no caller ever reset them (clear_pipe_closed_flags had no call
//     sites); a broken pipe in one call made later calls silently drop
//     stdout. They are reset per call.
// Previous handle and mode values are restored on destruction; the mode
// save/restore is the DLL side of the DESIGN §1.4 CRT contract (the host
// owns its own CRT's modes the same way).
struct StdBorrow {
  struct Slot {
    DWORD which;
    int fd;
    int open_flags;  // flags for _open_osfhandle on this fd
    HANDLE saved_handle = nullptr;  // Win32 slot value before the call
    bool handle_swapped = false;
    // True when the Win32 slot value was the fd's own OS handle at apply
    // time: _close then killed that value, so the slot must be restored
    // to the saved duplicate, not to the dead saved_handle.
    bool win32_slot_is_fd_handle = false;
    int saved_mode = -1;
    // Dup of the fd's original OS handle. Slot-owned until the fd takes
    // it on restore; null once the fd owns it.
    HANDLE saved_os_dup = nullptr;
    // Dup of the ctx handle. Owned by the fd while rebound; its value
    // dies with _close(fd) at restore time, never CloseHandle'd directly.
    HANDLE wanted_dup = nullptr;
    // True while the fd is bound to a DLL-owned duplicate (wanted_dup,
    // or a restore-time duplicate of the original).
    bool fd_rebound = false;
  };
  Slot slots[3] = {{STD_INPUT_HANDLE, 0, _O_RDONLY | _O_BINARY, nullptr,
                    false, false, -1},
                   {STD_OUTPUT_HANDLE, 1, _O_WRONLY | _O_BINARY, nullptr,
                    false, false, -1},
                   {STD_ERROR_HANDLE, 2, _O_WRONLY | _O_BINARY, nullptr,
                    false, false, -1}};

  // Bind the fd to a fresh duplicate of the original object (best effort,
  // used when the rebind to `want` fails after the fd was closed). The fd
  // takes ownership of the fresh duplicate; saved_os_dup stays Slot-owned
  // for the final restore.
  bool reopen_on_original(Slot &s) {
    if (!s.saved_os_dup) return false;
    HANDLE tmp = nullptr;
    if (!DuplicateHandle(GetCurrentProcess(), s.saved_os_dup,
                         GetCurrentProcess(), &tmp, 0, FALSE,
                         DUPLICATE_SAME_ACCESS)) {
      return false;
    }
    const int got = _open_osfhandle((intptr_t)tmp, s.open_flags);
    if (got == -1) {
      CloseHandle(tmp);
      return false;
    }
    if (got != s.fd) {
      _close(got);  // tmp is owned by that (free) fd now, not by us
      return false;
    }
    s.fd_rebound = true;
    return true;
  }

  // Rebind one fd to a duplicate of `want`. On success the fd owns the
  // duplicate; on failure the fd is left bound to a duplicate of the
  // original (or closed when there was none) and false is returned.
  bool rebind_fd(Slot &s, HANDLE want) {
    const intptr_t current = _get_osfhandle(s.fd);
    s.win32_slot_is_fd_handle =
        current != -1 && (HANDLE)current == s.saved_handle;
    if (current != -1 && (HANDLE)current == want) return true;  // already bound
    if (current != -1 && (HANDLE)current != INVALID_HANDLE_VALUE) {
      if (!DuplicateHandle(GetCurrentProcess(), (HANDLE)current,
                           GetCurrentProcess(), &s.saved_os_dup, 0, FALSE,
                           DUPLICATE_SAME_ACCESS)) {
        return false;
      }
      if (_close(s.fd) != 0) {
        CloseHandle(s.saved_os_dup);
        s.saved_os_dup = nullptr;
        return false;
      }
      // The fd is closed; the original VALUE is dead, the object
      // survives in saved_os_dup.
    }
    if (!DuplicateHandle(GetCurrentProcess(), want, GetCurrentProcess(),
                         &s.wanted_dup, 0, FALSE, DUPLICATE_SAME_ACCESS)) {
      reopen_on_original(s);  // best effort; the fd may stay closed
      return false;
    }
    const int got = _open_osfhandle((intptr_t)s.wanted_dup, s.open_flags);
    if (got == -1) {
      CloseHandle(s.wanted_dup);
      s.wanted_dup = nullptr;
      reopen_on_original(s);
      return false;
    }
    if (got != s.fd) {
      _close(got);  // wanted_dup is owned by that (free) fd now, not by us
      s.wanted_dup = nullptr;
      reopen_on_original(s);
      return false;
    }
    s.fd_rebound = true;
    return true;
  }

  bool apply(const winux_ffi_context *ctx) {
    // Drain the previous call's buffered output to the OLD handles and
    // drop any prefetched stdin bytes before the handles change.
    flush_stdio_for_borrow_switch();
    const HANDLE wanted[3] = {ctx->stdin_handle, ctx->stdout_handle,
                              ctx->stderr_handle};
    for (int i = 0; i < 3; ++i) {
      Slot &s = slots[i];
      if (wanted[i] && wanted[i] != INVALID_HANDLE_VALUE) {
        s.saved_handle = GetStdHandle(s.which);
        if (s.saved_handle != wanted[i]) {
          if (SetStdHandle(s.which, wanted[i])) s.handle_swapped = true;
        }
        if (!rebind_fd(s, wanted[i])) return false;
      }
      s.saved_mode = _setmode(s.fd, _O_BINARY);
    }
    // The console layer's cached handles predate this swap; without this
    // the first call's console handle serves every later call's safePrint.
    invalidateCachedHandles();
    clear_pipe_closed_flags();
    return true;
  }

  ~StdBorrow() {
    // Flush while the borrowed fds are still installed: buffered output
    // (tee/tr fwrite, std::cout) must land on the BORROWED handles, and
    // any prefetched stdin bytes must not leak into the next call.
    flush_stdio_for_borrow_switch();
    for (int i = 2; i >= 0; --i) {
      Slot &s = slots[i];
      // The Win32 slot goes back to the saved duplicate when the value it
      // held was the fd's own handle (killed by _close); otherwise to the
      // saved value, which _close never touched. The slot needs a restore
      // whenever it was swapped, or whenever its value died with the fd
      // (possible even when the swap itself failed).
      HANDLE restore_slot = s.saved_handle;
      if (s.fd_rebound && s.win32_slot_is_fd_handle && s.saved_os_dup) {
        restore_slot = s.saved_os_dup;
      }
      const bool slot_needs_restore =
          s.handle_swapped ||
          (s.fd_rebound && s.win32_slot_is_fd_handle);
      if (s.fd_rebound) {
        // _close also closes the duplicate the fd owned (wanted_dup, or
        // a restore-time duplicate of the original); that value is dead
        // now and must not be CloseHandle'd again.
        _close(s.fd);
        s.fd_rebound = false;
        s.wanted_dup = nullptr;
        if (s.saved_os_dup) {
          const int got =
              _open_osfhandle((intptr_t)s.saved_os_dup, s.open_flags);
          if (got == -1) {
            CloseHandle(s.saved_os_dup);
            s.saved_os_dup = nullptr;
            restore_slot = s.saved_handle;
          } else if (got == s.fd) {
            s.saved_os_dup = nullptr;  // now owned by the fd
          } else {
            _close(got);  // saved_os_dup is owned by that fd now
            s.saved_os_dup = nullptr;
            restore_slot = s.saved_handle;
          }
        }
      }
      if (s.saved_mode != -1) _setmode(s.fd, s.saved_mode);
      if (slot_needs_restore) SetStdHandle(s.which, restore_slot);
      // A duplicate left over from a failed rebind (fd closed, nothing
      // took ownership of it) is released here. A duplicate the fd now
      // owns was already nulled above; a dead value was nulled too.
      if (s.saved_os_dup) {
        CloseHandle(s.saved_os_dup);
        s.saved_os_dup = nullptr;
      }
    }
  }
};

// --- argv borrowing ----------------------------------------------------------
// One in-tree consumer (rm) reads the invocation arguments through the CRT
// globals __argc/__wargv. In the exe form those globals ARE the applet's own
// arguments, so the read is sound there. In this DLL they are not: the
// DLL's own CRT instance never ran an exe startup for the applet, so the
// globals hold whatever the host process's CRT produced — observed in the
// field as __argc >= 2 with __wargv == NULL, which made rm's scan read
// __wargv[1] at address 0x8 and take an access violation before doing any
// work. Borrow the ctx argv into the globals for the call instead:
// __wargv[0] is the applet name (consumers scan from index 1, exactly like
// the exe form) and ctx.argv follows verbatim. The narrow __argv is
// deliberately not synthesized: no consumer in the tree reads it, and the
// ctx carries wide strings only.
struct ArgvBorrow {
  std::vector<wchar_t *> slots;
  int saved_argc = 0;
  wchar_t **saved_wargv = nullptr;
  bool active = false;

  void apply(const winux_ffi_context *ctx) {
    saved_argc = __argc;
    saved_wargv = __wargv;
    slots.reserve(static_cast<size_t>(ctx->argc) + 2);
    slots.push_back(const_cast<wchar_t *>(ctx->applet));
    for (int32_t i = 0; i < ctx->argc; ++i) {
      // A NULL entry borrows as an empty string: consumers dereference
      // every slot while scanning.
      slots.push_back(ctx->argv[i] ? const_cast<wchar_t *>(ctx->argv[i])
                                   : const_cast<wchar_t *>(L""));
    }
    slots.push_back(nullptr);
    __argc = ctx->argc + 1;
    __wargv = slots.data();
    active = true;
  }

  ~ArgvBorrow() {
    if (!active) return;
    __argc = saved_argc;
    __wargv = saved_wargv;
  }
};

// Effective locale stamp from the live environment, mirroring the i18n
// selector precedence (WINUX_LANG, then LC_ALL, then LC_MESSAGES, then LANG).
std::wstring live_locale_stamp() {
  for (const wchar_t *name : {L"WINUX_LANG", L"LC_ALL", L"LC_MESSAGES", L"LANG"}) {
    const DWORD len = GetEnvironmentVariableW(name, nullptr, 0);
    if (len > 1) {
      std::wstring v(len, L'\0');
      const DWORD got = GetEnvironmentVariableW(name, v.data(), len);
      v.resize(got);
      return v;
    }
  }
  return {};
}

}  // namespace

extern "C" {

uint32_t winux_ffi_abi_version(void) { return WINUX_FFI_ABI_VERSION; }

int winux_ffi_init(const winux_ffi_init_args *args) {
  try {
    if (!args || args->struct_size < sizeof(winux_ffi_init_args)) {
      return WINUX_FFI_ERR_INVALID_ARG;
    }
    if (args->abi_version != WINUX_FFI_ABI_VERSION) {
      return WINUX_FFI_ERR_ABI_MISMATCH;
    }
    if (!is_absolute_wpath(args->install_root)) {
      return WINUX_FFI_ERR_INVALID_ARG;
    }
    std::lock_guard<std::mutex> lock(g_ffi_lock);
    if (g_initialized) {
      // Idempotent for the same root; a different root means the host is
      // mixing two WinuxCmd trees in one process — refuse rather than guess.
      return _wcsicmp(g_install_root.c_str(), args->install_root) == 0
                 ? 0
                 : WINUX_FFI_ERR_ABI_MISMATCH;
    }
    g_install_root = args->install_root;
    g_locale_stamp = args->locale_stamp ? args->locale_stamp : L"";
    g_initialized = true;
    return 0;
  } catch (...) {
    return WINUX_FFI_ERR_EXCEPTION;
  }
}

int winux_ffi_call(const winux_ffi_context *ctx) {
  if (!ctx || ctx->struct_size < sizeof(winux_ffi_context)) {
    return WINUX_FFI_ERR_INVALID_ARG;
  }
  HANDLE diag_handle = ctx->stderr_handle;
  try {
    if (ctx->abi_version != WINUX_FFI_ABI_VERSION) {
      return WINUX_FFI_ERR_ABI_MISMATCH;
    }
    if (!ctx->applet || !*ctx->applet || ctx->argc < 0 ||
        (ctx->argc > 0 && !ctx->argv) || !ctx->env_block ||
        !is_absolute_wpath(ctx->cwd)) {
      return WINUX_FFI_ERR_INVALID_ARG;
    }

    // One in-flight FFI call per process: the borrow below mutates
    // process-global cwd/env/std state (DESIGN §5 serialization lock).
    std::lock_guard<std::mutex> lock(g_ffi_lock);
    if (!g_initialized) return WINUX_FFI_ERR_NOT_INITIALIZED;

    if (ctx->cancel_check && ctx->cancel_check(ctx->cancel_user_data)) {
      return WINUX_FFI_ERR_CANCELLED;
    }

    const std::string applet_utf8 = wstring_to_utf8(ctx->applet);
    if (!CommandRegistry::hasCommand(applet_utf8)) {
      return WINUX_FFI_ERR_UNKNOWN_APPLET;
    }

    // Own the UTF-8 copies for the whole call; dispatch borrows views.
    std::vector<std::string> owned_args;
    owned_args.reserve(static_cast<size_t>(ctx->argc));
    for (int32_t i = 0; i < ctx->argc; ++i) {
      owned_args.emplace_back(ctx->argv[i] ? wstring_to_utf8(ctx->argv[i])
                                           : "");
    }
    std::vector<std::string_view> arg_views;
    arg_views.reserve(owned_args.size());
    for (const auto &a : owned_args) arg_views.emplace_back(a);

    CwdBorrow cwd_borrow;
    EnvBorrow env_borrow;
    StdBorrow std_borrow;
    ArgvBorrow argv_borrow;
    cwd_borrow.apply(ctx->cwd);
    env_borrow.apply(ctx->env_block);
    if (!std_borrow.apply(ctx)) return WINUX_FFI_ERR_GENERIC;
    argv_borrow.apply(ctx);

    // Locale stamp tracking (DESIGN §1.4): the spawn form re-reads the
    // catalog per process; the in-process form must at least notice a
    // stamp change. Full catalog reload inside utils:i18n is a follow-up;
    // the snapshot is refreshed here so the delta is observable.
    const std::wstring stamp = live_locale_stamp();
    if (stamp != g_locale_stamp) g_locale_stamp = stamp;

    // Mid-dispatch cancellation (#1487 defect B): the old code checked
    // the cancel callback once, before dispatch, and a 12s sort then ran
    // a further 9s after ^C. The watchdog above polls the callback while
    // dispatch runs; applets that notice the flag stop their work early
    // and return normally, and the call reports CANCELLED regardless of
    // the applet's own return code. (No exception is involved: the
    // dispatch chain is noexcept, so checkpoints bail out through their
    // normal return paths instead of throwing.)
    clear_ffi_cancel();
    CancelWatchdog watchdog;
    if (ctx->cancel_check) {
      watchdog.start(ctx->cancel_check, ctx->cancel_user_data);
    }
    const int rc = CommandRegistry::dispatch(applet_utf8, arg_views);
    watchdog.stop();
    if (ffi_cancel_requested()) {
      clear_ffi_cancel();
      return WINUX_FFI_ERR_CANCELLED;
    }
    return rc;
  } catch (const std::exception &e) {
    write_diag(diag_handle, "winux-ffi: exception at DLL boundary: ");
    write_diag(diag_handle, e.what());
    write_diag(diag_handle, "\n");
    return WINUX_FFI_ERR_EXCEPTION;
  } catch (...) {
    write_diag(diag_handle, "winux-ffi: unknown exception at DLL boundary\n");
    return WINUX_FFI_ERR_EXCEPTION;
  }
}

}  // extern "C"

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
  if (reason == DLL_PROCESS_ATTACH) {
    DisableThreadLibraryCalls(instance);
  }
  return TRUE;
}
