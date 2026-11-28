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

auto constexpr TOE_OPTIONS =
    // [DIFFERS] -a: Windows has no terminfo database
    // [EXT] -a
    std::array{OPTION("-a", "", "list all terminal databases", BOOL_TYPE),
               // [EXT] -h
               OPTION("-h", "", "list with a heading", BOOL_TYPE),
               // [DIFFERS] -s: Windows has no terminfo database
               // [EXT] -s
               OPTION("-s", "", "sort by terminal name", BOOL_TYPE),
               // [DIFFERS] -u: Windows has no terminfo database
               // [EXT] -u
               OPTION("-u", "", "write direct dependencies", STRING_TYPE),
               // [DIFFERS] -U: Windows has no terminfo database
               // [EXT] -U
               OPTION("-U", "", "write reverse dependencies", STRING_TYPE),
               // [DIFFERS] -v: Windows has no terminfo database
               // [EXT] -v
               OPTION("-v", "", "set verbosity level", OPTIONAL_INT_TYPE),
               // [EXT] -V
               OPTION("-V", "", "print version", BOOL_TYPE)};

REGISTER_COMMAND(toe, "toe", "toe [-ahsuUV] [-v n] [file...]",
                 "Table of terminfo entries.", "toe\ntoe -V", "infocmp",
                 "WinuxCmd", "Copyright © 2026 WinuxCmd", TOE_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/toe.rs, toe_cmd).
  return winux::rust2::run_command("toe", ctx.raw_args);
}
