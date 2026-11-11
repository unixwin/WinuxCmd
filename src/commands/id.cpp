// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for id.
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

auto constexpr ID_OPTIONS = std::array{
    OPTION("-a", "", "ignore, for compatibility with other versions",
           BOOL_TYPE),
    OPTION("-g", "--group", "print only the effective group ID", BOOL_TYPE),
    OPTION("-G", "--groups", "print all group IDs", BOOL_TYPE),
    OPTION("-n", "--name", "print a name instead of a number", BOOL_TYPE),
    OPTION("-r", "--real", "print the real ID instead of the effective ID",
           BOOL_TYPE),
    OPTION("-u", "--user", "print only the effective user ID", BOOL_TYPE),
    OPTION("-Z", "--context",
           "print only the security context (not implemented)", BOOL_TYPE),
    OPTION("", "--zero", "delimit entries with NUL, not whitespace", BOOL_TYPE),
    // [GNU]
    OPTION("-z", "", "delimit entries with NUL, not whitespace", BOOL_TYPE)};

REGISTER_COMMAND(
    id, "id", "id [OPTION]... [USER]",
    "Print user and group information for the specified USER,\n"
    "or (when USER omitted) for the current user.\n"
    "\n"
    "Note: This is a Windows implementation. Windows doesn't have\n"
    "POSIX UIDs/GIDs, so this command provides limited functionality.\n"
    "It mainly displays the username.",
    "  id\n"
    "  id -u\n"
    "  id -g\n"
    "  id -G",
    "groups(1), whoami(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    ID_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/id.rs, id_cmd); the C++ side keeps only
  // this registration — the OpenProcessToken/LookupAccountSidW identity
  // pipeline is deleted. ctx.raw_args passes the operands verbatim
  // because coreutils id.c parses its own option run (only-name/real-ID
  // conflicts, USER operand resolution).
  return winux::rust2::run_command("id", ctx.raw_args);
}
