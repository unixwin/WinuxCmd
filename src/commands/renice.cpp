// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for renice.
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

auto constexpr RENICE_OPTIONS =
    // [EXT]
    std::array{
        OPTION("-n", "--priority", "set niceness value", STRING_TYPE),
        // [EXT]
        OPTION("-p", "--pid", "interpret operands as process IDs"),
        // [DIFFERS] GNU renice: process group; not supported on Windows
        OPTION("-g", "--pgrp", "interpret operands as process group IDs"),
        // [DIFFERS] GNU renice: user-based renice; not supported on Windows
        OPTION("-u", "--user", "interpret operands as usernames or UIDs"),
        // [GNU] -V: version shorthand (probed: `renice -V`)
        OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(
    renice, "renice",
    "renice [-n] PRIORITY [-p] PID...  # also: -g PGRP, -u USER [DIFFERS]",
    "Alter the scheduling priority of running processes.\n"
    "Windows priority classes are mapped to Unix-like niceness "
    "values from -20 to 19.",
    "  renice 10 -p 1234\n"
    "  renice -n 0 1234",
    "nice(1), ps(1), pgrep(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    RENICE_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/renice.rs, renice_cmd).
  return winux::rust2::run_command("renice", ctx.raw_args);
}
