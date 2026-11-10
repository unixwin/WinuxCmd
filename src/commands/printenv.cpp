// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for printenv command.
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

auto constexpr PRINTENV_OPTIONS = std::array{
    // [GNU]
    OPTION("-0", "--null", "end each output line with NUL, not newline")};

REGISTER_COMMAND(
    printenv,
    /* name */
    "printenv",

    /* synopsis */
    "printenv [OPTION]... [VARIABLE]...",
    "Print all or part of environment.\n"
    "\n"
    "If no VARIABLE is specified, print all environment variables.\n"
    "If one or more VARIABLE names are specified, print the values of those\n"
    "variables only.\n"
    "\n"
    "Options:\n"
    "  -0, --null    end each output line with NUL, not newline",
    "  printenv\n"
    "  printenv PATH\n"
    "  printenv HOME USER\n"
    "  printenv -0 | xargs -0 ...",

    /* see also */
    "env(1), set(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    PRINTENV_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/printenv.rs, printenv_cmd); the C++ side
  // keeps only this registration. ctx.raw_args passes the operands
  // verbatim because GNU printenv.c parses its own getopt("+0") run and
  // the leading '+' stops at the first NAME — the Rust library owns the
  // same boundary. Note env VALUES reach stdout through the bridge's raw
  // (non-translating) writers: a variable's bytes are data, never catalog
  // text.
  return winux::rust2::run_command("printenv", ctx.raw_args);
}
