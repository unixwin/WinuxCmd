/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr RESET_OPTIONS = std::array{
    // [EXT]
    OPTION("-I", "", "no initialization strings", BOOL_TYPE),
    // [EXT] [DIFFERS] GNU reset: -c sets control characters; not applicable on
    // Windows console
    OPTION("-c", "", "set control characters", BOOL_TYPE),
    // [DIFFERS] GNU reset: -e sets erase character; not applicable on Windows
    // console
    OPTION("-e", "", "erase character", STRING_TYPE),
    // [DIFFERS] GNU reset: -i sets interrupt character; not applicable on
    // Windows console
    OPTION("-i", "", "interrupt character", STRING_TYPE),
    // [DIFFERS] GNU reset: -k sets kill character; not applicable on Windows
    // console
    OPTION("-k", "", "kill character", STRING_TYPE),
    // [DIFFERS] GNU reset: -m maps identifier to type; not applicable on
    // Windows console
    OPTION("-m", "", "map identifier to type", STRING_TYPE),
    // [EXT]
    OPTION("-q", "", "display term only, do no changes", BOOL_TYPE),
    // [EXT]
    OPTION("-Q", "", "do not output control key settings", BOOL_TYPE),
    // [EXT]
    OPTION("-r", "", "display term on stderr", BOOL_TYPE),
    // [EXT]
    OPTION("-s", "", "output TERM set command", BOOL_TYPE),
    // [EXT]
    OPTION("-V", "", "print curses-version", BOOL_TYPE),
    // [DIFFERS] GNU reset: -w sets window-size; not fully implemented on
    // Windows console
    OPTION("-w", "", "set window-size", BOOL_TYPE)};

REGISTER_COMMAND(reset, "reset", "reset [options] [terminal]",
                 "Reset terminal to its default state.",
                 "  reset\n  reset -q\n  reset -V", "tput(1), clear(1)",
                 "WinuxCmd", "Copyright © 2026 WinuxCmd", RESET_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/reset.rs, reset_cmd).
  return winux::rust2::run_command("reset", ctx.raw_args);
}
