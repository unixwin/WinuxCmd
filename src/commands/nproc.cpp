/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: nproc.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for nproc command.
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

auto constexpr NPROC_OPTIONS =
    std::array{OPTION("", "--all", "print number of all installed processors"),
               OPTION("", "--ignore", "ignore N processors", STRING_TYPE),
               OPTION("", "", "print number of processing units", STRING_TYPE)};

REGISTER_COMMAND(
    nproc_cmd,
    /* name */
    "nproc",

    /* synopsis */
    "nproc [OPTION]...",
    "Print the number of processing units available.\n"
    "\n"
    "This is useful for scripts that need to know how many parallel\n"
    "processes can be started.",
    "  nproc\n"
    "  nproc --all\n"
    "  nproc --ignore 1",

    /* see also */
    "sysconf(3)", "WinuxCmd", "Copyright © 2026 WinuxCmd", NPROC_OPTIONS) {
  SYSTEM_INFO sysInfo;
  GetSystemInfo(&sysInfo);

  DWORD numProcessors = sysInfo.dwNumberOfProcessors;

  if (ctx.get<bool>("--all", false)) {
    // On Windows, all processors are always available
    safePrintLn(std::to_string(numProcessors));
  } else {
    // Number of available processors
    int ignore = ctx.get<int>("--ignore", 0);
    DWORD available = numProcessors;
    if (static_cast<DWORD>(ignore) < available) {
      available -= ignore;
    } else {
      available = 1;
    }
    safePrintLn(std::to_string(available));
  }

  return 0;
}
