// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for paste.
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

auto constexpr PASTE_OPTIONS = std::array{
    // [GNU]
    OPTION("-d", "--delimiters", "reuse characters from LIST instead of TAB",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-s", "--serial", "paste one file at a time instead of in parallel",
           BOOL_TYPE),
    // [GNU]
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};
REGISTER_COMMAND(
    paste, "paste", "paste [OPTION]... [FILE]...",
    "Merge lines of files.\n"
    "\n"
    "Write lines consisting of the sequentially corresponding lines\n"
    "from each FILE, separated by TABs, to standard output.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This implementation supports basic paste functionality.",
    "  paste file1 file2\n"
    "  paste -d '|' file1 file2    # use pipe as delimiter\n"
    "  paste -s file                # serial mode (one file at a time)\n"
    "  paste -d '\\n' file          # use newline as delimiter",
    "cut(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", PASTE_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/paste.rs, paste_cmd); the C++ column
  // pipeline is deleted. ctx.raw_args passes the operands verbatim because
  // GNU paste.c parses its own getopt run. GNU paste reads stdin only when
  // there is no file operand or a "-" operand (paste.c:205-215), so the
  // whole-buffer v1 entry is called exactly there; every other shape takes
  // the stdin-less entry and never touches the input side.
  const bool reads_stdin =
      ctx.positionals.empty() ||
      std::ranges::any_of(ctx.positionals,
                          [](std::string_view a) { return a == "-"; });
  if (reads_stdin) {
    return winux::rust2::run_command_with_stdin("paste", ctx.raw_args);
  }
  return winux::rust2::run_command("paste", ctx.raw_args);
}
