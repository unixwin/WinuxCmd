// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for yes.
/// @Version: 0.3.0
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

auto constexpr YES_OPTIONS =
    // [GNU] option
    std::array{OPTION("", "", "output a string repeatedly", STRING_TYPE)};

REGISTER_COMMAND(yes, "yes", "yes [STRING]...",
                 "Repeatedly output a line with all specified STRING(s), or y.",
                 "  yes\n"
                 "  yes please",
                 "", "WinuxCmd", "Copyright © 2026 WinuxCmd", YES_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/yes.rs, yes_cmd).
  return winux::rust2::run_command("yes", ctx.raw_args);
}
