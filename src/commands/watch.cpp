// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for watch (procps 4.0.4).
/// @Version: 0.3.0
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

auto constexpr WATCH_OPTIONS = std::array{
    OPTION("-b", "--beep", "beep if command has a non-zero exit"),
    OPTION("-c", "--color", "interpret ANSI color sequences"),
    OPTION("-C", "--no-color", "do not print ANSI color escape sequences"),
    OPTION("-d", "--differences", "highlight changes between updates",
           OPTIONAL_STRING_TYPE),
    OPTION("-e", "--errexit", "exit if command has a non-zero exit"),
    OPTION("-g", "--chgexit", "exit when the output of command changes"),
    OPTION("-n", "--interval", "seconds to wait between updates", STRING_TYPE),
    OPTION("-p", "--precise", "run command at precise intervals"),
    OPTION("-q", "--equexit",
           "exit when the output does not change for the given number "
           "of cycles",
           STRING_TYPE),
    OPTION("-r", "--no-rerun", "do not rerun the command on a terminal resize"),
    OPTION("-t", "--no-title", "turn off header showing interval and command"),
    OPTION("-w", "--no-wrap", "turn off line wrapping"),
    OPTION("-x", "--exec",
           "execute the command directly instead of via the shell"),
    OPTION("", "--count", "number of times to run the command", INT_TYPE)};

REGISTER_COMMAND(
    watch, "watch", "watch [OPTION]... COMMAND",
    "Execute a program periodically, showing output fullscreen.\n"
    "\n"
    "Runs COMMAND repeatedly, displaying its output and errors.\n"
    "This allows you to watch the program output change over time.",
    "  watch -n 1 'ls -l'\n"
    "  watch -d ls -l\n"
    "  watch -n 5 date",
    "ps(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", WATCH_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/watch.rs, watch_cmd).
  return winux::rust2::run_command("watch", ctx.raw_args);
}
