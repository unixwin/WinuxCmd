/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr TIC_OPTIONS = std::array{
    // [EXT] -V
    OPTION("-V", "", "print version", BOOL_TYPE),
    // [DIFFERS] -a: Windows has no terminfo compiler database
    // [EXT] -a
    OPTION("-a", "", "retain commented-out capabilities", BOOL_TYPE),
    // [DIFFERS] -c: Windows has no terminfo compiler database
    // [EXT] -c
    OPTION("-c", "", "check only", BOOL_TYPE),
    // [DIFFERS] -o: Windows has no terminfo compiler database
    // [EXT] -o
    OPTION("-o", "", "set output directory", STRING_TYPE),
    // [DIFFERS] -v: Windows has no terminfo compiler database
    // [EXT] -v
    OPTION("-v", "", "set verbosity level", OPTIONAL_INT_TYPE),
    // [DIFFERS] -x: Windows has no terminfo compiler database
    // [EXT] -x
    OPTION("-x", "", "treat unknown capabilities as user-defined", BOOL_TYPE)};

REGISTER_COMMAND(tic, "tic",
                 "tic [-e names] [-o dir] [-R name] [-v[n]] [-V] [-w[n]] "
                 "[-1aCDcfGgIKLNrsTtUx] source-file",
                 "Terminfo compiler.", "tic myterm.ti", "infocmp", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", TIC_OPTIONS) {
  if (ctx.get<bool>("-V", false)) {
    safePrintLn("ncurses 6.6.20251230");
    return 0;
  }
  const std::array unsupported = {"-a", "-c", "-o", "-v", "-x"};
  for (const auto option : unsupported) {
    if (ctx.has(option)) {
      safeErrorPrintLn(
          winux::i18n::format("command.tic.error.unsupported_option",
                              "tic: option {} is not supported on Windows "
                              "(terminfo compiler unavailable)",
                              option));
      return 1;
    }
  }

  if (ctx.positionals.empty()) {
    safeErrorPrintLn("tic: File name needed.  Usage:");
    safeErrorPrintLn(
        "\ttic [-e names] [-o dir] [-R name] [-v[n]] [-V] [-w[n]] "
        "[-1aCDcfGgIKLNrsTtUx] source-file");
    return 1;
  }
  safePrintLn("tic: terminfo compilation not supported on Windows");
  return 0;
}
