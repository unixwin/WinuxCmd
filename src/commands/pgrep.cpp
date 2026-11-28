// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pgrep (procps 4.0.4 semantics).
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

auto constexpr PGREP_OPTIONS = std::array{
    OPTION("-a", "--list-full", "list PID and full command line"),
    OPTION("-A", "--ignore-ancestors",
           "ignore the ancestors of the pgrep process"),
    OPTION("-c", "--count", "print only a count of matching processes"),
    OPTION("-d", "--delimiter", "specify output delimiter", STRING_TYPE),
    OPTION("-f", "--full", "match against full command line"),
    OPTION("-F", "--pidfile", "read PID from file", STRING_TYPE),
    OPTION("-G", "--group", "match by real group ID or name", STRING_TYPE),
    OPTION("-i", "--ignore-case", "match case insensitively"),
    OPTION("-l", "--list-name", "list PID and process name"),
    OPTION("-L", "--logpidfile", "fail if the pidfile is not locked"),
    OPTION("-n", "--newest", "select the most recently started process"),
    OPTION("-o", "--oldest", "select the oldest process"),
    OPTION("-O", "--older", "select processes older than the given seconds",
           STRING_TYPE),
    OPTION("-P", "--parent", "match by parent PID", STRING_TYPE),
    OPTION("-s", "--session", "match by session ID (0 means own session)",
           STRING_TYPE),
    OPTION("-u", "--euid", "match by effective user ID or name", STRING_TYPE),
    OPTION("-U", "--uid", "match by real user ID or name", STRING_TYPE),
    OPTION("-v", "--inverse", "negate the matching"),
    OPTION("-w", "--lightweight", "list thread IDs of matching processes"),
    OPTION("-x", "--exact", "match the whole name or command line"),
    OPTION("", "--signal", "", STRING_TYPE),
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(pgrep, "pgrep", "pgrep [OPTION]... PATTERN",
                 "Look up processes by name or command line.\n"
                 "The current pgrep process is excluded from matches.",
                 "  pgrep explorer\n"
                 "  pgrep -af powershell\n"
                 "  pgrep -u 1000 -n node",
                 "ps(1), pkill(1), pidof(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PGREP_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/pgrep.rs, pgrep_cmd).
  return winux::rust2::run_command("pgrep", ctx.raw_args);
}
