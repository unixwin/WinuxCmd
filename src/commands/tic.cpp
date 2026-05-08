/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr TIC_OPTIONS =
    std::array{OPTION("", "", "compile terminfo entries", STRING_TYPE)};

REGISTER_COMMAND(tic,
                 /* cmd_name */ "tic",
                 /* cmd_synopsis */ "tic [FILE]",
                 /* cmd_desc */ "Terminfo compiler.",
                 /* examples */ "tic myterm.ti",
                 /* see_also */ "infocmp",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ TIC_OPTIONS) {
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("tic: missing file operand");
    return 1;
  }
  safePrintLn("tic: terminfo compilation not supported on Windows");
  return 0;
}
