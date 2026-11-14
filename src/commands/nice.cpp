// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for nice command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr NICE_OPTIONS =
    // [DIFFERS]
    std::array{OPTION("-n", "--adjustment", "adjust increment", STRING_TYPE)};
REGISTER_COMMAND(
    nice,
    /* cmd_name */ "nice",
    /* cmd_synopsis */ "nice [OPTION] [COMMAND [ARG]...]",
    /* cmd_desc */
    "Run a program with modified scheduling priority.\n"
    "Run COMMAND with an adjusted niceness, which affects process scheduling.\n"
    "With no COMMAND, print the current niceness. Niceness values range from\n"
    "-20 (most favorable) to 19 (least favorable). Default is 10.",
    /* examples */
    "  nice -n 5 tar -cf archive.tar /home\n"
    "  nice -n -10 make\n"
    "  nice",
    /* see_also */ "renice, nohup",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ NICE_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/nice.rs, nice_cmd); the C++ side keeps only
  // this registration — the legacy -N rewrite arrives through the
  // dispatcher's rewrite hook (ctx.raw_args), and the CREATE_SUSPENDED
  // priority-class child spawn moves to Rust. nice never reads stdin: the
  // pass-nothing run_command contract keeps fd 0 untouched so the wrapped
  // command inherits the live handle (same discipline as timeout).
  return winux::rust2::run_command("nice", ctx.raw_args);
}
