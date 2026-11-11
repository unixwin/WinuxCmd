// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for truncate.
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

auto constexpr TRUNCATE_OPTIONS = std::array{
    // [GNU]
    OPTION("-c", "--no-create", "do not create files", BOOL_TYPE),
    // [GNU]
    OPTION("-o", "--io-blocks", "treat SIZE as number of IO blocks", BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--size", "set or adjust the file size by SIZE bytes",
           STRING_TYPE),
    // [GNU]
    OPTION("-r", "--reference", "base size on RFILE", STRING_TYPE)};

REGISTER_COMMAND(
    truncate, "truncate", "truncate OPTION... FILE...",
    "Shrink or extend the size of each FILE to the specified size.\n"
    "\n"
    "A FILE argument that does not exist is created.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "SIZE is an integer and optional unit (example: 10M is 10*1024*1024).\n"
    "Units are K, M, G, T, P, E, Z, Y (powers of 1024) or KB, MB,... (powers "
    "of 1000).\n"
    "\n"
    "SIZE may also be prefixed with +, -, <, >, /, or % for GNU-style\n"
    "relative, clamp, and rounding adjustments. -o treats SIZE as file-system\n"
    "I/O blocks.",
    "  truncate -s 0 file.txt          # empty file\n"
    "  truncate -s 1K file.txt         # 1KB file\n"
    "  truncate -s +100 file.txt      # extend by 100 bytes\n"
    "  truncate -s -50 file.txt       # shrink by 50 bytes\n"
    "  truncate -s 1M newfile.txt     # create 1MB file",
    "dd(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TRUNCATE_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/truncate.rs — truncate.c SIZE grammar with
  // +,-,<,>,/,% adjustments, K/KiB/KB unit families and --reference); the
  // C++ side keeps only this registration. ctx.raw_args passes the operands
  // verbatim because GNU truncate.c parses its own getopt run.
  //
  // GNU truncate.c never reads stdin, so the plain (no stdin) bridge entry
  // is used — the v1 whole-buffer slurp would block at an interactive
  // console where GNU returns immediately.
  return winux::rust2::run_command("truncate", ctx.raw_args);
}
