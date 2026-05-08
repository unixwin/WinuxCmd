/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr INFOCMP_OPTIONS =
    std::array{OPTION("", "", "compare terminal descriptions", STRING_TYPE)};

REGISTER_COMMAND(infocmp,
                 /* cmd_name */ "infocmp",
                 /* cmd_synopsis */ "infocmp [TERMNAME]",
                 /* cmd_desc */ "Print terminfo description.",
                 /* examples */ "",
                 /* see_also */ "tput",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ INFOCMP_OPTIONS) {
  safePrintLn("# Windows ANSI Terminal");
  safePrintLn("windows-ansi|Windows ANSI Terminal,");
  safePrintLn("    am, cols#80, it#8, lines#24,");
  safePrintLn("    clear=\\E[H\\E[2J, cr=^M,");
  safePrintLn("    sgr0=\\E[0m, smso=\\E[7m, smul=\\E[4m");
  return 0;
}
