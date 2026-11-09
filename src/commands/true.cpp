// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: true command - registration + rust2 bridge (#1488).
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
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/truefalse.rs, true_cmd); the C++ side keeps
  // only this registration. The dispatcher still intercepts a sole
  // "--help"/"--version" (product-branded text shared by every applet), so
  // the handler sees the plain case or operands to disregard (#1196).
  return winux::rust2::run_command("true", ctx.raw_args);
}
