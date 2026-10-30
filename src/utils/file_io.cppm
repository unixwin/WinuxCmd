/// @Author: caomengxuan666
/// @Description: File I/O utilities
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
module;

// Pre-include a C++ header so c++config.h's std declarations (e.g.
// std::__is_constant_evaluated) are first seen with C++ linkage: pch.h's
// windows.h chain (winnt.h -> x86intrin -> mm_malloc -> stdlib.h -> cstdlib)
// would otherwise declare them inside winnt.h's extern "C" region, which
// conflicts with the same declarations imported from module std (#1412).
#include <cstddef>
// _fdopen (used below) comes from <stdio.h>; MSVC's header chain leaks it
// into this TU, GCC's does not.
#include <stdio.h>
// os_defines.h (via cstddef) defines NOMINMAX as 1; drop it so pch.h's own
// bare definition below applies exactly as before, without a warning.
#undef NOMINMAX

#include "pch/pch.h"
export module utils:file_io;

import std;
import :utf8;
import :native_path;
import :win32;
import :i18n;

namespace {
constexpr size_t kReadChunkSize = 64 * 1024;

auto read_open_error(std::string_view path,
                     const native_path::ApiPathOperand& operand,
                     unsigned long error) -> std::string {
  const DWORD attrs = native_path::operand_target_attributes_w(operand);
  if (operand.had_trailing_separator &&
      native_path::attributes_are_regular_file(attrs)) {
    return winux::i18n::format("utils.file.error.not_directory",
                               "cannot open '{}' for reading: Not a directory",
                               path);
  }

  if (native_path::attributes_are_directory(attrs)) {
    return winux::i18n::format("utils.file.error.is_directory",
                               "cannot open '{}' for reading: Is a directory",
                               path);
  }

  return winux::i18n::format(
      "utils.file.error.open", "cannot open '{}' for reading: {}", path,
      win32_posix_error_text(error, {.invalid_name_as_missing = true}));
}

auto reserve_file_size(std::string& content, HANDLE file) -> void {
  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(file, &file_size) || file_size.QuadPart <= 0) return;

  const auto size = static_cast<unsigned long long>(file_size.QuadPart);
  if (size <= static_cast<unsigned long long>(content.max_size())) {
    content.reserve(static_cast<size_t>(size));
  }
}

auto read_handle_to_string(HANDLE file, std::string_view path)
    -> std::expected<std::string, std::string> {
  std::string content;
  reserve_file_size(content, file);

  std::array<char, kReadChunkSize> buffer{};
  for (;;) {
    DWORD bytes_read = 0;
    if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &bytes_read, nullptr)) {
      return std::unexpected(winux::i18n::format("utils.file.error.read",
                                                 "error reading '{}'", path));
    }
    if (bytes_read == 0) break;
    content.append(buffer.data(), bytes_read);
  }

  return content;
}
}  // namespace

// NOTE: no `export` on the declarations inside this block — an export
// namespace already exports them, and a second `export` is ill-formed
// under GCC (the #1401 defect shape; cf. version.cppm).
export namespace file_io {

// A trailing separator makes the operand require a directory target
// (POSIX ENOTDIR). Windows path normalization strips it, which would
// silently turn "file/" into "file" and read/truncate the regular file
// (#1052 — data loss). Refuse by returning a failed stream and setting
// errno so callers report the right diagnostic.
// Open a CRT std fd (0/1/2) as a stream for /dev/std{in,out,err}-style
// operands. The fd is dup'ed so closing the stream never closes the real
// standard handle. A closed fd reports ENOENT: under GNU, /dev/stdin is a
// symlink into /proc/self/fd which dangles when the fd is closed (#1056).
auto open_std_fd_stream(int fd, bool write) -> std::FILE* {
  const int dup_fd = _dup(fd);
  if (dup_fd == -1) {
    errno = ENOENT;
    return nullptr;
  }
  std::FILE* file = _fdopen(dup_fd, write ? "wb" : "rb");
  if (!file) {
    const int saved = errno;
    _close(dup_fd);
    errno = saved;
    return nullptr;
  }
  return file;
}

auto failed_ifstream(int error) -> std::ifstream {
  errno = error;
  std::ifstream failed;
  failed.setstate(std::ios::failbit);
  return failed;
}

auto failed_ofstream(int error) -> std::ofstream {
  errno = error;
  std::ofstream failed;
  failed.setstate(std::ios::failbit);
  return failed;
}

// Bridge an on-disk fifo marker (#1038) to a Windows named pipe so
// WinuxCmd readers/writers get POSIX open() blocking semantics:
// opening for read blocks in ConnectNamedPipe until a writer arrives,
// opening for write waits until a reader creates the endpoint.
auto open_fifo_read_handle(std::wstring_view path) -> HANDLE {
  const std::wstring pipe_name = native_path::winux_fifo_pipe_name_w(path);
  HANDLE pipe = CreateNamedPipeW(
      pipe_name.c_str(), PIPE_ACCESS_INBOUND,
      PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT, PIPE_UNLIMITED_INSTANCES,
      64 * 1024, 64 * 1024, 0, nullptr);
  if (pipe == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
  if (!ConnectNamedPipe(pipe, nullptr) &&
      GetLastError() != ERROR_PIPE_CONNECTED) {
    CloseHandle(pipe);
    return INVALID_HANDLE_VALUE;
  }
  return pipe;
}

auto open_fifo_write_handle(std::wstring_view path) -> HANDLE {
  const std::wstring pipe_name = native_path::winux_fifo_pipe_name_w(path);
  for (;;) {
    HANDLE pipe = CreateFileW(pipe_name.c_str(), GENERIC_WRITE, 0, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (pipe != INVALID_HANDLE_VALUE) return pipe;
    const DWORD error = GetLastError();
    if (error == ERROR_PIPE_BUSY) {
      // GNU: O_WRONLY on a fifo blocks until a reader opens the pipe.
      WaitNamedPipeW(pipe_name.c_str(), NMPWAIT_WAIT_FOREVER);
      continue;
    }
    if (error == ERROR_FILE_NOT_FOUND) {
      // No reader has created the endpoint yet; keep waiting.
      Sleep(1);
      continue;
    }
    return INVALID_HANDLE_VALUE;
  }
}

auto open_fifo_read_stream(std::wstring_view path) -> std::ifstream {
  HANDLE pipe = open_fifo_read_handle(path);
  if (pipe == INVALID_HANDLE_VALUE) return failed_ifstream(EACCES);
  const int fd =
      _open_osfhandle(reinterpret_cast<intptr_t>(pipe), _O_RDONLY | _O_BINARY);
  if (fd == -1) {
    CloseHandle(pipe);
    return failed_ifstream(EACCES);
  }
  std::FILE* file = _fdopen(fd, "rb");
  if (!file) {
    const int saved = errno;
    _close(fd);
    errno = saved;
    return failed_ifstream(errno);
  }
  return std::ifstream(file);
}

auto open_fifo_write_stream(std::wstring_view path) -> std::ofstream {
  HANDLE pipe = open_fifo_write_handle(path);
  if (pipe == INVALID_HANDLE_VALUE) return failed_ofstream(EACCES);
  const int fd =
      _open_osfhandle(reinterpret_cast<intptr_t>(pipe), _O_WRONLY | _O_BINARY);
  if (fd == -1) {
    CloseHandle(pipe);
    return failed_ofstream(EACCES);
  }
  std::FILE* file = _fdopen(fd, "wb");
  if (!file) {
    const int saved = errno;
    _close(fd);
    errno = saved;
    return failed_ofstream(errno);
  }
  return std::ofstream(file);
}

auto open_binary_file(std::string_view filename) -> std::ifstream {
  // /dev/stdin-family operands bind to the real fd 0 so piped input works
  // exactly like GNU (and a closed stdin reports failure, #1056/#973).
  if (native_path::pseudo_device_std_fd(filename) == std::optional<int>(0)) {
    if (std::FILE* file = open_std_fd_stream(0, false)) {
      return std::ifstream(file);
    }
    return failed_ifstream(errno);
  }
  auto operand = native_path::make_api_path_operand(filename);
  if (int err = native_path::operand_file_open_error(operand)) {
    errno = err;
    // A default-constructed stream has goodbit set; callers test `!file`,
    // so failbit must be raised explicitly.
    std::ifstream failed;
    failed.setstate(std::ios::failbit);
    return failed;
  }
  if (native_path::is_winux_fifo_w(operand.normalized)) {
    return open_fifo_read_stream(operand.normalized);
  }
  return std::ifstream(std::filesystem::path(operand.extended),
                       std::ios::binary);
}

auto create_binary_file(std::string_view filename, bool append = false)
    -> std::ofstream {
  // /dev/stdout- and /dev/stderr-family operands bind to the real fd so
  // output follows redirections like GNU instead of going to the console
  // (#1056). Writing to fd 0 (/dev/stdin) is left to the device-name path.
  if (auto fd = native_path::pseudo_device_std_fd(filename); fd && *fd >= 1) {
    if (std::FILE* file = open_std_fd_stream(*fd, true)) {
      return std::ofstream(file);
    }
    return failed_ofstream(errno);
  }
  auto operand = native_path::make_api_path_operand(filename);
  if (int err = native_path::operand_file_open_error(operand, true)) {
    errno = err;
    std::ofstream failed;
    failed.setstate(std::ios::failbit);
    return failed;
  }
  if (native_path::is_winux_fifo_w(operand.normalized)) {
    return open_fifo_write_stream(operand.normalized);
  }
  return std::ofstream(
      std::filesystem::path(operand.extended),
      std::ios::binary | (append ? std::ios::app : std::ios::trunc));
}

// [GNU] A closed standard input (<&-) is a read error, not EOF (#973): the
// CRT reports EBADF on the first read while std::cin would silently yield
// EOF. _lseek probes fd validity without disturbing pipes (ESPIPE) or
// regular files (no-op).
auto stdin_is_bad() -> bool {
  errno = 0;
  return _lseek(0, 0, SEEK_CUR) == -1 && errno == EBADF;
}

// Unix filters consume fd 0 as a byte stream, but the CRT opens it in text
// mode by default: reads then stop at the first 0x1A (Ctrl-Z) byte and
// collapse CRLF to LF, so a piped payload silently loses data while the
// same bytes via a file operand come through intact. Commands that read
// stdin themselves call this before their first read (the idiom cat/wc/tr
// and the checksum family already use) so the pipe and operand forms agree.
auto set_stdin_binary() -> void {
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
#endif
}

// ---- winux#1450: cooked-console VEOF (line-discipline ^D) emulation ----
//
// GNU filters reading a canonical tty get their EOF and mid-line flushes
// from the kernel line discipline (termios VEOF): ^D with an empty
// canonical buffer ends the input stream; ^D after pending characters
// delivers those characters to the reader immediately, without a newline.
// The Windows cooked console (ENABLE_LINE_INPUT) has no line discipline:
// ReadFile returns only on Enter, and the 0x04 keystroke enters the line
// buffer as a plain character, so an interactive `cat` never observes EOF
// and swallows every line typed afterwards until ^C (winux#1450 baseline).
//
// read_stdin_record restores the contract on exactly that face — "stdin is
// a console handle in line-input mode" — by filtering the records the
// console delivers to ReadFile:
//   - 0x04 with no pending characters in the current canonical line (line
//     start) ends the input: the caller gets a zero-byte read, and the
//     unread tail of that console record is re-injected into the console
//     input queue (WriteConsoleInput) so the next reader — the shell —
//     sees it, the way the tty queue does under Unix;
//   - 0x04 after pending characters delivers the pending prefix to the
//     caller as its own record (no newline) and the remaining bytes of the
//     console record continue as the following record;
//   - every other byte passes through untouched, including the \r\n the
//     console appends, so pipe/file input (this reader is not armed there)
//     and non-^D console bytes stay byte-identical to the plain ReadFile
//     path.
//
// Known limits (documented, by scope):
//   - Mid-line ^D flushes are delivered together with the rest of the same
//     console record, so the consumer sees the flush only after the next
//     Enter completes the line; releasing the prefix earlier would require
//     raw-mode line-discipline emulation, which is out of scope here.
//   - Only callers that route stdin through this helper get the semantics;
//     std::cin-based whole-input readers (file_io::read_all_stdin) and
//     applet-private loops (head, dd) still need to adopt it.
//   - If WriteConsoleInput fails, the EOF tail is dropped rather than
//     re-injected (the caller's EOF contract is unaffected).
struct StdinRecordState {
  HANDLE handle = nullptr;
  bool probed = false;
  bool armed = false;
  bool eof = false;
  bool line_pending = false;  // canonical buffer holds >=1 undelivered char
  std::string pending;
};

thread_local StdinRecordState g_stdin_record;

// Re-inject the unread tail of a console record into the console input
// queue so the next reader (normally the shell) receives it, the way the
// Unix tty queue retains input typed after the EOF ^D. WriteConsoleInput
// appends behind pending events (per MSDN), so forward order preserves the
// typed sequence. The cooked record ends lines with \r\n; each pair becomes
// a single Enter key event so the shell does not observe a spurious second
// newline.
auto pushback_console_bytes(HANDLE handle, const char* bytes, size_t len)
    -> void {
  if (len == 0) return;
  const UINT cp = GetConsoleCP();
  const int wlen = MultiByteToWideChar(cp, 0, bytes,
                                       static_cast<int>(len), nullptr, 0);
  if (wlen <= 0) return;  // undecodable tail: drop (documented fallback)

  std::wstring wide(static_cast<size_t>(wlen), L'\0');
  if (MultiByteToWideChar(cp, 0, bytes, static_cast<int>(len), wide.data(),
                          wlen) <= 0) {
    return;
  }

  std::vector<INPUT_RECORD> records;
  records.reserve(wide.size() * 2);
  bool prev_was_cr = false;
  for (wchar_t wc : wide) {
    if (wc == L'\n') {
      if (prev_was_cr) {  // CRLF pair -> one Enter event already queued
        prev_was_cr = false;
        continue;
      }
      wc = L'\r';  // lone LF is still a line terminator in cooked input
    }
    INPUT_RECORD rec{};
    rec.EventType = KEY_EVENT;
    rec.Event.KeyEvent.bKeyDown = TRUE;
    rec.Event.KeyEvent.wRepeatCount = 1;
    rec.Event.KeyEvent.wVirtualKeyCode = (wc == L'\r') ? VK_RETURN : 0;
    rec.Event.KeyEvent.wVirtualScanCode = 0;
    rec.Event.KeyEvent.uChar.UnicodeChar = wc;
    rec.Event.KeyEvent.dwControlKeyState = 0;
    records.push_back(rec);
    rec.Event.KeyEvent.bKeyDown = FALSE;  // matching key-up
    records.push_back(rec);
    prev_was_cr = (wc == L'\r');
  }

  DWORD written = 0;
  WriteConsoleInputW(handle, records.data(),
                     static_cast<DWORD>(records.size()), &written);
}

// ReadFile-compatible stdin read with VEOF semantics on the cooked console
// face (see the block comment above). Returns TRUE on success — including
// the emulated EOF, which reports *got == 0 — and propagates ReadFile's
// own status otherwise.
auto read_stdin_record(HANDLE handle, char* buffer, DWORD size, DWORD* got)
    -> bool {
  if (!g_stdin_record.probed || g_stdin_record.handle != handle) {
    g_stdin_record = StdinRecordState{};
    g_stdin_record.handle = handle;
    g_stdin_record.probed = true;
    DWORD mode = 0;
    g_stdin_record.armed = handle != INVALID_HANDLE_VALUE &&
                           handle != nullptr && GetConsoleMode(handle, &mode) &&
                           (mode & ENABLE_LINE_INPUT) != 0;
  }
  if (!g_stdin_record.armed) {
    return ReadFile(handle, buffer, size, got, nullptr);
  }

  auto& state = g_stdin_record;
  while (!state.eof && state.pending.empty()) {
    std::array<char, 8192> raw{};
    DWORD n = 0;
    if (!ReadFile(handle, raw.data(),
                  static_cast<DWORD>(raw.size()), &n, nullptr) ||
        n == 0) {
      state.eof = true;
      break;
    }
    for (DWORD i = 0; i < n; ++i) {
      const auto c = static_cast<unsigned char>(raw[i]);
      if (c == 0x04) {  // VEOF
        if (!state.line_pending) {
          // ^D at line start: end the stream; the tail of this console
          // record stays queued for the next reader.
          pushback_console_bytes(handle, raw.data() + i + 1, n - i - 1);
          state.eof = true;
          break;
        }
        // ^D mid-line: flush the pending prefix as its own record; the
        // remainder of the console record continues below (0x04 itself is
        // consumed, like the line discipline does).
        state.line_pending = false;
        continue;
      }
      state.pending.push_back(static_cast<char>(c));
      // The console ends a cooked record's lines with \r\n; the LF is the
      // point where the canonical buffer empties.
      state.line_pending = (c != '\n');
    }
  }

  const DWORD out = static_cast<DWORD>(
      std::min<size_t>(state.pending.size(), size));
  if (out > 0) {
    std::memcpy(buffer, state.pending.data(), out);
    state.pending.erase(0, out);
  }
  *got = out;
  return TRUE;
}

auto read_all_stdin() -> std::expected<std::string, std::string> {
  if (stdin_is_bad()) {
    return std::unexpected(winux::i18n::translate(
        "utils.file.error.read_stdin_bad_fd",
        "error reading 'standard input': Bad file descriptor"));
  }
#ifdef _WIN32
  _setmode(_fileno(stdin), _O_BINARY);
#endif
  std::string content;
  std::array<char, kReadChunkSize> buffer{};

  while (std::cin.good()) {
    std::cin.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const auto bytes_read = std::cin.gcount();
    if (bytes_read > 0) {
      content.append(buffer.data(), static_cast<size_t>(bytes_read));
    }
  }

  if (std::cin.bad()) {
    return std::unexpected(winux::i18n::translate(
        "utils.file.error.read_stdin", "error reading from standard input"));
  }

  return content;
}

auto read_all_file(std::string_view filename)
    -> std::expected<std::string, std::string> {
  // /dev/stdin-family operands read the real fd 0 like GNU (#1056).
  if (native_path::pseudo_device_std_fd(filename) == std::optional<int>(0)) {
    return read_all_stdin();
  }
  auto operand = native_path::make_api_path_operand(filename);
  if (operand.had_trailing_separator) {
    const DWORD attrs = native_path::operand_target_attributes_w(operand);
    if (native_path::attributes_are_regular_file(attrs)) {
      return std::unexpected(
          read_open_error(filename, operand, ERROR_DIRECTORY));
    }
  }

  HANDLE file =
      CreateFileW(operand.extended.c_str(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
  if (file == INVALID_HANDLE_VALUE) {
    return std::unexpected(read_open_error(filename, operand, GetLastError()));
  }

  if (native_path::is_winux_fifo_w(operand.normalized)) {
    CloseHandle(file);
    file = open_fifo_read_handle(operand.normalized);
    if (file == INVALID_HANDLE_VALUE) {
      return std::unexpected(winux::i18n::format(
          "utils.file.error.open", "cannot open '{}' for reading: {}", filename,
          win32_posix_error_text(GetLastError(),
                                 {.invalid_name_as_missing = true})));
    }
  }

  UniqueHandle close_file(file);
  return read_handle_to_string(close_file.get(), filename);
}

auto read_all_input(std::string_view filename)
    -> std::expected<std::string, std::string> {
  if (filename == "-") return read_all_stdin();
  return read_all_file(filename);
}

}  // namespace file_io

/**
 * @brief Read file into lines
 * @param filename File path
 * @return Vector of lines (empty on error)
 */
export std::vector<std::string> read_file_lines(const std::string& filename) {
  std::vector<std::string> lines;

  auto content_result = file_io::read_all_file(filename);
  if (!content_result) {
    return lines;
  }

  const auto& content = *content_result;
  size_t start = 0;
  if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
      static_cast<unsigned char>(content[1]) == 0xBB &&
      static_cast<unsigned char>(content[2]) == 0xBF) {
    start = 3;
  }

  std::istringstream iss(content.substr(start));
  std::string line;
  while (std::getline(iss, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    lines.push_back(line);
  }

  return lines;
}
