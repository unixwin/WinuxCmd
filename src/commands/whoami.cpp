// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for whoami.
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

auto constexpr WHOAMI_OPTIONS = std::array{
    // [GNU] option
    OPTION("", "", "print effective userid", STRING_TYPE)
    // whoami has no standard options
    // -a, --all (not implemented - print all information)
    // -b, --boot (not implemented - print system boot time)
    // -d, --dead (not implemented - print dead processes)
    // -H, --heading (not implemented - print line of column headings)
    // -l, --login (not implemented - print login name)
    // -p, --process (not implemented - print process information)
    // -r, --runlevel (not implemented - print runlevel)
    // -t, --time (not implemented - print last system clock change)
    // -u, --users (not implemented - print logged in users)
};

REGISTER_COMMAND(
    whoami, "whoami", "whoami [OPTION]",
    "Print effective userid.\n"
    "\n"
    "Print the user name associated with the current effective user ID.\n"
    "Same as id -un.\n"
    "\n"
    "Note: This implementation only supports basic username printing.\n"
    "Advanced features are not implemented.",
    "  whoami\n"
    "  id -un",
    "id(1), logname(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    WHOAMI_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/whoami.rs, whoami_cmd); the C++ side keeps
  // only this registration. ctx.raw_args passes the operands verbatim
  // because GNU whoami.c parses its own argv: any operand is
  // "extra operand" rc 1 and the user name itself is platform data.
  return winux::rust2::run_command("whoami", ctx.raw_args);
}
