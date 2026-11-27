// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for uptime.
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

auto constexpr UPTIME_OPTIONS = std::array{
    // [EXT] option
    OPTION("-p", "--pretty", "show uptime in pretty format", BOOL_TYPE),
    // [EXT] option
    OPTION("-s", "--since", "system up since", BOOL_TYPE),
    // [GNU] -V: version shorthand (probed: `uptime -V`).
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(
    uptime, "uptime", "uptime [OPTION]...",
    "Tell how long the system has been running.\n"
    "\n"
    "Note: This implementation provides Windows-specific information.\n"
    "Load average is not available on Windows.",
    "  uptime\n"
    "  uptime -p\n"
    "  uptime -s",
    "w(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", UPTIME_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/uptime.rs, uptime_cmd).
  return winux::rust2::run_command("uptime", ctx.raw_args);
}
