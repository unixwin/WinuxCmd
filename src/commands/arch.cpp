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
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // The command never reads stdin: the pass-nothing run_command contract
  // keeps fd 0 untouched for later pipeline stages.
  return winux::rust2::run_command("arch", ctx.raw_args);
}
