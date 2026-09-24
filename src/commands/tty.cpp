// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU] option
    std::array{OPTION("-s", "--silent",
                      "print nothing, just return exit status", BOOL_TYPE),
               // [GNU] option
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
  clear_pipe_closed_flags();

  namespace cp = core::pipeline;

  if (!ctx.positionals.empty()) {
    safeErrorPrintLn("tty: extra operand '" +
                     std::string(ctx.positionals.front()) + "'");
    safeErrorPrintLn("Try 'tty --help' for more information.");
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
  } else {
    safePrintLn("not a tty");
  }

  if (is_stdout_pipe_closed()) {
    return 3;
  }

  return is_console ? 0 : 1;
}
