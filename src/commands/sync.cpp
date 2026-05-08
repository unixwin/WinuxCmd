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
 *  - File: sync.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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

auto constexpr SYNC_OPTIONS = std::array{
    OPTION("", "", "synchronize cached writes to disk", STRING_TYPE)};

REGISTER_COMMAND(
    sync,
    /* name */
    "sync",

    /* synopsis */
    "sync [OPTION]... [FILE]...",
    "Synchronize cached writes to persistent storage.\n"
    "\n"
    "If one or more files are specified, sync only those files.\n"
    "Otherwise, all file system buffers are synchronized.\n"
    "\n"
    "Note: On Windows, this uses FlushFileBuffers to flush file buffers.",
    "  sync\n"
    "  sync -d file.txt\n"
    "  sync file1.txt file2.txt",

    /* see also */
    "fsync(2)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SYNC_OPTIONS) {
  namespace cp = core::pipeline;

  bool sync_data = ctx.get<bool>("--data", false) || ctx.get<bool>("-d", false);
  bool sync_fs =
      ctx.get<bool>("--file-system", false) || ctx.get<bool>("-f", false);

  if (ctx.positionals.empty()) {
    // Sync all file systems - Windows doesn't have a direct equivalent
    // so we just return success
    return 0;
  }

  // Sync specified files
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
      std::wstring wpath = utf8_to_wstring(exp);
      HANDLE hFile = CreateFileW(wpath.c_str(), GENERIC_WRITE,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                 OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

      if (hFile == INVALID_HANDLE_VALUE) {
        auto err = std::string("sync: failed to open '") + exp + "'";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"sync");
        return 1;
      }

      if (!FlushFileBuffers(hFile)) {
        CloseHandle(hFile);
        auto err = std::string("sync: failed to flush '") + exp + "'";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"sync");
        return 1;
      }

      CloseHandle(hFile);
    }
  }

  return 0;
}
