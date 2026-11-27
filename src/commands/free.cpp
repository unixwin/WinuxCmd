// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for free.
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

auto constexpr FREE_OPTIONS = std::array{
    // [GNU]
    OPTION("-b", "--bytes", "display amount of memory in bytes", BOOL_TYPE),
    // [GNU] -k/--kibi: show output in kibibytes (1024-based)
    OPTION("-k", "--kibi", "display amount of memory in kibibytes", BOOL_TYPE),
    // [GNU] -m/--mebi: show output in mebibytes (1024-based)
    OPTION("-m", "--mebi", "display amount of memory in mebibytes", BOOL_TYPE),
    // [GNU] -g/--gibi: show output in gibibytes (1024-based)
    OPTION("-g", "--gibi", "display amount of memory in gibibytes", BOOL_TYPE),
    // [GNU]
    OPTION("-h", "--human", "show human-readable output", BOOL_TYPE),
    // [GNU]
    OPTION("-l", "--lohi", "show detailed low and high memory statistics",
           BOOL_TYPE),
    // [GNU]
    OPTION("-t", "--total", "display a line showing the totals", BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--seconds",
           "continuously display memory statistics with delay", INT_TYPE),
    // [GNU]
    OPTION("-c", "--count", "repeat the display COUNT times", INT_TYPE),
    // [GNU]
    OPTION("-w", "--wide", "wide output", BOOL_TYPE),
    // [GNU] --kilo: show output in kilobytes (1000-based SI)
    OPTION("", "--kilo", "display amount of memory in kilobytes (SI)",
           BOOL_TYPE),
    // [GNU] --mega: show output in megabytes (1000-based SI)
    OPTION("", "--mega", "display amount of memory in megabytes (SI)",
           BOOL_TYPE),
    // [GNU] --giga: show output in gigabytes (1000-based SI)
    OPTION("", "--giga", "display amount of memory in gigabytes (SI)",
           BOOL_TYPE),
    // [GNU] --tera: show output in terabytes (1000-based SI)
    OPTION("", "--tera", "display amount of memory in terabytes (SI)",
           BOOL_TYPE),
    // [GNU] --peta: show output in petabytes (1000-based SI)
    OPTION("", "--peta", "display amount of memory in petabytes (SI)",
           BOOL_TYPE),
    // [GNU] --tebi: show output in tebibytes (1024-based)
    OPTION("", "--tebi", "display amount of memory in tebibytes", BOOL_TYPE),
    // [GNU] --pebi: show output in pebibytes (1024-based)
    OPTION("", "--pebi", "display amount of memory in pebibytes", BOOL_TYPE),
    // [GNU] --si: use powers of 1000 not 1024
    OPTION("", "--si", "use powers of 1000 not 1024", BOOL_TYPE),
    // [GNU] -L/--line: show output on a single line
    OPTION("-L", "--line", "show output on a single line", BOOL_TYPE),
    // [GNU] -v/--committed: show committed memory and commit limit
    OPTION("-v", "--committed", "show committed memory and commit limit",
           BOOL_TYPE),
    // [GNU] -V: version shorthand (probed: `free -V`).
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(free, "free", "free [OPTION]",
                 "Display amount of free and used memory in the system.\n"
                 "\n"
                 "This is a Windows implementation of the Linux free command.\n"
                 "The memory statistics are approximated from Windows APIs.",
                 "  free\n"
                 "  free -h\n"
                 "  free -m -t\n"
                 "  free -g",
                 "vmstat(1), top(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FREE_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/free.rs, free_cmd).
  return winux::rust2::run_command("free", ctx.raw_args);
}
