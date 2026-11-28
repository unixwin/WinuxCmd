// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pldd.
/// @Version: 0.2.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PLDD_OPTIONS = std::array{
    // [EXT]
    OPTION("-n", "--name", "print module basenames instead of paths"),
    // [GNU] -V: version shorthand (probed: `pldd -V`).
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(pldd, "pldd", "pldd [OPTION]... PID",
                 "List DLLs and executable modules loaded by a process.",
                 "  pldd 1234\n"
                 "  pldd --name 1234",
                 "lsof(1), ps(1), pgrep(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PLDD_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/pldd.rs, pldd_cmd).
  return winux::rust2::run_command("pldd", ctx.raw_args);
}
