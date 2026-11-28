// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pkill (procps 4.0.4 semantics).
/// @Version: 0.3.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PKILL_OPTIONS = std::array{
    OPTION("-A", "--ignore-ancestors",
           "ignore the ancestors of the pkill process"),
    OPTION("-c", "--count", "print the count of matching processes"),
    OPTION("-e", "--echo", "display what is killed"),
    OPTION("-f", "--full", "match against full command line"),
    OPTION("-F", "--pidfile", "read PID from file", STRING_TYPE),
    OPTION("-G", "--group", "match by real group ID or name", STRING_TYPE),
    OPTION("-i", "--ignore-case", "match case insensitively"),
    OPTION("-L", "--logpidfile", "fail if the pidfile is not locked"),
    OPTION("-n", "--newest", "select the most recently started process"),
    OPTION("-o", "--oldest", "select the oldest process"),
    OPTION("-O", "--older", "select processes older than the given seconds",
           STRING_TYPE),
    OPTION("-P", "--parent", "match by parent PID", STRING_TYPE),
    OPTION("-q", "--queue", "use sigqueue(2) rather than kill(2)", INT_TYPE),
    OPTION("-s", "--session", "match by session ID (0 means own session)",
           STRING_TYPE),
    OPTION("-u", "--euid", "match by effective user ID or name", STRING_TYPE),
    OPTION("-U", "--uid", "match by real user ID or name", STRING_TYPE),
    OPTION("-x", "--exact", "match the whole name or command line"),
    OPTION("", "--signal", "send the given signal instead of SIGTERM",
           STRING_TYPE),
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(pkill, "pkill", "pkill [OPTION]... PATTERN",
                 "Signal processes selected by name or command line.\n"
                 "On Windows, TERM/KILL are mapped to Win32 termination.",
                 "  pkill -f worker-token\n"
                 "  pkill -0 explorer\n"
                 "  pkill -u 1000 -n node",
                 "pgrep(1), kill(1), killall(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PKILL_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/pkill.rs, pkill_cmd).
  return winux::rust2::run_command("pkill", ctx.raw_args);
}
