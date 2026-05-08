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
 *  - File: reset.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for reset command.
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

auto constexpr RESET_OPTIONS =
    std::array{OPTION("-I", "--initialize",
                      "reset terminal to its default mode", BOOL_TYPE),
               OPTION("-q", "--quiet", "be quiet", BOOL_TYPE)};

REGISTER_COMMAND(
    reset,
    /* name */
    "reset",

    /* synopsis */
    "reset [OPTION]...",
    "Reset terminal to its default state.\n"
    "\n"
    "This command resets the terminal to a sane state.\n"
    "On Windows, this sends ANSI escape codes to reset the terminal.\n"
    "\n"
    "Options:\n"
    "  -I, --initialize  reset terminal to its default mode\n"
    "  -q, --quiet       be quiet",
    "  reset\n"
    "  reset -q",

    /* see also */
    "tput(1), clear(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    RESET_OPTIONS) {
  namespace cp = core::pipeline;

  bool quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);
  bool initialize =
      ctx.get<bool>("--initialize", false) || ctx.get<bool>("-I", false);

  if (!quiet) {
    // Send ANSI escape codes to reset terminal
    safePrint("\033c");  // Reset terminal
  }

  if (initialize) {
    // Send additional initialization codes
    safePrint("\033[?25h");  // Show cursor
    safePrint("\033[0m");    // Reset all attributes
  }

  return 0;
}
