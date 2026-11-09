// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: In-process bridge from the C++ dispatcher to the rust2
///   command library (WinuxCmd 2.0 mechanical replacement, #1488).
///
/// A replaced command keeps its REGISTER_COMMAND registration (metadata for
/// help/man/completion, and the dispatcher's shared --help/--version
/// interception), but its handler body is a single call to
/// winux::rust2::run_command(). This unit is the only C++ that talks to the
/// Rust ABI (src/shared/rust2_abi.h): it turns the dispatcher's operands,
/// the process environment and the cwd into explicit UTF-8 C strings, runs
/// the command, and drains the returned stdout/stderr bytes through the
/// console layer (safePrint / safeErrorPrint) so redirected and captured
/// output keeps going through the same exit as every C++ applet.
///
/// Compiled only when CMake option WINUXCMD_ENABLE_RUST2 is ON (the source
/// is filtered out otherwise, together with the replaced commands).
module;

#define WIN32_LEAN_AND_MEAN
// Same pre-include order as utils:console (#1412): <cstddef> must be seen
// before pch.h's windows.h chain so c++config.h's std declarations get C++
// linkage.
#include <cstddef>
// _fileno and friends: keep <stdio.h> in its own block so clang-format does
// not sort it ahead of <cstddef>.
#include <stdio.h>
#undef NOMINMAX

#include "pch/pch.h"
#include "shared/rust2_abi.h"

export module rust2_bridge;

import std;
import utils;

namespace {

// Process environment as owned "name=value" UTF-8 strings. Entries whose
// name starts with '=' are the cmd.exe per-drive cwd pseudo-variables
// ("=C:=C:\\work"); they are not environment variables in the POSIX sense
// (GNU never sees them), so they are not forwarded.
auto snapshot_environment() -> std::vector<std::string> {
  std::vector<std::string> out;
  wchar_t* block = GetEnvironmentStringsW();
  if (block == nullptr) return out;
  for (const wchar_t* p = block; *p != L'\0';) {
    const std::wstring_view entry(p);
    if (entry.front() != L'=') {
      out.push_back(wstring_to_utf8(entry));
    }
    p += entry.size() + 1;
  }
  FreeEnvironmentStringsW(block);
  return out;
}

auto current_directory_utf8() -> std::string {
  const DWORD needed = GetCurrentDirectoryW(0, nullptr);
  if (needed == 0) return {};
  std::wstring buf(needed, L'\0');
  const DWORD written = GetCurrentDirectoryW(needed, buf.data());
  if (written == 0 || written >= needed) return {};
  buf.resize(written);
  return wstring_to_utf8(buf);
}

// Status the ABI uses for "the boundary could not run the command"
// (winux_core::ABI_FAILURE_STATUS).
constexpr int kAbiFailureStatus = 125;

}  // namespace

export namespace winux::rust2 {

/// Run rust2 library command `name` with the dispatcher's operands.
///
/// stdin is NOT forwarded: ABI v1 carries stdin as a whole buffer, which
/// would turn a streaming read into slurp-then-run. Only commands that never
/// read stdin may be wired through this entry (wave inventory io_idiom
/// `none` / output-only); stdin readers need a streaming ABI first (see
/// rust2/REPLACEMENT-RECIPE.md).
int run_command(const char* name, std::span<std::string_view> args) noexcept {
  if (winuxcmd_abi_version() != WINUXCMD_RUST2_ABI_VERSION) {
    safeErrorPrintLn(std::string(name) +
                     ": rust2 command library ABI version mismatch");
    return kAbiFailureStatus;
  }

  // argv: owned NUL-terminated copies (string_view is not terminated).
  std::vector<std::string> arg_storage(args.begin(), args.end());
  std::vector<const char*> argv;
  argv.reserve(arg_storage.size());
  for (const auto& a : arg_storage) argv.push_back(a.c_str());

  const std::vector<std::string> env_storage = snapshot_environment();
  std::vector<const char*> envp;
  envp.reserve(env_storage.size() + 1);
  for (const auto& kv : env_storage) envp.push_back(kv.c_str());
  envp.push_back(nullptr);

  const std::string cwd = current_directory_utf8();

  winuxcmd_cbuf out{nullptr, 0, 0};
  winuxcmd_cbuf err{nullptr, 0, 0};
  const int status = winuxcmd_run(
      name, static_cast<int>(argv.size()), argv.empty() ? nullptr : argv.data(),
      envp.data(), cwd.c_str(), nullptr, 0, &out, &err);

  // stdout before stderr: the Rust side buffered both, so their relative
  // interleaving is not observable anyway; GNU's order for the commands
  // wired so far (no interleaved writes) is preserved either way.
  if (out.ptr != nullptr && out.len > 0) {
    safePrint(
        std::string_view(reinterpret_cast<const char*>(out.ptr), out.len));
  }
  if (err.ptr != nullptr && err.len > 0) {
    safeErrorPrint(
        std::string_view(reinterpret_cast<const char*>(err.ptr), err.len));
  }
  winuxcmd_free_buf(&out);
  winuxcmd_free_buf(&err);
  return status;
}

}  // namespace winux::rust2
