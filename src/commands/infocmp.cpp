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

auto constexpr INFOCMP_OPTIONS = std::array{
    // [GNU]
    OPTION("-1", "", "print capabilities one per line", BOOL_TYPE),
    // [EXT]
    OPTION("-V", "", "print curses-version", BOOL_TYPE),
    // [EXT] operand
    OPTION("", "", "print terminfo description for TERMNAME", STRING_TYPE)};

REGISTER_COMMAND(infocmp,
                 /* cmd_name */ "infocmp",
                 /* cmd_synopsis */ "infocmp [-1] [TERMNAME]",
                 /* cmd_desc */
                 "Print the terminfo description of a terminal from the "
                 "builtin terminfo table, in GNU infocmp source format.",
                 /* examples */ "infocmp\ninfocmp -1 xterm",
                 /* see_also */ "tput, toe, tic",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ INFOCMP_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/infocmp.rs, infocmp_cmd).
  return winux::rust2::run_command("infocmp", ctx.raw_args);
}
