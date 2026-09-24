/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

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
  if (ctx.get<bool>("-V", false)) {
    safePrintLn("ncurses 6.6.20251230");
    return 0;
  }

  const bool quiet = ctx.get<bool>("-q", false);
  const bool initialize = ctx.get<bool>("-I", false);
  const bool shell_output = ctx.get<bool>("-s", false);
  const bool report_stderr = ctx.get<bool>("-r", false);
  // -Q: do not output control key settings (no control keys on Windows)
  (void)ctx.get<bool>("-Q", false);

  // [DIFFERS] -c/-e/-i/-k: GNU reset control character options; not applicable
  // on Windows console
  if (ctx.has("-c") || ctx.has("-e") || ctx.has("-i") || ctx.has("-k")) {
    // Silently ignore: Windows console does not support these terminal control
    // characters
  }

  // [DIFFERS] -m: GNU reset maps identifier to type; not applicable on Windows
  // console
  if (ctx.has("-m")) {
    // Silently ignore: Windows console does not support terminfo identifier
    // mapping
  }

  // [DIFFERS] -w: GNU reset sets window-size; partially applicable on Windows
  // console
  if (ctx.has("-w")) {
    // Attempt to reset console window size to default
    // On Windows console, we can use SetConsoleScreenBufferSize but the default
    // size is system-dependent, so we simply note this is a no-op
  }

  if (shell_output) {
    safePrintLn("TERM=xterm;");
    return 0;
  }

  if (report_stderr && !ctx.positionals.empty()) {
    safeErrorPrintLn(std::string(ctx.positionals.front()));
  }

  if (!quiet) {
    safePrint("\033c");
  }

  if (initialize) {
    safePrint("\033[?25h");
    safePrint("\033[0m");
  }

  return 0;
}
