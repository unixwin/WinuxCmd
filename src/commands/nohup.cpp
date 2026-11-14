// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for nohup command.
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

auto constexpr NOHUP_OPTIONS =
    // [GNU]
    std::array{OPTION("", "", "command to run", STRING_TYPE)};

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    nohup,
    /* cmd_name */ "nohup",
    /* cmd_synopsis */ "nohup COMMAND [ARG]...",
    /* cmd_desc */
    "Run a command immune to hangups.\n"
    "Run COMMAND, ignoring hangup signals. If standard output is a terminal,\n"
    "append output to 'nohup.out'. If standard error is a terminal, redirect\n"
    "it to standard output. To save output to FILE, use 'nohup COMMAND > "
    "FILE'.",
    /* examples */
    "  nohup make &\n"
    "  nohup ./myscript.sh > myscript.log 2>&1 &\n"
    "  nohup python long_running_task.py",
    /* see_also */ "nice, disown",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ NOHUP_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/nohup.rs). The C++ _isatty/STARTUPINFO
  // pipeline is deleted; the rust2 driver injects the console facts as
  // StdioMeta (the tty/test lane's channel), so the terminal faces keep
  // working under a real console while the ABI surface passes pipe
  // metadata — the same honesty rule the rust2 entry applies.
  // ctx.raw_args hands the argv tail verbatim: GNU nohup parses only the
  // leading option run (optstring "+"), which the rust2 body reproduces.
  return winux::rust2::run_command("nohup", ctx.raw_args);
}
