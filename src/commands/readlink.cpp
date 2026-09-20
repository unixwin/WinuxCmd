// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [DIFFERS]
    OPTION("-f", "--canonicalize", "canonicalize by following every symlink",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-e", "--canonicalize-existing",
           "canonicalize by following symlinks (must exist)", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-m", "--canonicalize-missing",
           "canonicalize by following symlinks (may not exist)", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-n", "--no-newline", "do not output the trailing delimiter",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-q", "--quiet", "suppress most error messages", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-s", "--silent", "suppress most error messages", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-v", "--verbose", "report error message", BOOL_TYPE),
    // [DIFFERS]
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

auto open_path_handle(const std::wstring& path, bool follow_reparse)
    -> UniqueHandle {
  DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
  if (!follow_reparse) {
    flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  }

  return UniqueHandle(
      CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, flags, nullptr));
}

// [GNU] -f, -e and -m are canonicalize_filename_mode() CAN_ALL_BUT_LAST,
// CAN_EXISTING and CAN_MISSING: every component is resolved and symlink
// targets are substituted as they are found, so "ldir/f" resolves through
// the intermediate link and a dangling leaf still prints its target's path
// under -f/-m (uutils #10249).
auto canonicalize_path(const std::wstring& original, Mode mode)
    -> std::expected<std::wstring, std::string> {
  native_path::CanonMode canon_mode = native_path::CanonMode::all_but_last;
  if (mode == Mode::canonicalize_existing) {
    canon_mode = native_path::CanonMode::existing;
  } else if (mode == Mode::canonicalize_missing) {
    canon_mode = native_path::CanonMode::missing;
  }

  auto resolved = native_path::canonicalize_path_w(original, canon_mode);
  if (!resolved) {
    switch (resolved.error()) {
      case ELOOP:
        return std::unexpected(
            winux::i18n::format("command.readlink.error.too_many_symlinks",
                                "Too many levels of symbolic links"));
      case ENOTDIR:
        return std::unexpected("Not a directory");
      case ENAMETOOLONG:
        return std::unexpected(winux::i18n::format(
            "command.readlink.error.file_name_too_long", "File name too long"));
      default:
        return std::unexpected("No such file or directory");
    }
  }
  return *resolved;
}

auto read_link_target(const std::wstring& path)
    -> std::expected<std::wstring, std::string> {
  DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return std::unexpected(win32_posix_error_text(GetLastError()));
  }

  if ((attrs & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
    return std::unexpected("Invalid argument");
  }

  UniqueHandle handle = open_path_handle(path, false);
  if (!handle) {
    return std::unexpected(win32_posix_error_text(GetLastError()));
  }

  std::array<std::byte, MAXIMUM_REPARSE_DATA_BUFFER_SIZE> buffer{};
  DWORD returned = 0;
  if (!DeviceIoControl(handle.get(), FSCTL_GET_REPARSE_POINT, nullptr, 0,
                       buffer.data(), static_cast<DWORD>(buffer.size()),
                       &returned, nullptr)) {
    return std::unexpected(win32_posix_error_text(GetLastError()));
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
  bool posixly_correct = posixly_correct_is_set();
  cfg.mode = Mode::link_target;
  cfg.no_newline =
      ctx.get<bool>("--no-newline", false) || ctx.get<bool>("-n", false);
  cfg.verbose = false;
  cfg.quiet = !posixly_correct;
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= READLINK_OPTIONS.size()) {
      continue;
    }

    const auto& meta = (*ctx.metas)[occurrence.index];
    if (meta.long_name == "--canonicalize-missing" || meta.short_name == "-m") {
      cfg.mode = Mode::canonicalize_missing;
      continue;
    }

    if (meta.long_name == "--canonicalize-existing" ||
        meta.short_name == "-e") {
      cfg.mode = Mode::canonicalize_existing;
      continue;
    }

    if (meta.long_name == "--canonicalize" || meta.short_name == "-f") {
      cfg.mode = Mode::canonicalize;
      continue;
    }

    if (meta.long_name == "--verbose" || meta.short_name == "-v") {
      cfg.verbose = true;
      cfg.quiet = false;
      continue;
    }

    if (meta.long_name == "--quiet" || meta.short_name == "-q" ||
        meta.long_name == "--silent" || meta.short_name == "-s") {
      if (posixly_correct) {
        continue;
      }
      cfg.verbose = false;
      cfg.quiet = true;
    }
  }
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
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("readlink: missing operand");
      safeErrorPrintLn("Try 'readlink --help' for more information.");
      return 1;
    }

    cp::report_error(cfg_result, L"readlink");
    return 1;
  }

  return run(*cfg_result);
}
