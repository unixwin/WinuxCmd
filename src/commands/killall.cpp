// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for killall (psmisc semantics).
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

auto constexpr KILLALL_OPTIONS = std::array{
    OPTION("-e", "--exact", "require an exact process-name match"),
    OPTION("-I", "--ignore-case", "match case insensitively"),
    OPTION("-i", "--interactive",
           "ask for confirmation before killing each process"),
    OPTION("-l", "--list", "list all known signal names"),
    OPTION("-o", "--older-than",
           "match only processes older than the given time", STRING_TYPE),
    OPTION("-q", "--quiet", "do not complain if no processes were killed"),
    OPTION("-r", "--regexp",
           "interpret process names as extended regular expressions"),
    OPTION("-s", "--signal", "send the given signal instead of SIGTERM",
           STRING_TYPE),
    OPTION("-u", "--user", "kill only processes owned by the given user",
           STRING_TYPE),
    OPTION("-v", "--verbose", "report successful signals"),
    OPTION("-w", "--wait", "wait for all killed processes to die"),
    OPTION("-y", "--younger-than",
           "match only processes younger than the given time", STRING_TYPE),
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(killall, "killall", "killall [OPTION]... NAME...",
                 "Signal processes by executable name.\n"
                 "On Windows, TERM/KILL are mapped to Win32 termination.",
                 "  killall notepad\n"
                 "  killall -0 explorer\n"
                 "  killall -u alice -r 'worker.*'",
                 "kill(1), pkill(1), pgrep(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", KILLALL_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/killall.rs, killall_cmd).
  return winux::rust2::run_command("killall", ctx.raw_args);
}
