/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;
import rust2_bridge;

auto constexpr TPUT_OPTIONS =
    // [GNU] option
    std::array{
        OPTION("-T", "", "specify terminal type", STRING_TYPE),
        // [EXT] operand
        OPTION("", "", "terminal capabilities", STRING_TYPE),
        // [GNU] -V: version shorthand (probed: `tput -V`)
        OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(tput,
                 /* cmd_name */ "tput",
                 /* cmd_synopsis */ "tput [CAPNAME]",
                 /* cmd_desc */ "Initialize a terminal or query terminfo.",
                 /* examples */ "tput clear",
                 /* see_also */ "reset",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ TPUT_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/infocmp.rs, tput_cmd).
  return winux::rust2::run_command("tput", ctx.raw_args);
}
