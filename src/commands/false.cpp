// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for false command.
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
  // [GNU] false ignores every operand: no getopt is run and "--help"/
  // "--version" are honored only as the sole argument (intercepted by
  // the dispatcher, which prints them but returns EXIT_FAILURE for
  // false, per true.c with false.c's EXIT_STATUS), so this handler
  // only ever runs for the plain failure case or with operands to
  // disregard (WinuxCmd #1196).
  (void)ctx;
  return 1;
}
