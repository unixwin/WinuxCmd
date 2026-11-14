// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for arch.
/// @Version: 0.1.0
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

auto constexpr ARCH_OPTIONS =
    // [GNU]
    std::array{OPTION("", "", "print machine hardware name", STRING_TYPE)};

REGISTER_COMMAND(
    arch, "arch", "arch",
    "Print machine architecture.\n"
    "\n"
    "Print machine hardware name.\n"
    "\n"
    "Note: This implementation displays the Windows processor architecture.",
    "  arch", "uname -m", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    ARCH_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/arch.rs); the C++ side keeps only this
  // registration. ctx.raw_args passes the operands verbatim because the GNU
  // applet parses its own argv.
  return winux::rust2::run_command("arch", ctx.raw_args);
}
