// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for groups.
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
  // (rust2/crates/winux-cmds/src/groups.rs, groups_cmd — NetUserGetGroups
  // level 0, the same membership source the 1.x pipeline used, the
  // well-known "None" group id fallback (#1024), GNU `<user> : g1 g2`
  // shape and the no-such-user wording). The C++ side keeps only this
  // registration; groups never reads fd 0, so the plain entry keeps stdin
  // live for the next pipeline stage.
  return winux::rust2::run_command("groups", ctx.raw_args);
}
