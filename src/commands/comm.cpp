// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for comm.
/// @Version: 0.1.0
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

auto constexpr COMM_OPTIONS = std::array{
    // [GNU]
    OPTION("-1", "", "suppress column 1 (lines unique to FILE1)", BOOL_TYPE),
    // [GNU]
    OPTION("-2", "", "suppress column 2 (lines unique to FILE2)", BOOL_TYPE),
    // [GNU]
    OPTION("-3", "", "suppress column 3 (lines that appear in both files)",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--check-order", "check that the input is correctly sorted",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--nocheck-order",
           "do not check that the input is correctly sorted", BOOL_TYPE),
    // [GNU]
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline"),
    // [GNU]
    OPTION("", "--output-delimiter", "separate columns with STR",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("", "--total", "output a summary")};

REGISTER_COMMAND(
    comm, "comm", "comm [OPTION]... FILE1 FILE2",
    "Compare sorted files line by line.\n"
    "\n"
    "With no options, produce three-column output. Column one contains\n"
    "lines unique to FILE1, column two contains lines unique to FILE2,\n"
    "and column three contains lines common to both files.\n"
    "\n"
    "Note: Input files must be sorted. This implementation does not\n"
    "automatically sort the input files.",
    "  comm file1 file2\n"
    "  comm -12 file1 file2        # show only common lines\n"
    "  comm -23 file1 file2        # show only lines in file1\n"
    "  comm -13 file1 file2        # show only lines in file2\n"
    "  comm -3 file1 file2 | wc -l # count common lines",
    "cmp(1), diff(1), uniq(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    COMM_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/comm.rs, comm_cmd); the C++ merge
  // pipeline is deleted. ctx.raw_args passes the operands verbatim because
  // GNU comm.c parses its own getopt run. GNU comm reads stdin only for a
  // "-" operand, so the whole-buffer v1 entry is called exactly there;
  // every other shape takes the stdin-less entry.
  const bool reads_stdin = std::ranges::any_of(
      ctx.positionals, [](std::string_view a) { return a == "-"; });
  if (reads_stdin) {
    return winux::rust2::run_command_with_stdin("comm", ctx.raw_args);
  }
  return winux::rust2::run_command("comm", ctx.raw_args);
}
