// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

constexpr auto TIME_OPTIONS = std::array{
    OPTION("-p", "--portability",
           "use the portable output format (POSIX 1003.2 "
           "style: real, user and sys, in seconds)"),
    OPTION("", "--posix", "same as --portability"),
    OPTION("-f", "--format",
           "use FORMAT to format the report instead of the "
           "default",
           STRING_TYPE),
    OPTION("-o", "--output",
           "write the report to FILE instead of standard error", STRING_TYPE),
    OPTION("-a", "--append",
           "with -o, append to the output file instead of "
           "overwriting it"),
    OPTION("-v", "--verbose",
           "print a verbose report with all available fields"),
    OPTION("-q", "--quiet",
           "do not report abnormal termination of the command"),
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(
    time_cmd, "time", "time [OPTION]... COMMAND [ARG]...",
    "Run COMMAND, then print system resource usage.\n"
    "\n"
    "The command is executed as a child process; its exit status is returned\n"
    "unchanged.  After the command finishes, a resource report is written\n"
    "to standard error, or to the file named by -o/--output.",
    "  time true\n"
    "  time -p cmd.exe /c exit 7\n"
    "  time -o report.txt -f \"elapsed %e, status %x\" dir",
    "bash(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TIME_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/time.rs, time_cmd).
  return winux::rust2::run_command("time", ctx.raw_args);
}
