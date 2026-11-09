// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: false command - registration + rust2 bridge (#1488).
/// @Version: 0.2.0
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

auto constexpr FALSE_OPTIONS =
    // [GNU]
    std::array{OPTION("--help", "", "display this help and exit", BOOL_TYPE)};

REGISTER_COMMAND(
    false_cmd,
    /* name */
    "false",

    /* synopsis */
    "false",
    "Do nothing, unsuccessfully.\n"
    "\n"
    "Exit with a status code indicating failure.\n"
    "This command is often used in shell scripts where a command is required\n"
    "to fail unconditionally.",
    "  false\n"
    "  while false; do echo 'This will never execute'; done",

    /* see also */
    "true(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", FALSE_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/truefalse.rs, false_cmd); the C++ side
  // keeps only this registration. The dispatcher still intercepts a sole
  // "--help"/"--version" (prints, then EXIT_FAILURE for false per true.c
  // with false.c's EXIT_STATUS), so the handler sees the plain failure case
  // or operands to disregard (#1196).
  return winux::rust2::run_command("false", ctx.raw_args);
}
