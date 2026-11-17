// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for chown command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd


#include "core/command_macros.h"
#include "pch/pch.h"


import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CHOWN_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-c", "--changes",
           "like verbose but report only when a change is made"),
    // [DIFFERS]
    OPTION("-f", "--silent", "suppress most error messages"),
    // [DIFFERS]
    OPTION("", "--quiet", "suppress most error messages"),
    // [DIFFERS]
    OPTION("", "--dereference", "affect referent of each symbolic link"),
    // [DIFFERS]
    OPTION("-h", "--no-dereference",
           "affect symbolic links instead of referenced files"),
    // [DIFFERS]
    OPTION("", "--from", "change only from current owner/group", STRING_TYPE),
    // [DIFFERS]
    OPTION("-H", "", "traverse command-line symlinks to directories"),
    // [DIFFERS]
    OPTION("-L", "", "traverse every symlink to a directory"),
    // [DIFFERS]
    OPTION("-P", "", "do not traverse any symbolic links"),
    // [DIFFERS]
    OPTION("-R", "--recursive", "operate on files and directories recursively"),
    // [DIFFERS]
    OPTION("", "--reference", "use RFILE's owner and group", STRING_TYPE),
    // [DIFFERS]
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
    // [DIFFERS]
    OPTION("", "--preserve-root", "fail to operate recursively on '/'"),
    // [DIFFERS]
    OPTION("", "--no-preserve-root", "do not treat '/' specially"),
};

REGISTER_COMMAND(
    chown, "chown", "change file owner and group",
    "Change the owner and/or group of each FILE.\n"
    "\n"
    "Note: On Windows, changing ownership requires the\n"
    "SeTakeOwnership/SeRestore privileges, usually an elevated shell.\n"
    "Use -h to modify a symbolic link itself, including dangling links.",
    "  chown user file.txt            Change owner of file.txt\n"
    "  chown user:group file.txt     Change owner and group\n"
    "  chown -R user dir/            Recursively change owner\n"
    "  chown user *.txt              Change owner of all .txt files",
    "chgrp(1), chmod(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    CHOWN_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/chown.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // The command never reads stdin: the pass-nothing run_command contract
  // keeps fd 0 untouched for later pipeline stages.
  return winux::rust2::run_command("chown", ctx.raw_args);
}
