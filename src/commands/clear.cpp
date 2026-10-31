// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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

auto constexpr CLEAR_OPTIONS = std::array{
    // [GNU] -x: do not try to clear the scrollback (the E3 sequence)
    OPTION("-x", "", "do not clear scrollback", BOOL_TYPE),
    // [EXT]
    OPTION("-V", "", "print curses-version", BOOL_TYPE),
    // [EXT]
    OPTION("", "", "clear terminal screen", STRING_TYPE)};

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

  if (ctx.get<bool>("-V", false)) {
    safePrintLn("ncurses 6.6.20251230");
    return 0;
  }

  // Send ANSI escape sequence to clear screen and move cursor to home
  // \033[2J - Clear entire screen
  // \033[H  - Move cursor to home position (top-left)
  // \033[3J - Clear scrollback (the terminfo E3 extension); GNU clear
  // omits it under -x (ncurses clear.c: "-x do not try to clear
  // scrollback").
  safePrint("\033[H\033[2J");
  if (!ctx.get<bool>("-x", false)) {
    safePrint("\033[3J");
  }

  return 0;
}
