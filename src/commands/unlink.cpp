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
    std::array{OPTION("-v", "--verbose", "print a message for each action"),
               OPTION("", "", "remove single file", STRING_TYPE)};

REGISTER_COMMAND(unlink,
                 /* name */
                 "unlink",

                 /* synopsis */
                 "unlink [OPTION]... FILE...",
                 "Remove a specified file.\n"
                 "\n"
                 "Unlink the file named FILE. If FILE is a symbolic link, the "
                 "symbolic link\n"
                 "is removed, not the file it points to.\n"
                 "\n"
                 "Options:\n"
                 "  -v, --verbose  print a message for each action",
                 "  unlink file.txt\n"
                 "  unlink -v file.txt",

                 /* see also */
                 "rm(1), link(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 UNLINK_OPTIONS) {
  namespace cp = core::pipeline;

  bool verbose =
      ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);

  if (ctx.positionals.empty()) {
    safeErrorPrintLn("unlink: missing operand");
    safePrintLn("Try 'unlink --help' for more information.");
    return 1;
  }

  bool success = true;
  for (auto file : ctx.positionals) {
    std::string filename = std::string(file);
    std::vector<std::string> expanded;
    if (contains_wildcard(filename)) {
      auto glob_result = glob_expand(filename);
      if (glob_result.expanded) {
        for (const auto& f : glob_result.files) {
          expanded.push_back(wstring_to_utf8(f));
        }
      } else {
        expanded.push_back(filename);
      }
    } else {
      expanded.push_back(filename);
    }
    for (const auto& exp : expanded) {
      std::wstring wfilename = utf8_to_wstring(exp);

      if (verbose) {
        safePrint("unlink: removing '" + exp + "'\n");
      }

      BOOL result = DeleteFileW(wfilename.c_str());
      if (!result) {
        DWORD error = GetLastError();
        safeErrorPrintLn("unlink: cannot remove '" + exp +
                         "': " + std::to_string(error));
        success = false;
      }
    }
  }

  return success ? 0 : 1;
}
