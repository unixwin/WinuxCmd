/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr RUNCON_OPTIONS = std::array{
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-c", "--compute",
           "compute process transition context before modifying"),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-u", "--user", "set user component of the context", STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-r", "--role", "set role component of the context", STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-t", "--type", "set type component of the context", STRING_TYPE),
    // [DIFFERS] - SELinux is unavailable on Windows
    OPTION("-l", "--range", "set range component of the context", STRING_TYPE),
};

REGISTER_COMMAND(
    runcon, "runcon",
    "runcon CONTEXT COMMAND [ARG]...\n"
    "  or:  runcon [-c] [-u USER] [-r ROLE] [-t TYPE] [-l RANGE] [COMMAND "
    "[ARG]...]",
    "Run a program in a different SELinux security context.\n"
    "\n"
    "WinuxCmd accepts the GNU-compatible command line surface for runcon, but\n"
    "Windows does not provide SELinux process contexts. This command "
    "therefore\n"
    "acts as a compatibility command and reports that the operation is "
    "not\n"
    "supported on Windows.",
    "  runcon system_u:system_r:httpd_t:s0 cmd.exe /c echo hi\n"
    "  runcon -t httpd_t cmd.exe /c echo hi\n"
    "  runcon -c -u system_u -r system_r -t httpd_t cmd.exe /c echo hi",
    "chcon(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", RUNCON_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/runcon.rs); the C++ side keeps only this
  // registration. ctx.raw_args passes the operands verbatim because the GNU
  // applet parses its own argv.
  return winux::rust2::run_command("runcon", ctx.raw_args);
}
