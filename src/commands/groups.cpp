// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for groups.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#include "pch/pch.h"
// include other header after pch.h

#include "core/command_macros.h"

#pragma comment(lib, "netapi32.lib")

import std;
import core;
import utils;
import container;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr GROUPS_OPTIONS =
    // [GNU]
    std::array{OPTION("", "", "print the groups a user is in", STRING_TYPE)};

REGISTER_COMMAND(groups, "groups", "groups [OPTION]... [USERNAME]...",
                 "Print a list of the groups a user is in.",
                 "  groups\n"
                 "  groups username",
                 "id(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 GROUPS_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/groups.rs); the C++ side keeps only this
  // registration. ctx.raw_args passes the operands verbatim because the GNU
  // applet parses its own argv.
  return winux::rust2::run_command("groups", ctx.raw_args);
}
