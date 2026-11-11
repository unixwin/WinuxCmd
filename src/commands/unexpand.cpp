// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for unexpand.
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

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr UNEXPAND_OPTIONS = std::array{
    // [GNU] option
    OPTION("-t", "--tabs", "specify tab stop positions (default: 8)",
           STRING_TYPE),
    // [GNU] option
    OPTION("-a", "--all", "convert all spaces, not just leading ones",
           BOOL_TYPE),
    // [GNU] --first-only has NO short option in GNU unexpand
    // (unexpand.c longopts); the old -f alias accepted what GNU
    // rejects as an invalid option (audit #1462).
    OPTION("", "--first-only", "convert only leading sequences of blanks",
           BOOL_TYPE),
    // [EXT] option
    OPTION("-U", "--no-utf8",
           "interpret input file as 8-bit ASCII rather than UTF-8", BOOL_TYPE),
    // [GNU] obsolescent -NUM is an alias for --tabs=NUM.
    // #1519: tab stops are uintmax quantities, so -NUM uses the
    // parser's unsigned type.
    OPTION("-NUM", "", "same as --tabs=NUM", UINT_TYPE)};
REGISTER_COMMAND(
    unexpand, "unexpand", "unexpand [OPTION]... [FILE]...",
    "Convert spaces to tabs.\n"
    "\n"
    "Convert spaces to tabs. By default, only convert leading spaces to tabs.\n"
    "Use -a to convert all spaces.\n"
    "\n"
    "Supports GNU tab lists, including /N and +N repeat tab stops.",
    "  unexpand file.txt\n"
    "  unexpand -t 4 file.txt\n"
    "  unexpand -a file.txt\n"
    "  echo 'hello world' | unexpand",
    "expand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", UNEXPAND_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/unexpand.rs, unexpand_cmd); the C++ side
  // keeps only this registration -- the column/tab-stop back-converter is
  // deleted. stdin is forwarded by the bridge (kRust2StdinCommands
  // declares unexpand). GNU -t position lists and the leading-blank
  // model are re-derived from GNU 9.4 by the library.
  return winux::rust2::run_command("unexpand", ctx.raw_args);
}
