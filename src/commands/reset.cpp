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

  // [GNU ncurses] tset resolves the terminal type once: the operand if
  // given, else $TERM (progs/tset.c). The historical build hardcoded
  // "xterm" for -s and ignored the type everywhere else.
  std::string term;
  if (!ctx.positionals.empty()) {
    term = std::string(ctx.positionals.front());
  } else if (const char* env = std::getenv("TERM"); env != nullptr) {
    term = env;
  } else {
    term = "xterm";
  }

  // [GNU] -q is "display term only" (tset's noset): print the type on
  // stdout and change nothing. It used to suppress the reset sequence
  // but print nothing at all.
  if (quiet) {
    safePrintLn(term);
    return 0;
  }

  // [GNU] Invoked as reset, tset aims its reset strings at stderr:
  // reset_start(stderr, ...) (tset.c, reset_cmd.c), which is what
  // leaves stdout clean for -q type printing and for the classic
  // eval $(reset -s) capture. The historical build wrote the reset
  // byte to stdout, so `reset > file` filed the byte away and never
  // reset the terminal. The reset therefore goes to stderr here,
  // ahead of the -r report, in GNU's emission order.
  safeErrorPrint("\033c");

  if (initialize) {
    safeErrorPrint("\033[?25h");
    safeErrorPrint("\033[0m");
  }

  // [GNU] -r reports the type on stderr: "Terminal type is %s.\n"
  // (tset.c). It used to echo the first operand back, and only when an
  // operand was given.
  if (report_stderr) {
    safeErrorPrintLn("Terminal type is " + term + ".");
  }

  if (shell_output) {
    // [GNU] print_shell_commands (tset.c): POSIX shells get
    // "TERM=%s;\n"; a SHELL whose name ends in "csh" gets the setenv
    // form bracketed by set/unset noglob. -s does not suppress the
    // reset itself (above); the streams are separate exactly so both
    // can happen.
    const char* shell_env = std::getenv("SHELL");
    const std::string shell = shell_env != nullptr ? shell_env : "";
    if (shell.size() >= 3 && shell.ends_with("csh")) {
      safePrintLn("set noglob;");
      safePrintLn("setenv TERM " + term + ";");
      safePrintLn("unset noglob;");
    } else {
      safePrintLn("TERM=" + term + ";");
    }
  }

  return 0;
}
