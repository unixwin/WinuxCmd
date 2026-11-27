// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for col.
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

auto constexpr COL_OPTIONS = std::array{
    OPTION("-b", "--no-backspaces", "do not output backspaces", BOOL_TYPE),
    OPTION("-f", "--fine", "permit forward half line feeds", BOOL_TYPE),
    OPTION("-p", "--pass", "pass unknown control sequences", BOOL_TYPE),
    OPTION("-h", "--tabs", "convert spaces to tabs", BOOL_TYPE),
    OPTION("-x", "--spaces", "convert tabs to spaces", BOOL_TYPE),
    OPTION("-l", "--lines", "buffer at least NUM lines", STRING_TYPE),
    OPTION("-V", "--version", "output version information and exit",
           BOOL_TYPE),
    OPTION("-H", "--help", "display this help and exit", BOOL_TYPE)};

REGISTER_COMMAND(
    col, "col", "col [OPTION]...",
    "Filter reverse line feeds from standard input.\n"
    "\n"
    "col filters out reverse (and half-reverse) line feeds so that the\n"
    "output is in the correct order with only forward and half-forward\n"
    "line feeds.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options\n"
    "too.\n"
    "\n"
    "  -b, --no-backspaces    do not output backspaces\n"
    "  -f, --fine             permit forward half line feeds\n"
    "  -p, --pass             pass unknown control sequences\n"
    "  -h, --tabs             convert spaces to tabs (the default)\n"
    "  -x, --spaces           convert tabs to spaces\n"
    "  -l, --lines NUM        buffer at least NUM lines (default 32)",
    "  col -b < out.txt    strip backspaces from nroff output\n"
    "  col -x              output spaces instead of tabs",
    "expand(1), unexpand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    COL_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/col.rs, col_cmd).
  return winux::rust2::run_command_with_stdin("col", ctx.raw_args);
}
