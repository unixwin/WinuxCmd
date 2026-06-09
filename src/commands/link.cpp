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
 *  - File: link.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for link command.
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

auto constexpr LINK_OPTIONS =
    std::array{OPTION("", "", "create link to file", STRING_TYPE)};

namespace {

auto emit_link_operand_count_error() -> int {
  safeErrorPrintLn("link: 2 values required");
  return 1;
}

auto link_windows_error_text(DWORD error) -> std::string {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return "No such file or directory";
    case ERROR_FILE_EXISTS:
    case ERROR_ALREADY_EXISTS:
      return "File exists";
    case ERROR_ACCESS_DENIED:
      return "Permission denied";
    case ERROR_INVALID_PARAMETER:
      return "Invalid argument";
    default:
      return std::system_category().message(static_cast<int>(error));
  }
}

auto resolve_source_operand(const std::string& source)
    -> std::expected<std::string, std::string> {
  if (!contains_wildcard(source)) {
    return source;
  }

  auto glob_result = glob_expand(source);
  if (glob_result.expanded && !glob_result.files.empty()) {
    if (glob_result.files.size() != 1) {
      return std::unexpected("2 values required");
    }
    return wstring_to_utf8(glob_result.files[0]);
  }

  return source;
}

}  // namespace

REGISTER_COMMAND(
    link,
    /* name */
    "link",

    /* synopsis */
    "link [OPTION]... FILE LINKNAME",
    "Create a hard link to FILE named LINKNAME.\n"
    "\n"
    "On Windows, this creates a hard link using CreateHardLink API.\n"
    "Note: Hard links only work on NTFS file systems.\n"
    "\n"
    "This command has no options other than --help and --version.",
    "  link file.txt link_to_file.txt\n"
    "  link existing new_link",

    /* see also */
    "ln(1), unlink(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", LINK_OPTIONS) {
  namespace cp = core::pipeline;

  if (ctx.positionals.size() < 2) {
    return emit_link_operand_count_error();
  }
  if (ctx.positionals.size() > 2) {
    return emit_link_operand_count_error();
  }

  std::string source_arg = std::string(ctx.positionals[0]);
  auto source_result = resolve_source_operand(source_arg);
  if (!source_result) {
    if (source_result.error() == "2 values required") {
      return emit_link_operand_count_error();
    }
    safeErrorPrintLn("link: " + source_result.error());
    return 1;
  }

  std::string file = *source_result;
  std::string linkname = std::string(ctx.positionals[1]);

  std::wstring wfile = utf8_to_wstring(file);
  std::wstring wlinkname = utf8_to_wstring(linkname);

  BOOL result = CreateHardLinkW(wlinkname.c_str(), wfile.c_str(), nullptr);
  if (!result) {
    DWORD error = GetLastError();
    std::string error_text =
        contains_wildcard(source_arg) && file == source_arg
            ? "No such file or directory"
            : link_windows_error_text(error);
    safeErrorPrintLn("link: cannot create link '" + linkname + "' to '" + file +
                     "': " + error_text);
    return 1;
  }

  return 0;
}
