// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sleep.
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

auto constexpr SLEEP_OPTIONS =
    std::array{// [GNU] NUMBER: pause for seconds
               OPTION("", "", "pause for NUMBER seconds", STRING_TYPE)};

REGISTER_COMMAND(
    sleep, "sleep", "sleep NUMBER[SUFFIX]...",
    "Pause for NUMBER seconds.\n"
    "\n"
    "SUFFIX may be 's' for seconds (the default), 'm' for minutes,\n"
    "'h' for hours or 'd' for days.\n"
    "\n"
    "Unlike most implementations that require NUMBER be an integer,\n"
    "here NUMBER may be an arbitrary floating point number.\n"
    "\n"
    "Note: This implementation supports floating point numbers.",
    "  sleep 1\n"
    "  sleep 2.5\n"
    "  sleep 1m 30s\n"
    "  sleep 0.5h",
    "pause(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SLEEP_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/sleep.rs, sleep_cmd); the C++ side keeps
  // only this registration — the strtod parse / Sleep() wait pipeline is
  // deleted. ctx.raw_args passes the operands verbatim because sleep.c
  // parses its own argv (`sleep --` still has an argument, argc == 1 test).
  return winux::rust2::run_command("sleep", ctx.raw_args);
}
