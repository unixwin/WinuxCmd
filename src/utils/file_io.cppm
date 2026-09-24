/// @Author: caomengxuan666
/// @Description: File I/O utilities
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
module;

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
export auto open_fifo_read_handle(std::wstring_view path) -> HANDLE {
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

export auto open_fifo_write_handle(std::wstring_view path) -> HANDLE {
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

export auto open_binary_file(std::string_view filename) -> std::ifstream {
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

export auto create_binary_file(std::string_view filename, bool append = false)
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
export auto stdin_is_bad() -> bool {
  errno = 0;
  return _lseek(0, 0, SEEK_CUR) == -1 && errno == EBADF;
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
