// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sync command.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr SYNC_OPTIONS =
    // [GNU] -d, --data
    std::array{OPTION("-d", "--data",
                      "sync only file data, no unneeded metadata", BOOL_TYPE),
               // [GNU] -f, --file-system
               OPTION("-f", "--file-system",
                      "sync the file systems that contain files", BOOL_TYPE)};

REGISTER_COMMAND(
    sync,
    /* name */
    "sync",

    /* synopsis */
    "sync [OPTION]... [FILE]...",
    "Synchronize cached writes to persistent storage.\n"
    "\n"
    "If one or more files are specified, sync only those files.\n"
    "  -d, --data         sync only file data, no unneeded metadata\n"
    "  -f, --file-system  sync the file systems that contain the files\n"
    "Otherwise, all file system buffers are synchronized.\n"
    "\n"
    "Note: On Windows, this uses FlushFileBuffers to flush file buffers.",
    "  sync\n"
    "  sync -d file.txt\n"
    "  sync file1.txt file2.txt",

    /* see also */
    "fsync(2)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SYNC_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/sync.rs, sync_cmd); the C++ side keeps
  // only this registration — the FlushFileBuffers walk and the operand
  // glob expansion are deleted (GNU never globs operands; the caller's
  // shell does). ctx.raw_args passes the operands verbatim because GNU
  // sync.c parses its own getopt run.
  return winux::rust2::run_command("sync", ctx.raw_args);
}
