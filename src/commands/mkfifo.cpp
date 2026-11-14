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

auto constexpr MKFIFO_OPTIONS = std::array{
    // [GNU]
    OPTION("-m", "--mode", "set file permission bits", STRING_TYPE),
    // [GNU] -Z pairs with --context[=CTX] (system.h:344-345); the old
    // declaration invented a --security-context long name and a
    // separate argument-less --context flag.
    OPTION("-Z", "--context", "set the SELinux security context",
           OPTIONAL_STRING_TYPE),
};

REGISTER_COMMAND(
    mkfifo, "mkfifo", "mkfifo [OPTION]... NAME...",
    "Create named pipes (FIFOs) with the given NAMEs.\n"
    "\n"
    "Windows has no filesystem FIFO node type, so WinuxCmd emulates one with\n"
    "an on-disk marker file. WinuxCmd commands opening the marker for\n"
    "reading or writing are bridged to a Windows named pipe with blocking\n"
    "POSIX open() semantics; other tools see a small marker file.",
    "  mkfifo mypipe\n"
    "  mkfifo -m 600 mypipe\n"
    "  mkfifo --context system_u:object_r:fifo_file_t:s0 mypipe",
    "mknod(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", MKFIFO_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/mkfifo.rs); the C++ side keeps only this
  // registration. ctx.raw_args passes the operands verbatim because the GNU
  // applet parses its own argv.
  return winux::rust2::run_command("mkfifo", ctx.raw_args);
}
