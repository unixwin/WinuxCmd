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

// rust2 commands whose library implementation reads fd 0 (winux_core::Io's
// stdin) in EVERY invocation shape. For exactly these names run_command
// forwards stdin through the v1 whole-buffer ABI (winuxcmd_run's
// stdin_buf), slurped with file_io::read_all_stdin. Every other rust2
// command keeps the pass-nothing contract — their fd 0 stays untouched, so
// later consumers in the same pipeline stage keep unread bytes and spawned
// children (timeout's wrapped command) inherit the live handle. The set is
// declared here, not probed, because the v1 entry cannot ask the library;
// the streaming v2 ABI (winuxcmd_run_stream) is the long-term replacement
// (rust2/REPLACEMENT-RECIPE.md §5). Wave3b (#1488): numfmt reads stdin
// only in its no-operand form; the slurp happens before that is known,
// which is the accepted whole-buffer cost.
constexpr std::array<std::string_view, 5> kRust2StdinCommands{
    "expand", "join", "numfmt", "tsort", "unexpand"};

auto rust2_reads_stdin(const char* name) -> bool {
  const std::string_view view(name);
  return std::find(kRust2StdinCommands.begin(), kRust2StdinCommands.end(),
                   view) != kRust2StdinCommands.end();
}

// Standard input as one ABI v1 whole buffer, slurped through the ONE
// Windows stdin read path (file_io::read_all_stdin: console VEOF (^Z/^D)
// observation, binary mode, broken-pipe EOF). A closed or otherwise
// unreadable stdin yields an empty buffer: ABI v1 has no stdin error
// channel, so the GNU read-error diagnostics for a closed stdin (#973
// family) stay a streaming-v2 item.
auto slurp_stdin_binary() -> std::string {
  if (auto content = file_io::read_all_stdin()) {
    return std::move(*content);
  }
  return {};
}

// Shared body: turn the dispatcher's operands + process state into the ABI
// call and drain the returned buffers through the RAW console writers.
auto run_common(const char* name, std::span<std::string_view> args,
                const unsigned char* stdin_buf, size_t stdin_len) noexcept
    -> int {
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
  const int status = winuxcmd_run(name, static_cast<int>(argv.size()),
                                  argv.empty() ? nullptr : argv.data(),
                                  envp.data(), cwd.c_str(), stdin_buf,
                                  stdin_len, &out, &err);

  // [GNU] close_stdout contract for the closed-fd shape (issue #1368,
  // probed coreutils 9.4): a command that PRODUCED stdout bytes against a
  // closed fd 1 reports "<name>: write error: Bad file descriptor" and
  // fails (rc 1); a command that wrote nothing stays silent with its own
  // status (`true >&-` rc 0). The v1 ABI buffers stdout, so the write
  // failure surfaces here at the drain instead of inside the library —
  // the same place GNU's global close_stdout hook reports it. Broken-pipe
  // write failures stay with the streaming v2 ABI (recipe §5).
  if (out.ptr != nullptr && out.len > 0 && _fileno(stdout) < 0) {
    safeErrorPrintLn(std::string(name) + ": write error: Bad file descriptor");
    winuxcmd_free_buf(&out);
    winuxcmd_free_buf(&err);
    return 1;
  }

  // stdout before stderr: the Rust side buffered both, so their relative
  // interleaving is not observable anyway; GNU's order for the commands
  // wired so far (no interleaved writes) is preserved either way.
  //
  // Both streams drain through the RAW writers (safePrintRaw /
  // safeErrorPrintRaw): Rust output is already final GNU-compatible text,
  // and the legacy catalog lookup in the translating writers would key on
  // the whole payload — command data (a basename result, an env value)
  // must reach the handle byte-for-byte (#1488; see console.cppm).
  if (out.ptr != nullptr && out.len > 0) {
    safePrintRaw(
        std::string_view(reinterpret_cast<const char*>(out.ptr), out.len));
  }
  if (err.ptr != nullptr && err.len > 0) {
    safeErrorPrintRaw(
        std::string_view(reinterpret_cast<const char*>(err.ptr), err.len));
  }
  winuxcmd_free_buf(&out);
  winuxcmd_free_buf(&err);
  return status;
}

}  // namespace

export namespace winux::rust2 {

/// Run rust2 library command `name` with the dispatcher's operands.
///
/// stdin is forwarded ONLY for the declared stdin readers (see
/// kRust2StdinCommands), as the v1 whole buffer: slurp-then-run. All other
/// commands keep the pass-nothing contract, which keeps fd 0 live for
/// pipeline consumers and spawned children.
int run_command(const char* name, std::span<std::string_view> args) noexcept {
  if (rust2_reads_stdin(name)) {
    std::string stdin_storage = slurp_stdin_binary();
    return run_common(
        name, args,
        stdin_storage.empty()
            ? nullptr
            : reinterpret_cast<const unsigned char*>(stdin_storage.data()),
        stdin_storage.size());
  }
  return run_common(name, args, nullptr, 0);
}

/// Run rust2 library command `name`, forwarding standard input as the ABI v1
/// whole buffer (#1488 wave3a stdin class: tee / paste / fold / fmt / ln -i).
///
/// The slurp happens BEFORE the command runs, so only invocations whose GNU
/// shape actually consumes stdin may call this entry — that condition is the
/// wiring site's knowledge and is stated in each command's comment:
/// tee always; paste/fold/fmt when there is no file operand or a "-"
/// operand; ln when -i/--interactive may prompt. A per-invocation entry is
/// needed because these readers are CONDITIONAL (paste f1 f2 must leave fd 0
/// untouched, the way run_command's pass-nothing contract does), which the
/// declared-reader name list behind run_command cannot express.
///
/// Whole-buffer means GNU's incremental reads become slurp-then-run: for a
/// pipe producer the same bytes are consumed, but output happens only at
/// EOF, and an interactive `ln -i` sees its first prompt only after the
/// console reaches EOF (Ctrl+Z). That is the accepted v1 tradeoff, the same
/// one run_command's declared readers carry; streaming stdin
/// (winuxcmd_run_stream, ABI v2) is the later wave (see
/// rust2/REPLACEMENT-RECIPE.md §5).
int run_command_with_stdin(const char* name,
                           std::span<std::string_view> args) noexcept {
  std::string stdin_data = slurp_stdin_binary();
  return run_common(name, args,
                    stdin_data.empty()
                        ? nullptr
                        : reinterpret_cast<const unsigned char*>(
                            stdin_data.data()),
                    stdin_data.size());
}

}  // namespace winux::rust2
