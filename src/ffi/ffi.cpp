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
 *     installed as the process std handles and used directly;
 *   - env and cwd are explicit ctx fields, borrowed into process state
 *     under the FFI lock for the call and restored afterwards;
 *   - calls are serialized by one process-level mutex (the borrow above
 *     mutates process-global state; see DESIGN §5);
 *   - every entry point is wrapped so no C++ exception crosses the DLL
 *     boundary (the dispatch chain is noexcept; an escaping exception
 *     would otherwise std::terminate the host).
 */

#include "winux_ffi.h"

#include <windows.h>

#include <fcntl.h>
#include <io.h>

import std;
import core;
import utils;

namespace {

std::mutex g_ffi_lock;
bool g_initialized = false;
std::wstring g_install_root;
std::wstring g_locale_stamp;

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
// Installs the ctx handles as the process std handles (Win32 layer) and
// forces this DLL's CRT std fds to binary mode for the call — the mode the
// applet set expects (winuxcmd applets do binary stdio; see the exe entry
// flow). Previous handle and mode values are restored on destruction; the
// mode save/restore is the DLL side of the DESIGN §1.4 CRT contract (the
// host owns its own CRT's modes the same way).
struct StdBorrow {
  struct Slot {
    DWORD which;
    int fd;
    HANDLE saved_handle = nullptr;
    bool handle_swapped = false;
    int saved_mode = -1;
  };
  Slot slots[3] = {{STD_INPUT_HANDLE, 0, nullptr, false, -1},
                   {STD_OUTPUT_HANDLE, 1, nullptr, false, -1},
                   {STD_ERROR_HANDLE, 2, nullptr, false, -1}};

  void apply(const winux_ffi_context *ctx) {
    const HANDLE wanted[3] = {ctx->stdin_handle, ctx->stdout_handle,
                              ctx->stderr_handle};
    for (int i = 0; i < 3; ++i) {
      Slot &s = slots[i];
      if (wanted[i]) {
        s.saved_handle = GetStdHandle(s.which);
        if (s.saved_handle != wanted[i]) {
          if (SetStdHandle(s.which, wanted[i])) s.handle_swapped = true;
        }
      }
      s.saved_mode = _setmode(s.fd, _O_BINARY);
    }
  }

  ~StdBorrow() {
    for (int i = 2; i >= 0; --i) {
      Slot &s = slots[i];
      if (s.saved_mode != -1) _setmode(s.fd, s.saved_mode);
      if (s.handle_swapped) SetStdHandle(s.which, s.saved_handle);
    }
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
    cwd_borrow.apply(ctx->cwd);
    env_borrow.apply(ctx->env_block);
    std_borrow.apply(ctx);

    // Locale stamp tracking (DESIGN §1.4): the spawn form re-reads the
    // catalog per process; the in-process form must at least notice a
    // stamp change. Full catalog reload inside utils:i18n is a follow-up;
    // the snapshot is refreshed here so the delta is observable.
    const std::wstring stamp = live_locale_stamp();
    if (stamp != g_locale_stamp) g_locale_stamp = stamp;

    // No CRT flush around dispatch: the exe form does not flush either —
    // applets own their stream flushing, and stdio writes in this tree go
    // through the console layer on the borrowed handles. (std::fflush is
    // also unspellable here: _CRT_DISABLE_PERFCRIT_LOCKS in the PCH macro-
    // rewrites the token to _fflush_nolock, which the std module lacks.)
    const int rc = CommandRegistry::dispatch(applet_utf8, arg_views);
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
