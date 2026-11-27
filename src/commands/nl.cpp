// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for nl.
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

auto constexpr NL_OPTIONS = std::array{
    // [GNU]
    OPTION("-b", "--body-numbering", "use STYLE for numbering body lines",
           STRING_TYPE),
    // [GNU]
    OPTION("-d", "--section-delimiter", "use CC for logical page delimiters",
           STRING_TYPE),
    // [GNU]
    OPTION("-f", "--footer-numbering", "use STYLE for numbering footer lines",
           STRING_TYPE),
    // [GNU]
    OPTION("-h", "--header-numbering", "use STYLE for numbering header lines",
           STRING_TYPE),
    // [GNU]
    OPTION("-i", "--line-increment", "line number increment at each line",
           STRING_TYPE),
    // [GNU]
    OPTION("-l", "--join-blank-lines",
           "group NUMBER empty lines as one numbered line", STRING_TYPE),
    // [GNU]
    OPTION("-n", "--number-format", "use FORMAT for line numbers", STRING_TYPE),
    // [GNU]
    OPTION("-p", "--no-renumber", "do not reset line numbers at logical pages",
           BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--number-separator",
           "add STRING after (possible) line number", STRING_TYPE),
    // [GNU]
    OPTION("-v", "--starting-line-number",
           "first line number on each logical page", STRING_TYPE),
    // [GNU]
    OPTION("-w", "--number-width", "width of line numbers", STRING_TYPE)};

REGISTER_COMMAND(
    nl, "nl", "nl [OPTION]... [FILE]...",
    "Number lines of files.\n"
    "\n"
    "Write each FILE to standard output, with line numbers added.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Supports GNU-compatible numbering formats, logical page delimiters,\n"
    "renumbering controls, and blank-line grouping.",
    "  nl file.txt\n"
    "  nl -b a file.txt          # number all lines\n"
    "  nl -i 5 file.txt          # increment by 5\n"
    "  nl -s ': ' file.txt       # use custom separator\n"
    "  nl -v 10 file.txt         # start from 10\n"
    "  nl -w 3 file.txt         # 3-digit numbers",
    "cat(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", NL_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/nl.rs, nl_cmd).
  const bool reads_stdin =
      ctx.positionals.empty() ||
      std::ranges::any_of(ctx.positionals,
                          [](std::string_view a) { return a == "-"; });
  if (reads_stdin) {
    return winux::rust2::run_command_with_stdin("nl", ctx.raw_args);
  }
  return winux::rust2::run_command("nl", ctx.raw_args);
}
