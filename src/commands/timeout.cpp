// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Windows process timeout with signal and job control semantics.
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

auto constexpr TIMEOUT_OPTIONS = std::array{
    OPTION("-s", "--signal", "specify the signal to be sent on timeout",
           STRING_TYPE),
    OPTION("-k", "--kill-after",
           "also send a KILL signal if COMMAND still running", STRING_TYPE),
    OPTION("-v", "--verbose", "diagnose to stderr any signal sent on timeout",
           BOOL_TYPE),
    // [GNU] timeout.c long_options: --preserve-status makes a timed-out
    // command report the status it died with (128+signal) instead of 124.
    OPTION("", "--preserve-status",
           "exit with the status of COMMAND, even when timing out",
           BOOL_TYPE),
    // [GNU] --foreground: on POSIX this stops timeout from creating a
    // separate process group for COMMAND.  The Windows launch model
    // has no such separation to suppress -- the child shares the
    // console and is waited on in the foreground by construction --
    // so the option is accepted with its (vacuous) native effect.
    OPTION("", "--foreground",
           "run COMMAND in the foreground (native launch is always "
           "foreground)",
           BOOL_TYPE)};

REGISTER_COMMAND(
    timeout, "timeout", "timeout [OPTION] DURATION COMMAND [ARG]...",
    "Start COMMAND, and kill it if still running after DURATION.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "DURATION is a floating point number with an optional suffix:\n"
    "'s' for seconds (default), 'm' for minutes, 'h' for hours or 'd' for "
    "days.\n"
    "\n"
    "The command is executed as a child process and terminated when the\n"
    "time limit expires.\n"
    "\n"
    "Exit status:\n"
    "  124  if COMMAND times out, and --preserve-status is not specified\n"
    "  125  if the timeout command itself fails\n"
    "  126  if COMMAND is found but cannot be invoked\n"
    "  127  if COMMAND cannot be found\n"
    "  137  if COMMAND (or timeout itself) is sent the KILL (9) signal",
    "  timeout 5s command\n"
    "  timeout 1m command args\n"
    "  timeout -k 10s 30s command",
    "kill(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TIMEOUT_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/timeout.rs, timeout_cmd); the C++ side
  // keeps only this registration — the duration parser, child spawn/kill
  // machinery and the 124/125/126/127/137 exit-status contract move to
  // Rust (the child inherits the live stdin handle: the bridge does not
  // declare timeout a stdin reader). The GNU exit-code matrix gates
  // exit-only on the product side (the child set is platform-bound).
  return winux::rust2::run_command("timeout", ctx.raw_args);
}
