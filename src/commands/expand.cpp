// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for expand.
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

auto constexpr EXPAND_OPTIONS =
    std::array{// [GNU]
               OPTION("-t", "--tabs", "specify tab stop positions (default: 8)",
                      STRING_TYPE),
               // [GNU]
               OPTION("-i", "--initial",
                      "only convert tabs at the beginning of lines", BOOL_TYPE),
               // [GNU] obsolescent -NUM is an alias for --tabs=NUM.
               // #1519: tab stops are uintmax quantities, so -NUM uses
               // the parser's unsigned type.
               OPTION("-NUM", "", "same as --tabs=NUM", UINT_TYPE)};
REGISTER_COMMAND(
    expand, "expand", "expand [OPTION]... [FILE]...",
    "Convert tabs to spaces.\n"
    "\n"
    "Convert each tab character to one or more spaces.\n"
    "By default, tabs are converted to 8 spaces.\n"
    "\n"
    "Supports GNU tab lists, including /N and +N repeat tab stops.",
    "  expand file.txt\n"
    "  expand -t 4 file.txt\n"
    "  expand -i file.txt\n"
    "  echo -e 'hello\tworld' | expand",
    "unexpand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", EXPAND_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/expand.rs, expand_cmd); the C++ side
  // keeps only this registration -- the tab-stop list compiler and the
  // column walker are deleted. stdin is forwarded by the bridge
  // (kRust2StdinCommands declares expand). Obsolescent -NUM spelling
  // and GNU -t position lists are re-derived from GNU 9.4 by the
  // library.
  return winux::rust2::run_command("expand", ctx.raw_args);
}
