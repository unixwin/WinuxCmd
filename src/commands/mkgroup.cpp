// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for mkgroup.
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

auto constexpr MKGROUP_OPTIONS = std::array{
    // [EXT]
    OPTION("-c", "--current", "print groups from the current process token"),
    // [EXT]
    OPTION("-l", "--local", "enumerate local machine groups (default)")};

REGISTER_COMMAND(mkgroup, "mkgroup", "mkgroup [OPTION]...",
                 "Generate group-like entries from Windows groups.",
                 "  mkgroup --current\n"
                 "  mkgroup --local",
                 "groups(1), id(1), mkpasswd(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", MKGROUP_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/mkpasswd.rs, mkgroup_cmd).
  return winux::rust2::run_command("mkgroup", ctx.raw_args);
}
