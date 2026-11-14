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
/// ABI surface: v1 whole-buffer stdin (winuxcmd_run) and, since the
/// #1556 adoption, the additive v2 streaming stdin (winuxcmd_run_stream,
/// host pull callback) — run_command_with_stdin_streaming reads fd 0 in
/// chunks through the same file_io read path as the v1 slurp. First
/// wired commands (#1488 stream adoption): the digest family b2sum /
/// sha224sum / sha384sum and tac; the stream-ABI driver remains the
/// bridge-level regression net for the entry itself.
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

// Shared ABI-call setup: owned copies of everything the explicit-handle
// ABI needs (#1488 "ABI 面只传显式句柄/缓冲，不传环境状态") — argv as
// NUL-terminated UTF-8 copies (string_view is not terminated), the env
// snapshot as a NULL-terminated `name=value` array, and the cwd. Lives for
// the duration of one bridge call; both ABI entries share it.
struct HostCall {
  std::vector<std::string> arg_storage;
  std::vector<const char*> argv;
  std::vector<std::string> env_storage;
  std::vector<const char*> envp;
  std::string cwd;

  explicit HostCall(std::span<std::string_view> args) {
    arg_storage.assign(args.begin(), args.end());
    argv.reserve(arg_storage.size());
    for (const auto& a : arg_storage) argv.push_back(a.c_str());

    env_storage = snapshot_environment();
    envp.reserve(env_storage.size() + 1);
    for (const auto& kv : env_storage) envp.push_back(kv.c_str());
    envp.push_back(nullptr);

    cwd = current_directory_utf8();
  }
};

// The bridge refuses to run on an ABI surface mismatch (the header and
// lib.rs bump together; see WINUXCMD_RUST2_ABI_VERSION).
auto abi_version_ok(const char* name) -> bool {
  if (winuxcmd_abi_version() != WINUXCMD_RUST2_ABI_VERSION) {
    safeErrorPrintLn(std::string(name) +
                     ": rust2 command library ABI version mismatch");
    return false;
  }
  return true;
}

// Write-error status for the closed-fd shape, per command. GNU grep
// treats every write failure as EXIT_TROUBLE (2): the 1.x body surfaced
// it as report_stdout_write_error("grep", 2) (#1368), and grep.c's
// prwrite-failure path dies with EXIT_TROUBLE — so the grep family keeps
// 2 where the generic drain used 1. Both ABIs buffer stdout, so the
// failure can only be observed here, at the host drain, per command name.
auto write_error_status(const char* name) -> int {
  const std::string_view view(name);
  if (view == "grep" || view == "egrep" || view == "fgrep") return 2;
  return 1;
}

// Shared drain for both ABI entries: the closed-stdout contract, the raw
// writers, and the buffer release. `status` is whatever the ABI entry
// returned (v2 may hand back the 124 host-stopped-feeding status here).
auto drain_result(const char* name, int status, winuxcmd_cbuf& out,
                  winuxcmd_cbuf& err) noexcept -> int {
  // [GNU] close_stdout contract for the closed-fd shape (issue #1368,
  // probed coreutils 9.4): a command that PRODUCED stdout bytes against a
  // closed fd 1 reports "<name>: write error: Bad file descriptor" and
  // fails (rc 1; the grep family keeps GNU's EXIT_TROUBLE 2); a command
  // that wrote nothing stays silent with its own status (`true >&-`
  // rc 0). Both ABIs buffer stdout, so the write failure surfaces here
  // at the drain instead of inside the library — the same place GNU's
  // global close_stdout hook reports it. Broken-pipe write failures stay
  // with a future streaming write callback (README "Output side").
  if (out.ptr != nullptr && out.len > 0 && _fileno(stdout) < 0) {
    safeErrorPrintLn(std::string(name) + ": write error: Bad file descriptor");
    winuxcmd_free_buf(&out);
    winuxcmd_free_buf(&err);
    return write_error_status(name);
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

// Shared v1 body: turn the dispatcher's operands, the environment snapshot
// and the cwd into the explicit v1 buffers and drain the result. `stdin_buf`
// is the v1 whole-buffer stdin (NULL/0 = empty stdin); the host slurped it
// before the call and owns the bytes for the duration of the run.
auto run_with_stdin_bytes(const char* name, std::span<std::string_view> args,
                          const unsigned char* stdin_buf,
                          size_t stdin_len) noexcept -> int {
  if (!abi_version_ok(name)) return kAbiFailureStatus;

  HostCall call(args);
  winuxcmd_cbuf out{nullptr, 0, 0};
  winuxcmd_cbuf err{nullptr, 0, 0};
  const int status = winuxcmd_run(name, static_cast<int>(call.argv.size()),
                                  call.argv.empty() ? nullptr : call.argv.data(),
                                  call.envp.data(), call.cwd.c_str(), stdin_buf,
                                  stdin_len, &out, &err);
  return drain_result(name, status, out, err);
}

// ---- ABI v2 streaming stdin (#1556 stream-ABI adoption, #1488) ----

// Per-run host state for the real-stdin pull callback. The callback fires
// synchronously on the caller's thread in the order the command reads
// (README "Threading model": single-threaded, no locks), so plain members
// are enough.
struct StdinFeedState {
  HANDLE handle = nullptr;
  bool prepared = false;
};

// The host pull callback (winuxcmd_stream_read_fn): fill up to `cap` bytes
// from the process's real stdin (console face / pipe / redirected file).
//
// Read-path parity with the v1 slurp (file_io::read_all_stdin): binary mode
// once before the first read (file_io::set_stdin_binary — the text-mode CRT
// translator would otherwise stop at a 0x1A byte and collapse CRLF), then
// file_io::read_stdin_record for every fill, which carries the
// winux#1450 cooked-console VEOF contract: ^D at line start ends the
// stream (0), a mid-line ^D flushes the pending prefix as its own partial
// fill (the segmentation boundary), and the unread tail of the console
// record stays queued for the next reader. ^Z (0x1A) is NOT an EOF on this
// path: in binary mode it is ordinary data on every face — console
// records, pipes, redirected files — exactly as in the v1 slurp; the
// DOS text-mode ^Z EOF belongs to the CRT translator this path bypasses.
//
// ReadFile failure mapping, same as read_all_stdin: the broken-pipe EOF
// family (ERROR_BROKEN_PIPE / ERROR_NO_DATA / ERROR_HANDLE_EOF) is
// end-of-stream (0); anything else is -2, the ABI v2 host read failure —
// the channel v1 lacked, so the command now reports the #973-family
// closed/bad-stdin diagnostics in its own GNU wording.
auto stdin_feed_read(void* user_data, unsigned char* buf, size_t cap)
    -> long long {
  if (cap == 0) return 0;  // defensive; the boundary never asks for 0
  auto* state = static_cast<StdinFeedState*>(user_data);
  if (!state->prepared) {
    state->prepared = true;
    file_io::set_stdin_binary();
    state->handle = GetStdHandle(STD_INPUT_HANDLE);
  }
  DWORD got = 0;
  const DWORD want = static_cast<DWORD>(
      std::min<size_t>(cap, std::numeric_limits<DWORD>::max()));
  if (!file_io::read_stdin_record(state->handle,
                                  reinterpret_cast<char*>(buf), want, &got)) {
    const DWORD err = GetLastError();
    if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA ||
        err == ERROR_HANDLE_EOF) {
      return 0;
    }
    return -2;
  }
  return static_cast<long long>(got);
}

// Shared v2 body: same host-side setup and drain as v1, stdin through the
// host pull callback. `stdin_read` may be NULL (empty stdin, same as
// `stdin_buf == NULL` in v1). Status contract per the header: the command's
// own status; 124 when the host reported -1 (timeout convention) even if
// the command swallowed the read error; the command's own read-error rc on
// -2 (e.g. tac exits 1 with its GNU diagnostic — #973 family, now flowing
// through the command instead of a host-side pre-check).
auto run_with_stdin_source(const char* name, std::span<std::string_view> args,
                           winuxcmd_stream_read_fn* stdin_read,
                           void* stdin_user_data) noexcept -> int {
  if (!abi_version_ok(name)) return kAbiFailureStatus;

  HostCall call(args);
  winuxcmd_cbuf out{nullptr, 0, 0};
  winuxcmd_cbuf err{nullptr, 0, 0};
  const int status =
      winuxcmd_run_stream(name, static_cast<int>(call.argv.size()),
                          call.argv.empty() ? nullptr : call.argv.data(),
                          call.envp.data(), call.cwd.c_str(), stdin_read,
                          stdin_user_data, &out, &err);
  return drain_result(name, status, out, err);
}

}  // namespace

export namespace winux::rust2 {

/// Routing query for the grep wire (#1488 grep lane): 1 when this
/// grep/egrep/fgrep argument vector selects the Perl matcher, judged by
/// the rust2 library's getopt-shaped [`uses_perl`] grammar over the
/// dispatcher's operands (winuxcmd_grep_uses_perl in the FFI). The grep
/// handler splits on this at the dispatch layer: a Perl invocation keeps
/// the C++ PCRE2 face (DOCS/2.0-regex-engine-decision.md §6 owner point
/// 2 — the same command, two engines, the split point is HERE, before
/// either body runs); every other shape is the mechanical rust2
/// replacement. POSIXLY_CORRECT is read from the process environment —
/// the same source the run entries snapshot into the injected env — so
/// both engines apply the same stop-at-first-operand rule.
int grep_uses_perl(std::span<std::string_view> args) noexcept {
  std::vector<std::string> storage;
  storage.assign(args.begin(), args.end());
  std::vector<const char*> argv;
  argv.reserve(storage.size());
  for (const auto& a : storage) argv.push_back(a.c_str());
  const int posixly =
      std::getenv("POSIXLY_CORRECT") != nullptr ? 1 : 0;
  return winuxcmd_grep_uses_perl(
      static_cast<int>(argv.size()),
      argv.empty() ? nullptr : argv.data(), posixly);
}

/// Run rust2 library command `name` with the dispatcher's operands.
///
/// stdin is forwarded ONLY for the declared stdin readers (see
/// kRust2StdinCommands), as the v1 whole buffer: slurp-then-run. All other
/// commands keep the pass-nothing contract, which keeps fd 0 live for
/// pipeline consumers and spawned children.
int run_command(const char* name, std::span<std::string_view> args) noexcept {
  // Whole-buffer forwarding for the declared stdin readers only. A slurp
  // error (closed fd 0) hands the command empty stdin — the v1 buffer ABI
  // cannot carry the error, so the EOF shape is the closest the boundary
  // can express (GNU's own "Bad file descriptor" wording stays with the
  // streaming v2 ABI).
  std::string stdin_storage;
  const unsigned char* stdin_ptr = nullptr;
  size_t stdin_len = 0;
  if (rust2_reads_stdin(name)) {
    if (auto content = file_io::read_all_stdin()) {
      stdin_storage = std::move(*content);
    }
    stdin_ptr = reinterpret_cast<const unsigned char*>(stdin_storage.data());
    stdin_len = stdin_storage.size();
  }
  return run_with_stdin_bytes(name, args, stdin_ptr, stdin_len);
}

/// Run rust2 library command `name` with the dispatcher's operands and ABI
/// v1 whole-buffer stdin: fd 0 is slurped up front (binary-safe, the same
/// read_all_stdin path the 1.x applets used — console VEOF, broken-pipe EOF,
/// NUL bytes) and handed to the command as one byte buffer.
///
/// The CALLER decides when stdin is an input: the registered handler applies
/// the command's own GNU operand rule (basenc/digest/tac read fd 0 when no
/// FILE operand is present or an operand is "-") and picks this entry, so
/// `base64 FILE` at an interactive console does not block on a slurp GNU
/// would never do — and, unlike the kRust2StdinCommands allowlist above, a
/// non-stdin shape keeps fd 0 live for pipeline consumers and spawned
/// children. A slurp failure (bad fd) reports `name: <reason>` like the 1.x
/// bodies did and exits 1 — the command itself never runs.
///
/// This is deliberately NOT the streaming ABI (v2 `winuxcmd_run_stream`):
/// whole-buffer is the shape this wave wires (recipe §5); a pipe stdin is
/// drained to EOF even for shapes where GNU would leave it unread — the
/// documented v1 price, revisited by the streaming wave.
int run_command_with_stdin(const char* name,
                           std::span<std::string_view> args) noexcept {
  if (!abi_version_ok(name)) return kAbiFailureStatus;

  auto content = file_io::read_all_stdin();
  if (!content) {
    safeErrorPrintLn(std::string(name) + ": " + content.error());
    return 1;
  }
  const std::string& bytes = *content;
  return run_with_stdin_bytes(
      name, args,
      bytes.empty() ? nullptr
                    : reinterpret_cast<const unsigned char*>(bytes.data()),
      bytes.size());
}

/// Run rust2 library command `name` with an explicit stdin BYTE BUFFER
/// through ABI v1: the generic form of run_command_with_stdin for hosts
/// that already hold the bytes (tests, the stream-ABI driver, the next
/// wiring wave's A/B comparisons). No fd 0 is touched.
int run_command_with_stdin_bytes(
    const char* name, std::span<std::string_view> args,
    std::span<const unsigned char> stdin_bytes) noexcept {
  return run_with_stdin_bytes(name, args, stdin_bytes.data(),
                              stdin_bytes.size());
}

/// Run rust2 library command `name` with stdin arriving through a host
/// pull callback (ABI v2, #1556): the generic streaming form. `stdin_read`
/// may be NULL (empty stdin). The callback contract, the 124
/// host-stops-feeding status and the -2 read-failure channel are specified
/// in src/shared/rust2_abi.h and rust2/README.md "Streaming stdin ABI".
/// Output stays v1-shaped: returned buffers drained by drain_result.
int run_command_with_stdin_source(
    const char* name, std::span<std::string_view> args,
    winuxcmd_stream_read_fn* stdin_read, void* stdin_user_data) noexcept {
  return run_with_stdin_source(name, args, stdin_read, stdin_user_data);
}

/// Run rust2 library command `name` with the dispatcher's operands and
/// ABI v2 STREAMING stdin: fd 0 is read in chunks through a host pull
/// callback (the same file_io read path as the v1 slurp — binary mode,
/// cooked-console VEOF ^D records and flush boundaries, broken-pipe EOF —
/// but handed to the command segment by segment, so no whole-buffer input
/// residency: a 64 MiB input no longer costs the slurp plus the ABI's
/// `stdin_buf -> Vec` copy).
///
/// WIRED COMMANDS (#1488 stream adoption): the digest family — b2sum,
/// sha224sum, sha384sum — and tac route their stdin shapes through this
/// entry; their v1 bodies had a host-side slurp pre-check whose wording
/// GNU does not use (sha224sum/sha384sum/tac are silent rc 1 on a closed
/// stdin, probed coreutils 9.4), so the streaming entry is also the
/// GNU-exact error shape for them. The remaining v1 behaviors of
/// run_command / run_command_with_stdin and the kRust2StdinCommands
/// slurp are deliberately left untouched; later wiring waves pick this
/// entry for commands whose GNU shape is genuinely streaming (see
/// REPLACEMENT-RECIPE.md §5). The stream-ABI driver
/// (rust2-stream-driver) and its ctest lanes stay the bridge-level
/// regression net for the entry.
///
/// Unlike the v1 whole-buffer entry, a closed or bad fd 0 is NOT
/// pre-screened here: the callback reports -2 and the command emits its own
/// GNU read-error diagnostic (#973 family) — the channel v1 could not
/// express.
int run_command_with_stdin_streaming(
    const char* name, std::span<std::string_view> args) noexcept {
  StdinFeedState state;
  return run_with_stdin_source(name, args, &stdin_feed_read, &state);
}

}  // namespace winux::rust2
