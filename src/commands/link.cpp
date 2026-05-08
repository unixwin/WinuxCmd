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
    std::array{OPTION("-v", "--verbose", "print a message for each action"),
               OPTION("", "", "create link to file", STRING_TYPE)};

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
    "Options:\n"
    "  -v, --verbose  print a message for each action",
    "  link file.txt link_to_file.txt\n"
    "  link -v file.txt link_to_file.txt",

    /* see also */
    "ln(1), unlink(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", LINK_OPTIONS) {
  namespace cp = core::pipeline;

  bool verbose =
      ctx.get<bool>("--verbose", false) || ctx.get<bool>("-v", false);

  if (ctx.positionals.size() != 2) {
    safeErrorPrintLn("link: missing file operand");
    safePrintLn("Try 'link --help' for more information.");
    return 1;
  }

  std::string file = std::string(ctx.positionals[0]);
  std::string linkname = std::string(ctx.positionals[1]);

  std::wstring wfile = utf8_to_wstring(file);
  std::wstring wlinkname = utf8_to_wstring(linkname);

  if (verbose) {
    safePrint("link: '" + linkname + "' -> '" + file + "'\n");
  }

  BOOL result = CreateHardLinkW(wlinkname.c_str(), wfile.c_str(), nullptr);
  if (!result) {
    DWORD error = GetLastError();
    safeErrorPrintLn("link: cannot create link '" + linkname +
                     "': " + std::to_string(error));
    return 1;
  }

  return 0;
}
