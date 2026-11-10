// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for readlink.
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

auto constexpr READLINK_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-f", "--canonicalize", "canonicalize by following every symlink",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-e", "--canonicalize-existing",
           "canonicalize by following symlinks (must exist)", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-m", "--canonicalize-missing",
           "canonicalize by following symlinks (may not exist)", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-n", "--no-newline", "do not output the trailing delimiter",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-q", "--quiet", "suppress most error messages", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-s", "--silent", "suppress most error messages", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-v", "--verbose", "report error message", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-z", "--zero", "end each output line with NUL", BOOL_TYPE)};

REGISTER_COMMAND(
    readlink, "readlink", "readlink [OPTION]... FILE...",
    "Print value of a symbolic link or canonical file name.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This is a Windows implementation. Windows supports\n"
    "reparse points (symlinks/junctions) but they work differently\n"
    "than Unix symlinks. This command provides basic detection.",
    "  readlink file\n"
    "  readlink -f file\n"
    "  readlink -e file",
    "ln(1), realpath(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    READLINK_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/readlink.rs, readlink_cmd); the C++ side
  // keeps only this registration — the raw reparse-buffer decode, the
  // canonicalization walks and the terminator policy are deleted. The
  // handler receives ctx.raw_args verbatim because GNU readlink.c parses
  // its own getopt run (its -q/-s/-v verbosity is a last-wins enum).
  // Link targets and canonical names are DATA and reach stdout through
  // the bridge's raw (non-translating) writers.
  return winux::rust2::run_command("readlink", ctx.raw_args);
}
