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
/// the process environment and the cwd into explicit UTF-8 C strings and
/// runs the command with host stdio callbacks, so stdout/stderr bytes go
/// through the console layer (safePrintRaw / safeErrorPrintRaw) WHILE the
/// command runs — the same exit as every C++ applet, now with bounded
/// memory, live output, broken-pipe stop and stdout/stderr interleaving.
///
/// ABI surface: every entry point runs through ONE body over ABI v3
/// (winuxcmd_run_io, #1488 streaming output): a stdin pull callback (real
/// fd 0 through the file_io read path, a host memory buffer, a caller
/// callback, or none) plus stdout/stderr write callbacks. The v1
/// (winuxcmd_run) and v2 (winuxcmd_run_stream) symbols stay exported by
/// the library but have no caller here any more. Status overrides the
/// boundary cannot know (README "Streaming output ABI (v3)"): a broken
/// stdout pipe ends the run as 141 (the product's SIGPIPE-death
/// emulation), any other stdout write failure keeps the #1368
/// closed-stdout contract ("<name>: write error: <reason>", rc 1).
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

// [GNU] a write to a pipe whose reader is gone kills the writer with
// SIGPIPE; the shell observes 128 + SIGPIPE = 141. Windows has no SIGPIPE,
// so the product emulates the death as exit status 141 (#1142, the same
// constant as src/commands/yes.cpp kSigpipeDeathStatus).
constexpr int kSigpipeDeathStatus = 128 + 13;

// rust2 commands whose library implementation reads fd 0 (winux_core::Io's
// stdin) in EVERY invocation shape. For exactly these names run_command
// forwards stdin, slurped with file_io::read_all_stdin and served from
// host memory through the v3 stdin callback. Every other rust2 command
// keeps the pass-nothing contract — their fd 0 stays untouched, so later
// consumers in the same pipeline stage keep unread bytes and spawned
// children (timeout's wrapped command) inherit the live handle. The set
// is declared here, not probed, because the boundary cannot ask the
// library; streaming stdin (run_command_with_stdin_streaming) is the
// long-term replacement (rust2/REPLACEMENT-RECIPE.md §5). Wave3b (#1488):
// numfmt reads stdin only in its no-operand form; the slurp happens
// before that is known, which is the accepted whole-buffer cost.
constexpr std::array<std::string_view, 5> kRust2StdinCommands{
    "expand", "join", "numfmt", "tsort", "unexpand"};

auto rust2_reads_stdin(const char* name) -> bool {
  const std::string_view view(name);
  return std::find(kRust2StdinCommands.begin(), kRust2StdinCommands.end(),
                   view) != kRust2StdinCommands.end();
}

// Shared ABI-call setup: owned copies of everything the explicit-handle
// ABI needs (#1488 "ABI 面只传显式句柄/缓冲，不传环境状态") — argv as
// NUL-terminated UTF-8 copies (string_view is not terminated), the env
// snapshot as a NULL-terminated `name=value` array, and the cwd. Lives for
// the duration of one bridge call.
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

// ---- stdin sources (ABI v2/v3 pull callbacks) ----

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
// end-of-stream (0); anything else is -2, the ABI host read failure —
// the channel v1 lacked, so the command reports the #973-family
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
  if (!file_io::read_stdin_record(state->handle, reinterpret_cast<char*>(buf),
                                  want, &got)) {
    const DWORD err = GetLastError();
    if (err == ERROR_BROKEN_PIPE || err == ERROR_NO_DATA ||
        err == ERROR_HANDLE_EOF) {
      return 0;
    }
    return -2;
  }
  return static_cast<long long>(got);
}

// Host-memory stdin for the byte-buffer entries (the v1 whole-buffer
// shapes): serves a caller-owned byte range through the pull callback.
// The bytes stay host-owned for the whole run; the boundary copies them
// fill by fill, so no ABI-side whole-buffer copy exists any more.
struct MemoryFeedState {
  const unsigned char* data = nullptr;
  size_t len = 0;
  size_t pos = 0;
};

auto memory_feed_read(void* user_data, unsigned char* buf, size_t cap)
    -> long long {
  auto* state = static_cast<MemoryFeedState*>(user_data);
  const size_t n = std::min(cap, state->len - state->pos);
  if (n == 0) return 0;
  std::memcpy(buf, state->data + state->pos, n);
  state->pos += n;
  return static_cast<long long>(n);
}

// ---- stdout/stderr sinks (ABI v3 write callbacks) ----

// Length of the longest prefix of `bytes` that does not end inside an
// incomplete UTF-8 sequence (a lead byte still waiting for continuation
// bytes). Only the console face needs it: WriteConsoleW takes UTF-16, and
// a chunk cut mid-character would be converted as two replacement
// characters. Invalid sequences pass through unchanged — the converter
// maps them exactly as the whole-buffer drain did.
auto utf8_complete_prefix(std::string_view bytes) -> size_t {
  const size_t n = bytes.size();
  size_t i = n;
  size_t trailing = 0;
  while (i > 0 && trailing < 3 &&
         (static_cast<unsigned char>(bytes[i - 1]) & 0xC0) == 0x80) {
    --i;
    ++trailing;
  }
  if (i == 0) return n;
  const auto lead = static_cast<unsigned char>(bytes[i - 1]);
  size_t need = 0;
  if (lead >= 0xC2 && lead <= 0xDF) {
    need = 2;
  } else if (lead >= 0xE0 && lead <= 0xEF) {
    need = 3;
  } else if (lead >= 0xF0 && lead <= 0xF4) {
    need = 4;
  } else {
    return n;  // ASCII or not a lead byte: nothing pending
  }
  return (trailing + 1 < need) ? i - 1 : n;
}

// One console-layer stream as seen by the v3 callbacks. On the console
// face a chunk's trailing incomplete UTF-8 sequence is carried into the
// next chunk (and written as-is at the end of the run); pipes and files
// get the bytes unchanged.
struct SinkFace {
  bool console = false;
  std::string carry;
};

template <typename Writer>
auto sink_write(SinkFace& face, std::string_view chunk, Writer&& write)
    -> void {
  if (!face.console) {
    write(chunk);
    return;
  }
  std::string_view data = chunk;
  std::string joined;
  if (!face.carry.empty()) {
    joined = std::move(face.carry);
    face.carry.clear();
    joined.append(chunk);
    data = joined;
  }
  const size_t complete = utf8_complete_prefix(data);
  if (complete < data.size()) face.carry.assign(data.substr(complete));
  if (complete > 0) write(data.substr(0, complete));
}

// Per-run state of the stdout/stderr write callbacks (both receive the
// same user_data). Single-threaded, like the stdin callback.
struct OutputState {
  SinkFace out;
  SinkFace err;
  bool stdout_broken_pipe = false;  // a stdout write returned -1
  bool stdout_failed = false;       // a stdout write returned -2
  bool stdout_fd_closed = false;    // ... because fd 1 is closed (`>&-`)
  // stderr callbacks seen after the first stdout -2: the command reported
  // the write failure itself (e.g. tac/tee), so the host must not add a
  // second diagnostic.
  unsigned long long stderr_writes_after_failure = 0;
};

// stdout write callback (winuxcmd_stream_write_fn). The console layer
// records the Win32 cause of a failed write (console.cppm
// note_stdout_write_failure): a broken pipe raises is_stdout_pipe_closed()
// -> -1, anything else is_stdout_write_failed() -> -2. A closed CRT fd 1
// (`>&-`) is -2 without a write attempt — the exact trigger of the #1368
// contract the whole-buffer drain applied before.
auto stdout_write_cb(void* user_data, const unsigned char* buf, size_t len)
    -> int {
  auto* st = static_cast<OutputState*>(user_data);
  if (_fileno(stdout) < 0) {
    st->stdout_failed = true;
    st->stdout_fd_closed = true;
    return -2;
  }
  // RAW writer: Rust output is already final GNU-compatible text, and the
  // legacy catalog lookup in the translating writers would key on the
  // payload — command data must reach the handle byte-for-byte (#1488).
  sink_write(st->out, std::string_view(reinterpret_cast<const char*>(buf), len),
             [](std::string_view sv) { safePrintRaw(sv); });
  if (is_stdout_pipe_closed()) {
    st->stdout_broken_pipe = true;
    return -1;
  }
  if (is_stdout_write_failed()) {
    st->stdout_failed = true;
    return -2;
  }
  return 0;
}

// stderr write callback: unbuffered on the Rust side, so every command
// write lands here in program order. -1 only when the stderr pipe is gone;
// a stderr failure never changes the run's status.
auto stderr_write_cb(void* user_data, const unsigned char* buf, size_t len)
    -> int {
  auto* st = static_cast<OutputState*>(user_data);
  if (st->stdout_failed) ++st->stderr_writes_after_failure;
  sink_write(st->err, std::string_view(reinterpret_cast<const char*>(buf), len),
             [](std::string_view sv) { safeErrorPrintRaw(sv); });
  return is_stderr_pipe_closed() ? -1 : 0;
}

// The ONE body every exported entry runs through (ABI v3). `stdin_read`
// may be NULL (empty stdin). Status mapping on top of the ABI's own
// (command status / 124 host-stopped-feeding / 125 boundary failure):
//
// * a stdout write saw a broken pipe -> 141, silently, whatever the
//   command returned: GNU dies of SIGPIPE at that write (#1142). The
//   sticky Rust-side failure already stopped the producer.
// * another stdout write failure -> the [GNU] close_stdout contract of
//   issue #1368 (probed coreutils 9.4): a command that PRODUCED stdout
//   bytes against a closed/failing stdout reports "<name>: write error:
//   <strerror>" once and fails (rc 1); a command that wrote nothing never
//   reaches a callback and keeps its own status (`true >&-` rc 0). When
//   the command already printed its own diagnostic after the failure
//   (tac's/tee's write-error paths), the host adds none and keeps the
//   command's failing status.
auto run_io(const char* name, std::span<std::string_view> args,
            winuxcmd_stream_read_fn* stdin_read, void* stdin_user_data) noexcept
    -> int {
  if (!abi_version_ok(name)) return kAbiFailureStatus;

  HostCall call(args);
  // Per-run console-layer write state: the classification below must see
  // only this run's failures.
  clear_pipe_closed_flags();
  OutputState st;
  st.out.console = isOutputConsole();
  st.err.console = isErrorConsole();
  const winuxcmd_io_v3 io{
      stdin_read,
      stdin_user_data,
      &stdout_write_cb,
      &st,
      &stderr_write_cb,
      &st,
      st.out.console ? WINUXCMD_IO_STDOUT_LINE_BUFFERED : 0u};
  const int status =
      winuxcmd_run_io(name, static_cast<int>(call.argv.size()),
                      call.argv.empty() ? nullptr : call.argv.data(),
                      call.envp.data(), call.cwd.c_str(), &io);

  // A console chunk may have ended mid-character; hand over what is left.
  if (!st.out.carry.empty() && !st.stdout_broken_pipe && !st.stdout_failed) {
    safePrintRaw(st.out.carry);
  }
  if (!st.err.carry.empty()) safeErrorPrintRaw(st.err.carry);

  if (st.stdout_broken_pipe) return kSigpipeDeathStatus;
  if (st.stdout_failed) {
    if (st.stderr_writes_after_failure != 0) {
      return status != 0 ? status : 1;
    }
    const std::string reason = st.stdout_fd_closed
                                   ? std::string("Bad file descriptor")
                                   : std::string(stdout_write_error_text());
    safeErrorPrintLn(std::string(name) + ": write error: " + reason);
    return 1;
  }
  return status;
}

// Byte-buffer stdin (the whole-buffer shapes): NULL/0 = empty stdin.
auto run_with_stdin_bytes(const char* name, std::span<std::string_view> args,
                          const unsigned char* stdin_buf,
                          size_t stdin_len) noexcept -> int {
  if (stdin_buf == nullptr || stdin_len == 0) {
    return run_io(name, args, nullptr, nullptr);
  }
  MemoryFeedState feed{stdin_buf, stdin_len, 0};
  return run_io(name, args, &memory_feed_read, &feed);
}

}  // namespace

export namespace winux::rust2 {

/// Run rust2 library command `name` with the dispatcher's operands.
///
/// stdin is forwarded ONLY for the declared stdin readers (see
/// kRust2StdinCommands), as a whole buffer: slurp-then-run. All other
/// commands keep the pass-nothing contract, which keeps fd 0 live for
/// pipeline consumers and spawned children. Output streams through the v3
/// write callbacks (see run_io).
int run_command(const char* name, std::span<std::string_view> args) noexcept {
  // Whole-buffer forwarding for the declared stdin readers only. A slurp
  // error (closed fd 0) hands the command empty stdin — the whole-buffer
  // shape cannot carry the error, so the EOF shape is the closest the
  // boundary can express (GNU's own "Bad file descriptor" wording belongs
  // to the streaming stdin entry).
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

/// Run rust2 library command `name` with the dispatcher's operands and
/// whole-buffer stdin: fd 0 is slurped up front (binary-safe, the same
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
/// This is deliberately NOT streaming stdin: whole-buffer is the shape
/// this wave wires (recipe §5); a pipe stdin is drained to EOF even for
/// shapes where GNU would leave it unread — the documented whole-buffer
/// price, revisited by the streaming wave. The slurped bytes reach the
/// command through the v3 memory stdin callback (no ABI-side copy);
/// output streams through the v3 write callbacks.
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
/// (served by the v3 memory stdin callback): the generic form of
/// run_command_with_stdin for hosts that already hold the bytes (tests, the
/// stream-ABI driver, the next wiring wave's A/B comparisons). No fd 0 is
/// touched.
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
/// Output streams through the v3 write callbacks (see run_io).
int run_command_with_stdin_source(const char* name,
                                  std::span<std::string_view> args,
                                  winuxcmd_stream_read_fn* stdin_read,
                                  void* stdin_user_data) noexcept {
  return run_io(name, args, stdin_read, stdin_user_data);
}

/// Run rust2 library command `name` with the dispatcher's operands and
/// STREAMING stdin: fd 0 is read in chunks through a host pull callback
/// (the same file_io read path as the whole-buffer slurp — binary mode,
/// cooked-console VEOF ^D records and flush boundaries, broken-pipe EOF —
/// but handed to the command segment by segment, so no whole-buffer input
/// residency: a 64 MiB input no longer costs the slurp). Output streams
/// through the v3 write callbacks (see run_io).
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
  return run_io(name, args, &stdin_feed_read, &state);
}

}  // namespace winux::rust2
