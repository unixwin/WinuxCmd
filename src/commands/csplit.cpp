// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for csplit.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CSPLIT_OPTIONS = std::array{
    // [GNU]
    OPTION("-f", "--prefix", "use PREFIX instead of 'xx'", STRING_TYPE),
    // [GNU]
    OPTION("-b", "--suffix-format", "use sprintf FORMAT instead of %02d",
           STRING_TYPE),
    // [GNU]
    OPTION("-n", "--digits", "use specified number of digits", STRING_TYPE),
    // [GNU]
    OPTION("-k", "--keep-files", "do not remove output files on errors",
           BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--quiet", "do not print counts of output file sizes",
           BOOL_TYPE),
    // [GNU]
    OPTION("-q", "--silent", "do not print counts of output file sizes",
           BOOL_TYPE),
    // [GNU]
    OPTION("-z", "--elide-empty-files", "remove empty output files", BOOL_TYPE),
    // [GNU]
    OPTION("", "--suppress-matched",
           "suppress lines matching PATTERN from output", BOOL_TYPE)};

REGISTER_COMMAND(
    csplit, "csplit", "csplit [OPTION]... FILE PATTERN...",
    "Output pieces of FILE separated by PATTERN(s) to files 'xx00', 'xx01', "
    "...\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "PATTERN is a line number, or a /regex/ pattern.\n"
    "\n"
    "Note: This implementation supports common GNU line-number, regex,\n"
    "skip, repeat, suppress-matched, and empty-file behavior.",
    "  csplit file.txt '/pattern/'\n"
    "  csplit -f chapter file.txt '/Chapter/'\n"
    "  csplit -n 3 file.txt 100\n"
    "  csplit -z file.txt '/^Header$/' '/^Footer$/'",
    "split(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CSPLIT_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/csplit.rs, csplit_cmd — csplit.c pattern
  // grammar (line numbers, /regex/ and %regex% skip forms, offsets,
  // {N}/{*} repeats), --suppress-matched, -z elision, -b suffix formats and
  // the GNU diagnostic wording). The C++ side keeps only this registration.
  //
  // stdin shape: FILE "-" is stdin. The v2 streaming entry is a lazy PULL
  // callback, so choosing it unconditionally costs nothing for real files
  // (fd 0 stays live for the next pipeline stage) and keeps whole-buffer
  // residency off the stdin path.
  return winux::rust2::run_command_with_stdin_streaming("csplit", ctx.raw_args);
}
