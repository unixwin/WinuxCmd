// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for true command.
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

auto constexpr TRUE_OPTIONS =
    // [GNU] option
    std::array{OPTION("--help", "", "display this help and exit", BOOL_TYPE)};

REGISTER_COMMAND(
    true_cmd,
    /* name */
    "true",

    /* synopsis */
    "true",
    "Do nothing, successfully.\n"
    "\n"
    "Exit with a status code indicating success.\n"
    "This command is often used in shell scripts where a command is required\n"
    "but no action is needed.",
    "  true\n"
    "  while true; do echo 'Press Ctrl+C to stop'; sleep 1; done",

    /* see also */
    "false(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TRUE_OPTIONS) {
  // [GNU] true ignores every operand: no getopt is run and "--help"/
  // "--version" are honored only as the sole argument (intercepted by the
  // dispatcher), so this handler only ever runs for the plain no-op case
  // or with operands to disregard (WinuxCmd #1196).
  (void)ctx;
  return 0;
}
