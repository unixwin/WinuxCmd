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

auto constexpr MKNOD_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-m", "--mode", "set file permission bits", STRING_TYPE),
    // [DIFFERS]
    OPTION("-Z", "", "set the SELinux security context to default type"),
    // [DIFFERS]
    OPTION("", "--context", "set the SELinux security context",
           OPTIONAL_STRING_TYPE),
};

REGISTER_COMMAND(
    mknod, "mknod", "mknod [OPTION]... NAME TYPE [MAJOR MINOR]",
    "Create the special file NAME of the given TYPE.\n"
    "\n"
    "Type 'p' creates a FIFO: on Windows this is an on-disk marker file\n"
    "that WinuxCmd commands bridge to a named pipe. Block and character\n"
    "device nodes have no Windows equivalent and report an error.",
    "  mknod mypipe p\n"
    "  mknod ttyS0 c 4 64\n"
    "  mknod sda b 8 0",
    "mkfifo(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", MKNOD_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/mknod.rs); the C++ side keeps only this
  // registration. ctx.raw_args passes the operands verbatim because the GNU
  // applet parses its own argv.
  return winux::rust2::run_command("mknod", ctx.raw_args);
}
