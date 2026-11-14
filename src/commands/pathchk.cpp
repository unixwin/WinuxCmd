// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for pathchk command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr PATHCHK_OPTIONS = std::array{
    // [GNU] -p: check for most POSIX systems (no long form in GNU)
    OPTION("-p", "", "check for most POSIX systems"),
    // [GNU] -P: check for empty names and leading dash (no long form in GNU)
    OPTION("-P", "", "check for empty names and leading dash"),
    // [GNU] --portability: equivalent to -p -P combined (no short form in GNU)
    OPTION("", "--portability",
           "check for all POSIX systems (equivalent to -p -P)")};

REGISTER_COMMAND(
    pathchk,
    /* cmd_name */ "pathchk",
    /* cmd_synopsis */ "pathchk [OPTION] NAME...",
    /* cmd_desc */
    "Check whether file names are valid or portable.\n"
    "Check that each file name is valid for the current filesystem,\n"
    "and optionally check for POSIX portability.",
    /* examples */
    "  pathchk myfile.txt\n"
    "  pathchk -p /tmp/test\n"
    "  pathchk -P -p filename",
    /* see_also */ "ln, mkdir",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ PATHCHK_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/pathchk.rs); the C++ side keeps only this
  // registration. ctx.raw_args passes the operands verbatim because the GNU
  // applet parses its own argv.
  return winux::rust2::run_command("pathchk", ctx.raw_args);
}
