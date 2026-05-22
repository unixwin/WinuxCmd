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
 *  - File: tty.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for tty command.
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

auto constexpr TTY_OPTIONS =
    std::array{OPTION("-s", "--silent",
                      "print nothing, just return exit status", BOOL_TYPE),
               OPTION("", "--quiet", "same as --silent", BOOL_TYPE)};

REGISTER_COMMAND(
    tty,
    /* name */
    "tty",

    /* synopsis */
    "tty [OPTION]...",
    "Print the file name of the terminal connected to standard input.\n"
    "\n"
    "If standard input is not a terminal, print \"not a tty\" and exit with\n"
    "non-zero status.\n"
    "\n"
    "Options:\n"
    "  -s, --silent, --quiet    print nothing, just return exit status",
    "  tty\n"
    "  tty -s  # silent mode, only check exit status",

    /* see also */
    "isatty(3)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TTY_OPTIONS) {
  namespace cp = core::pipeline;

  if (!ctx.positionals.empty()) {
    safeErrorPrintLn("tty: extra operand");
    safePrintLn("Try 'tty --help' for more information.");
    return 2;
  }

  bool silent = ctx.get<bool>("--silent", false) ||
                ctx.get<bool>("--quiet", false) || ctx.get<bool>("-s", false);
  HANDLE hStdIn = GetStdHandle(STD_INPUT_HANDLE);

  DWORD mode = 0;
  bool is_console = GetConsoleMode(hStdIn, &mode) != 0;

  if (silent) {
    return is_console ? 0 : 1;
  }

  if (is_console) {
    safePrintLn("con");
    return 0;
  } else {
    safePrintLn("not a tty");
    return 1;
  }
}
