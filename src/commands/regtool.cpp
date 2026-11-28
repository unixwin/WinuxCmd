// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Small Windows-native registry tool.
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

auto constexpr REGTOOL_OPTIONS =
    // [EXT]
    std::array{OPTION("", "", "operate on the Windows registry", STRING_TYPE)};

REGISTER_COMMAND(regtool, "regtool", "regtool ACTION KEY [VALUE] [DATA]",
                 "Small Windows-native registry tool.",
                 "  regtool list HKCU/Software\n"
                 "  regtool get HKCU/Software/App Name\n"
                 "  regtool set HKCU/Software/App Name Value\n"
                 "  regtool remove HKCU/Software/App Name",
                 "reg.exe(1), hostid(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", REGTOOL_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/regtool.rs, regtool_cmd).
  return winux::rust2::run_command("regtool", ctx.raw_args);
}
