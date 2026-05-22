/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: readlink.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for readlink.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <winioctl.h>

#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

#ifndef IO_REPARSE_TAG_SYMLINK
#define IO_REPARSE_TAG_SYMLINK (0xA000000CL)
#endif

#ifndef IO_REPARSE_TAG_MOUNT_POINT
#define IO_REPARSE_TAG_MOUNT_POINT (0xA0000003L)
#endif

namespace readlink_win32_compat {
#pragma pack(push, 1)
struct ReparseDataBuffer {
  ULONG ReparseTag;
  USHORT ReparseDataLength;
  USHORT Reserved;
  union {
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      ULONG Flags;
      WCHAR PathBuffer[1];
    } SymbolicLinkReparseBuffer;
    struct {
      USHORT SubstituteNameOffset;
      USHORT SubstituteNameLength;
      USHORT PrintNameOffset;
      USHORT PrintNameLength;
      WCHAR PathBuffer[1];
    } MountPointReparseBuffer;
    struct {
      UCHAR DataBuffer[1];
    } GenericReparseBuffer;
  };
};
#pragma pack(pop)
}  // namespace readlink_win32_compat

auto constexpr READLINK_OPTIONS = std::array{
    OPTION("-f", "--canonicalize", "canonicalize by following every symlink",
           BOOL_TYPE),
    OPTION("-e", "--canonicalize-existing",
           "canonicalize by following symlinks (must exist)", BOOL_TYPE),
    OPTION("-m", "--canonicalize-missing",
           "canonicalize by following symlinks (may not exist)", BOOL_TYPE),
    OPTION("-n", "--no-newline", "do not output the trailing delimiter",
           BOOL_TYPE),
    OPTION("-q", "--quiet", "suppress most error messages", BOOL_TYPE),
    OPTION("-s", "--silent", "suppress most error messages", BOOL_TYPE),
    OPTION("-v", "--verbose", "report error message", BOOL_TYPE),
    OPTION("-z", "--zero", "end each output line with NUL", BOOL_TYPE)};

namespace readlink_pipeline {
namespace cp = core::pipeline;

enum class Mode {
  link_target,
  canonicalize,
  canonicalize_existing,
  canonicalize_missing,
};

struct Config {
  Mode mode = Mode::link_target;
  bool no_newline = false;
  bool quiet = false;
  bool verbose = false;
  bool zero_terminated = false;
  SmallVector<std::string, 64> files;
};

struct Handle {
  HANDLE value = INVALID_HANDLE_VALUE;

  Handle() = default;
  explicit Handle(HANDLE h) : value(h) {}
  ~Handle() {
    if (value != INVALID_HANDLE_VALUE && value != nullptr) {
      CloseHandle(value);
    }
  }

  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;

  Handle(Handle&& other) noexcept
      : value(std::exchange(other.value, INVALID_HANDLE_VALUE)) {}
  Handle& operator=(Handle&& other) noexcept {
    if (this != &other) {
      if (value != INVALID_HANDLE_VALUE && value != nullptr) {
        CloseHandle(value);
      }
      value = std::exchange(other.value, INVALID_HANDLE_VALUE);
    }
    return *this;
  }

  [[nodiscard]] explicit operator bool() const {
    return value != INVALID_HANDLE_VALUE && value != nullptr;
  }

  [[nodiscard]] HANDLE get() const { return value; }
};

auto windows_error_text(DWORD error) -> std::string {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return "No such file or directory";
    case ERROR_ACCESS_DENIED:
      return "Permission denied";
    case ERROR_INVALID_PARAMETER:
      return "Invalid argument";
    default:
      return std::system_category().message(static_cast<int>(error));
  }
}

auto readlink_error(const std::string& file, std::string_view reason)
    -> std::string {
  return "readlink: " + file + ": " + std::string(reason);
}

auto posixly_correct_is_set() -> bool {
  return GetEnvironmentVariableW(L"POSIXLY_CORRECT", nullptr, 0) > 0;
}

auto strip_extended_prefix(std::wstring path) -> std::wstring {
  if (path.rfind(L"\\\\?\\UNC\\", 0) == 0) {
    return L"\\\\" + path.substr(8);
  }
  if (path.rfind(L"\\\\?\\", 0) == 0) {
    return path.substr(4);
  }
  if (path.rfind(L"\\??\\", 0) == 0) {
    return path.substr(4);
  }
  return path;
}

auto get_full_path(const std::wstring& path)
    -> std::expected<std::wstring, std::string> {
  DWORD required = GetFullPathNameW(path.c_str(), 0, nullptr, nullptr);
  if (required == 0) {
    return std::unexpected(windows_error_text(GetLastError()));
  }

  std::wstring buffer(required, L'\0');
  DWORD written =
      GetFullPathNameW(path.c_str(), required, buffer.data(), nullptr);
  if (written == 0 || written >= required) {
    return std::unexpected(windows_error_text(GetLastError()));
  }

  buffer.resize(written);
  return strip_extended_prefix(std::move(buffer));
}

auto open_path_handle(const std::wstring& path, bool follow_reparse) -> Handle {
  DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
  if (!follow_reparse) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  return Handle(
      CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr));
}

auto path_from_handle(HANDLE handle)
    -> std::expected<std::wstring, std::string> {
  DWORD required = GetFinalPathNameByHandleW(
      handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (required == 0) {
    return std::unexpected(windows_error_text(GetLastError()));
  }

  std::wstring buffer(required, L'\0');
  DWORD written = GetFinalPathNameByHandleW(
      handle, buffer.data(), required, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
  if (written == 0 || written >= required) {
    return std::unexpected(windows_error_text(GetLastError()));
  }

  buffer.resize(written);
  return strip_extended_prefix(std::move(buffer));
}

auto path_suffix(const std::filesystem::path& full,
                 const std::filesystem::path& prefix) -> std::filesystem::path {
  auto full_it = full.begin();
  auto prefix_it = prefix.begin();
  while (full_it != full.end() && prefix_it != prefix.end() &&
         *full_it == *prefix_it) {
    ++full_it;
    ++prefix_it;
  }

  std::filesystem::path suffix;
  for (; full_it != full.end(); ++full_it) {
    suffix /= *full_it;
  }
  return suffix;
}

auto find_existing_prefix(const std::filesystem::path& absolute)
    -> std::filesystem::path {
  std::error_code ec;
  auto current = absolute;
  while (!current.empty()) {
    if (std::filesystem::exists(current, ec) && !ec) {
      return current;
    }

    auto parent = current.parent_path();
    if (parent == current) {
      break;
    }
    current = parent;
  }

  return {};
}

auto canonicalize_existing(const std::filesystem::path& absolute)
    -> std::expected<std::wstring, std::string> {
  Handle handle = open_path_handle(absolute.wstring(), true);
  if (!handle) {
    return std::unexpected(windows_error_text(GetLastError()));
  }

  return path_from_handle(handle.get());
}

auto canonicalize_path(const std::wstring& original, Mode mode)
    -> std::expected<std::wstring, std::string> {
  auto full_path_result = get_full_path(original);
  if (!full_path_result) {
    return std::unexpected(full_path_result.error());
  }

  std::filesystem::path absolute(*full_path_result);
  std::error_code ec;
  bool exists = std::filesystem::exists(absolute, ec) && !ec;

  if (mode == Mode::canonicalize_existing) {
    if (!exists) {
      return std::unexpected("No such file or directory");
    }
    return canonicalize_existing(absolute);
  }

  if (mode == Mode::canonicalize) {
    if (exists) {
      return canonicalize_existing(absolute);
    }

    auto parent = absolute.parent_path();
    std::error_code parent_ec;
    if (parent.empty() || !std::filesystem::exists(parent, parent_ec) ||
        parent_ec || !std::filesystem::is_directory(parent, parent_ec) ||
        parent_ec) {
      return std::unexpected("No such file or directory");
    }

    auto parent_resolved = canonicalize_existing(parent);
    if (!parent_resolved) {
      return parent_resolved;
    }

    auto suffix = path_suffix(absolute, parent);
    return (std::filesystem::path(*parent_resolved) / suffix).wstring();
  }

  if (exists) {
    return canonicalize_existing(absolute);
  }

  auto prefix = find_existing_prefix(absolute);
  if (prefix.empty()) {
    return std::unexpected("No such file or directory");
  }

  auto suffix = path_suffix(absolute, prefix);
  if (!suffix.empty()) {
    std::error_code dir_ec;
    if (!std::filesystem::is_directory(prefix, dir_ec) || dir_ec) {
      return std::unexpected("No such file or directory");
    }
  }

  auto prefix_resolved = canonicalize_existing(prefix);
  if (!prefix_resolved) {
    return prefix_resolved;
  }

  return (std::filesystem::path(*prefix_resolved) / suffix).wstring();
}

auto read_link_target(const std::wstring& path)
    -> std::expected<std::wstring, std::string> {
  DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return std::unexpected(windows_error_text(GetLastError()));
  }

  if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
    return std::unexpected("Invalid argument");
  }

  Handle handle = open_path_handle(path, false);
  if (!handle) {
    return std::unexpected(windows_error_text(GetLastError()));
  }

  std::array<std::byte, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> buffer{};
  DWORD returned = 0;
  if (!DeviceIoControl(handle.get(), FSCTL_GET_REPARSE_POINT, nullptr, 0,
                       buffer.data(), static_cast<DWORD>(buffer.size()),
                       &returned, nullptr)) {
    return std::unexpected(windows_error_text(GetLastError()));
  }

  auto* reparse = reinterpret_cast<readlink_win32_compat::ReparseDataBuffer*>(
      buffer.data());
  if (reparse->ReparseTag == IO_REPARSE_TAG_SYMLINK) {
    const auto& sl = reparse->SymbolicLinkReparseBuffer;
    std::wstring_view print_name(
        sl.PathBuffer + sl.PrintNameOffset / sizeof(wchar_t),
        sl.PrintNameLength / sizeof(wchar_t));
    if (print_name.empty()) {
      std::wstring_view substitute_name(
          sl.PathBuffer + sl.SubstituteNameOffset / sizeof(wchar_t),
          sl.SubstituteNameLength / sizeof(wchar_t));
      return strip_extended_prefix(std::wstring(substitute_name));
    }
    return strip_extended_prefix(std::wstring(print_name));
  }

  if (reparse->ReparseTag == IO_REPARSE_TAG_MOUNT_POINT) {
    const auto& mp = reparse->MountPointReparseBuffer;
    std::wstring_view print_name(
        mp.PathBuffer + mp.PrintNameOffset / sizeof(wchar_t),
        mp.PrintNameLength / sizeof(wchar_t));
    if (print_name.empty()) {
      std::wstring_view substitute_name(
          mp.PathBuffer + mp.SubstituteNameOffset / sizeof(wchar_t),
          mp.SubstituteNameLength / sizeof(wchar_t));
      return strip_extended_prefix(std::wstring(substitute_name));
    }
    return strip_extended_prefix(std::wstring(print_name));
  }

  return std::unexpected("Unsupported reparse point");
}

auto build_config(const CommandContext<READLINK_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.mode = Mode::link_target;
  if (ctx.get<bool>("--canonicalize-missing", false) ||
      ctx.get<bool>("-m", false)) {
    cfg.mode = Mode::canonicalize_missing;
  } else if (ctx.get<bool>("--canonicalize-existing", false) ||
             ctx.get<bool>("-e", false)) {
    cfg.mode = Mode::canonicalize_existing;
  } else if (ctx.get<bool>("--canonicalize", false) ||
             ctx.get<bool>("-f", false)) {
    cfg.mode = Mode::canonicalize;
  }
  cfg.no_newline =
      ctx.get<bool>("--no-newline", false) || ctx.get<bool>("-n", false);
  cfg.verbose = ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false) ||
              ctx.get<bool>("-s", false) ||
              (!cfg.verbose && !posixly_correct_is_set());
  cfg.zero_terminated =
      ctx.get<bool>("--zero", false) || ctx.get<bool>("-z", false);

  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    cfg.files.push_back(file_arg);
  }

  if (cfg.files.empty()) {
    return std::unexpected("missing operand");
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;
  bool no_newline = cfg.no_newline;
  if (cfg.no_newline && cfg.files.size() > 1) {
    safeErrorPrintLn(
        "readlink: warning: ignoring --no-newline with multiple "
        "arguments");
    no_newline = false;
  }

  for (const auto& file : cfg.files) {
    auto resolved = [&]() -> std::expected<std::wstring, std::string> {
      if (cfg.mode == Mode::link_target) {
        return read_link_target(utf8_to_wstring(file));
      }
      return canonicalize_path(utf8_to_wstring(file), cfg.mode);
    }();

    if (!resolved) {
      all_ok = false;
      if (!cfg.quiet) {
        safeErrorPrintLn(readlink_error(file, resolved.error()));
      }
      continue;
    }

    safePrint(*resolved);
    if (cfg.zero_terminated) {
      safePrint('\0');
    } else if (!no_newline) {
      safePrint('\n');
    }
  }

  return all_ok ? 0 : 1;
}

}  // namespace readlink_pipeline

REGISTER_COMMAND(
    readlink, "readlink", "readlink [OPTION]... FILE...",
    "Print value of a symbolic link or canonical file name.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This is a Windows implementation. Windows supports\n"
    "reparse points (symlinks/junctions) but they work differently\n"
    "than Unix symlinks. This command provides basic detection.",
    "  readlink file\n"
    "  readlink -f file\n"
    "  readlink -e file",
    "ln(1), realpath(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    READLINK_OPTIONS) {
  using namespace readlink_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"readlink");
    return 1;
  }

  return run(*cfg_result);
}
