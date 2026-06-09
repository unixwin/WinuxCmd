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
 *  - File: unlink.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for unlink command.
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

auto constexpr UNLINK_OPTIONS =
    std::array{OPTION("", "", "remove single file", STRING_TYPE)};

namespace {

auto windows_error_text(DWORD error) -> std::string {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
    case ERROR_INVALID_NAME:
      return "No such file or directory";
    case ERROR_ACCESS_DENIED:
      return "Permission denied";
    case ERROR_INVALID_PARAMETER:
      return "Invalid argument";
    default:
      return std::system_category().message(static_cast<int>(error));
  }
}

auto describe_unlink_failure(const std::wstring& path, DWORD error)
    -> std::string {
  DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY)) {
    return "Is a directory";
  }
  if (path.find(L'*') != std::wstring::npos ||
      path.find(L'?') != std::wstring::npos ||
      path.find(L'[') != std::wstring::npos) {
    return "No such file or directory";
  }

  return windows_error_text(error);
}

auto remove_unlink_target(const std::wstring& path) -> bool {
  DWORD attrs = GetFileAttributesW(path.c_str());
  if (attrs != INVALID_FILE_ATTRIBUTES &&
      (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
      (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return RemoveDirectoryW(path.c_str()) != 0;
  }

  return DeleteFileW(path.c_str()) != 0;
}

}  // namespace

REGISTER_COMMAND(unlink,
                 /* name */
                 "unlink",

                 /* synopsis */
                 "unlink FILE",
                 "Remove a specified file.\n"
                 "\n"
                 "Unlink the file named FILE. If FILE is a symbolic link, the "
                 "symbolic link\n"
                 "is removed, not the file it points to.",
                 "  unlink file.txt",

                 /* see also */
                 "rm(1), link(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 UNLINK_OPTIONS) {
  namespace cp = core::pipeline;

  if (ctx.positionals.empty()) {
    safeErrorPrintLn("unlink: missing operand");
    safeErrorPrintLn("Try 'unlink --help' for more information.");
    return 1;
  }
  if (ctx.positionals.size() > 1) {
    safeErrorPrintLn("unlink: extra operand '" +
                     std::string(ctx.positionals[1]) + "'");
    safeErrorPrintLn("Try 'unlink --help' for more information.");
    return 1;
  }

  std::string filename = std::string(ctx.positionals[0]);
  std::vector<std::string> expanded;
  if (contains_wildcard(filename)) {
    auto glob_result = glob_expand(filename);
    if (glob_result.expanded) {
      for (const auto& f : glob_result.files) {
        expanded.push_back(wstring_to_utf8(f));
      }
    }
    if (expanded.empty()) {
      expanded.push_back(filename);
    }
  } else {
    expanded.push_back(filename);
  }

  if (expanded.size() != 1) {
    safeErrorPrintLn("unlink: extra operand '" + expanded[1] + "'");
    safeErrorPrintLn("Try 'unlink --help' for more information.");
    return 1;
  }

  std::wstring wfilename = utf8_to_wstring(expanded[0]);
  BOOL result = remove_unlink_target(wfilename);
  if (!result) {
    DWORD error = GetLastError();
    safeErrorPrintLn("unlink: cannot unlink '" + expanded[0] + "': " +
                     describe_unlink_failure(wfilename, error));
    return 1;
  }

  return 0;
}
