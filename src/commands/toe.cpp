/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr TOE_OPTIONS =
    std::array{OPTION("", "", "list terminal types", STRING_TYPE)};

REGISTER_COMMAND(toe,
                 /* cmd_name */ "toe",
                 /* cmd_synopsis */ "toe [OPTION]...",
                 /* cmd_desc */ "Table of terminfo entries.",
                 /* examples */ "toe",
                 /* see_also */ "infocmp",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ TOE_OPTIONS) {
  safePrintLn("windows-ansi");
  safePrintLn("vt100");
  safePrintLn("xterm");
  return 0;
}
