// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for w.
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

auto constexpr W_OPTIONS = std::array{
    OPTION("-h", "--no-header", "do not print header", BOOL_TYPE),
    OPTION("-u", "--no-current", "ignore current process username",
           BOOL_TYPE),
    OPTION("-s", "--short", "short format", BOOL_TYPE),
    OPTION("-f", "--from", "show remote hostname field", BOOL_TYPE),
    OPTION("-o", "--old-style", "old style output", BOOL_TYPE),
    OPTION("-i", "--ip-addr",
           "display IP address instead of hostname (if possible)",
           BOOL_TYPE),
    OPTION("-p", "--pids", "show the PID(s) of processes in WHAT",
           BOOL_TYPE),
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(w, "w", "w [options] [user]",
                 "Show who is logged on and what they are doing.\n"
                 "\n"
                 "This command displays information about currently logged-in\n"
                 "users and their processes, similar to the Linux w command.",
                 "  w\n"
                 "  w -s\n"
                 "  w -h\n"
                 "  w username",
                 "who(1), uptime(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 W_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/w.rs, w_cmd).
  return winux::rust2::run_command("w", ctx.raw_args);
}
