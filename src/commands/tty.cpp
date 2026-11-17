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
import rust2_bridge;

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

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/tty.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // The command never reads stdin: the pass-nothing run_command contract
  // keeps fd 0 untouched for later pipeline stages.
  return winux::rust2::run_command("tty", ctx.raw_args);
}
