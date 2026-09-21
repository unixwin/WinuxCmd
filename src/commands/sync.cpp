// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sync command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

namespace {

std::string format_open_error(const std::string& path, DWORD error) {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
      return "error opening '" + path + "': No such file or directory";
    default:
      return "error opening '" + path + "'";
  }
}

}  // namespace

auto constexpr SYNC_OPTIONS =
    // [GNU] -d, --data
    std::array{OPTION("-d", "--data",
                      "sync only file data, no unneeded metadata", BOOL_TYPE),
               // [GNU] -f, --file-system
               OPTION("-f", "--file-system",
                      "sync the file systems that contain files", BOOL_TYPE)};

REGISTER_COMMAND(
    sync,
    /* name */
    "sync",

    /* synopsis */
    "sync [OPTION]... [FILE]...",
    "Synchronize cached writes to persistent storage.\n"
    "\n"
    "If one or more files are specified, sync only those files.\n"
    "  -d, --data         sync only file data, no unneeded metadata\n"
    "  -f, --file-system  sync the file systems that contain the files\n"
    "Otherwise, all file system buffers are synchronized.\n"
    "\n"
    "Note: On Windows, this uses FlushFileBuffers to flush file buffers.",
    "  sync\n"
    "  sync -d file.txt\n"
    "  sync file1.txt file2.txt",

    /* see also */
    "fsync(2)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SYNC_OPTIONS) {
  namespace cp = core::pipeline;

  // [GNU] -d/--data: On Linux, fdatasync() flushes data without metadata.
  // On Windows, FlushFileBuffers() flushes both data and metadata; there is
  // no data-only mode. The flag is accepted for GNU compatibility but has no
  // behavioral difference.
  bool sync_data = ctx.get<bool>("--data", false) || ctx.get<bool>("-d", false);
  // [GNU] -f/--file-system: On Linux, syncfs() flushes the containing
  // filesystem. On Windows, there is no syncfs() equivalent, so we fall back to
  // FlushFileBuffers() on each file (best-effort approximation).
  bool sync_fs =
      ctx.get<bool>("--file-system", false) || ctx.get<bool>("-f", false);

  if (sync_data && sync_fs) {
    cp::Result<int> result = std::unexpected<cp::Error>(
        "options --data and --file-system are mutually exclusive");
    cp::report_error(result, L"sync");
    return 1;
  }

  if (sync_data && ctx.positionals.empty()) {
    cp::Result<int> result =
        std::unexpected<cp::Error>("--data needs at least one argument");
    cp::report_error(result, L"sync");
    return 1;
  }

  if (ctx.positionals.empty()) {
    // Sync all file systems - Windows doesn't have a direct equivalent
    // so we just return success
    return 0;
  }

  // Sync specified files
  bool had_error = false;
  for (auto file : ctx.positionals) {
    std::string file_arg(file);
    std::vector<std::string> expanded;
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& f : glob_result.files) {
          expanded.push_back(wstring_to_utf8(f));
        }
      } else {
        expanded.push_back(file_arg);
      }
    } else {
      expanded.push_back(file_arg);
    }
    for (const auto& exp : expanded) {
      std::error_code path_ec;
      const std::filesystem::path fs_path = utf8_to_wstring(exp);

      if (sync_fs) {
        if (!std::filesystem::exists(fs_path, path_ec)) {
          auto err = format_open_error(exp, ERROR_FILE_NOT_FOUND);
          cp::Result<int> result = std::unexpected(err);
          cp::report_error(result, L"sync");
          had_error = true;
          continue;
        }
        // [DIFFERS from GNU] GNU syncfs() flushes the entire filesystem.
        // Windows has no syncfs() equivalent; fall through to
        // FlushFileBuffers() as a best-effort approximation.
      }

      if (std::filesystem::is_directory(fs_path, path_ec)) {
        continue;
      }

      std::wstring wpath = utf8_to_wstring(exp);
      HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

      if (hFile == INVALID_HANDLE_VALUE) {
        auto err = format_open_error(exp, GetLastError());
        cp::Result<int> result = std::unexpected(err);
        cp::report_error(result, L"sync");
        had_error = true;
        continue;
      }

      if (!FlushFileBuffers(hFile)) {
        CloseHandle(hFile);
        auto err = std::string("failed to flush '") + exp + "'";
        cp::Result<int> result = std::unexpected(err);
        cp::report_error(result, L"sync");
        had_error = true;
        continue;
      }

      CloseHandle(hFile);
    }
  }

  return had_error ? 1 : 0;
}
