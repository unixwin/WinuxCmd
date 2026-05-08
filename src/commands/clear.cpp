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
 *  - File: clear.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for clear command.
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

auto constexpr CLEAR_OPTIONS =
    std::array{OPTION("", "", "clear terminal screen", STRING_TYPE)};

REGISTER_COMMAND(
    clear,
    /* name */
    "clear",

    /* synopsis */
    "clear",
    "Clear the terminal screen.\n"
    "\n"
    "Clears the terminal screen by sending ANSI escape sequences.\n"
    "This works on Windows Terminal, modern terminals, and terminals that "
    "support ANSI escape codes.",
    "  clear",

    /* see also */
    "reset(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CLEAR_OPTIONS) {
  namespace cp = core::pipeline;

  // Send ANSI escape sequence to clear screen and move cursor to home
  // \033[2J - Clear entire screen
  // \033[H  - Move cursor to home position (top-left)
  safePrint("\033[2J\033[H");

  return 0;
}
