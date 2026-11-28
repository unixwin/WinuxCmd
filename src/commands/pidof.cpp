// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pidof.
/// @Version: 0.2.0
/// @License: MIT

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PIDOF_OPTIONS =
    // [EXT]
    std::array{OPTION("-x", "--exact", "exact match only")};

REGISTER_COMMAND(pidof, "pidof", "pidof [OPTION]... PROGRAM...",
                 "Find the process ID of a running program.",
                 "  pidof explorer\n"
                 "  pidof -x powershell",
                 "pgrep(1), ps(1), kill(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PIDOF_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/pidof.rs, pidof_cmd).
  return winux::rust2::run_command("pidof", ctx.raw_args);
}
