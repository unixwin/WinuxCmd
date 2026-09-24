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
  if (ctx.get<bool>("-V", false)) {
    safePrintLn("ncurses 6.6.20251230");
    return 0;
  }
  const std::array unsupported = {"-a", "-s", "-u", "-U", "-v"};
  for (const auto option : unsupported) {
    if (ctx.has(option)) {
      safeErrorPrintLn(
          winux::i18n::format("command.toe.error.unsupported_option",
                              "toe: option {} is not supported on Windows "
                              "(terminfo database unavailable)",
                              option));
      return 1;
    }
  }

  if (ctx.get<bool>("-h", false)) {
    safePrintLn("Terminfo Entries");
  }
  safePrintLn("windows-ansi Windows ANSI/VT console");
  safePrintLn("xterm        xterm terminal emulator");
  safePrintLn("vt100        DEC VT100");
  safePrintLn("ansi         ANSI/VT100 terminal");
  return 0;
}
