// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for join.
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

auto constexpr JOIN_OPTIONS = std::array{
    // [GNU]
    OPTION("-1", "", "join on this FIELD of file 1", STRING_TYPE),
    // [GNU]
    OPTION("-2", "", "join on this FIELD of file 2", STRING_TYPE),
    // [GNU]
    OPTION("-t", "", "use CHAR as input and output field separator",
           STRING_TYPE),
    // [GNU]
    // [GNU] -e has no long form in GNU join; "--empty" is invented (#1411).
    OPTION("-e", "", "replace missing input fields with EMPTY", STRING_TYPE),
    // [GNU]
    // [GNU] -o has no long form in GNU join; "--output" is invented (#1355).
    OPTION("-o", "", "use specified output format", STRING_TYPE),
    // [GNU]
    OPTION("-j", "", "equivalent to -1 FIELD -2 FIELD", STRING_TYPE),
    // [GNU]
    OPTION("-a", "", "print unpairable lines from file 1 or 2", STRING_TYPE),
    // [GNU]
    OPTION("-v", "", "print only unpairable lines from file 1 or 2",
           STRING_TYPE),
    // [GNU]
    OPTION("-i", "--ignore-case", "ignore differences in case", BOOL_TYPE),
    // [GNU]
    OPTION("", "--check-order", "check that the input is correctly sorted",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--nocheck-order",
           "do not check that the input is correctly sorted", BOOL_TYPE),
    // [GNU]
    OPTION("", "--header", "treat the first line in each file as headers",
           BOOL_TYPE),
    // [GNU]
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline",
           BOOL_TYPE)};

REGISTER_COMMAND(
    join, "join", "join [OPTION]... FILE1 FILE2",
    "For each pair of input lines with identical join fields, write a line to\n"
    "standard output. The default join field is the first, delimited by "
    "blanks.\n"
    "\n"
    "Supports GNU-compatible blank-delimited fields, custom output lists,\n"
    "unpairable-line output, and case-insensitive joins.",
    "  join file1 file2\n"
    "  join -j 1 file1 file2        # join on first field\n"
    "  join -t ',' file1 file2     # use comma as separator\n"
    "  join -1 2 -2 1 file1 file2  # join on field 2 of file1 and field 1 of "
    "file2",
    "comm(1), paste(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    JOIN_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/join.rs, join_cmd); the C++ side keeps
  // only this registration — the field splitter, -o format compiler,
  // unpairable-line walkers and the operand/file plumbing are deleted.
  // stdin is forwarded by the bridge (kRust2StdinCommands declares join),
  // covering the "-" operand and FILE1/FILE2 defaults. The known 1.x
  // divergences the rust2 side re-derives from GNU 9.4 (sort-check
  // diagnostics, -t multi-char rejection, --header auto-disable of the
  // check) are not inherited.
  return winux::rust2::run_command("join", ctx.raw_args);
}
