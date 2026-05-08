/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr TPUT_OPTIONS =
    std::array{OPTION("", "", "terminal capabilities", STRING_TYPE)};

REGISTER_COMMAND(tput,
                 /* cmd_name */ "tput",
                 /* cmd_synopsis */ "tput [CAPNAME]",
                 /* cmd_desc */ "Initialize a terminal or query terminfo.",
                 /* examples */ "tput clear",
                 /* see_also */ "reset",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ TPUT_OPTIONS) {
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("tput: missing capability name");
    return 1;
  }

  std::string cap = std::string(ctx.positionals[0]);

  if (cap == "clear") {
    safePrint("\033[2J\033[H");
  } else if (cap == "reset") {
    safePrint("\033c");
  } else if (cap == "bold") {
    safePrint("\033[1m");
  } else if (cap == "sgr0") {
    safePrint("\033[0m");
  } else if (cap == "cup") {
    safePrint("\033[H");
  } else {
    safeErrorPrintLn("tput: unknown capability '" + cap + "'");
    return 1;
  }

  return 0;
}
