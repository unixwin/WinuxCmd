// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for fmt.
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

auto constexpr FMT_OPTIONS = std::array{
    // [GNU]
    OPTION("-c", "--crown-margin",
           "preserve indentation of the first two lines", BOOL_TYPE),
    // [EXT] -m/--preserve-headers: not in GNU fmt
    OPTION("-m", "--preserve-headers",
           "attempt to detect and preserve mail headers", BOOL_TYPE),
    // [GNU]
    OPTION("-p", "--prefix", "reformat only lines beginning with STRING",
           STRING_TYPE),
    // [EXT] -P/--skip-prefix: not in GNU fmt
    OPTION("-P", "--skip-prefix", "do not reformat lines beginning with STRING",
           STRING_TYPE),
    // [GNU]
    OPTION("-s", "--split-only", "split long lines, but do not refill",
           BOOL_TYPE),
    // [GNU]
    OPTION("-t", "--tagged-paragraph", "expect indentation in first 2 lines",
           BOOL_TYPE),
    // [GNU]
    OPTION("-u", "--uniform-spacing",
           "one space between words, two after sentences", BOOL_TYPE),
    // [EXT] -x/--exact-prefix: not in GNU fmt
    OPTION("-x", "--exact-prefix", "do not ignore leading whitespace for -p",
           BOOL_TYPE),
    // [EXT] -X/--exact-skip-prefix: not in GNU fmt
    OPTION("-X", "--exact-skip-prefix",
           "do not ignore leading whitespace for -P", BOOL_TYPE),
    // [EXT] -T/--tab-width: GNU fmt has no such option; its tab
    // width is the compile-time TABWIDTH 8.
    OPTION("-T", "--tab-width",
           "treat tabs as TABWIDTH spaces when measuring line length",
           STRING_TYPE),
    // [GNU]
    OPTION("-w", "--width", "maximum line width (default 75)", STRING_TYPE),
    // [GNU]
    OPTION("-g", "--goal", "goal width (default 93% of width)", STRING_TYPE)};
REGISTER_COMMAND(
    fmt, "fmt", "fmt [OPTION]... [FILE]...",
    "Reformat paragraphs.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Note: This is a simplified implementation. Advanced features\n"
    "like crown margin and tagged paragraphs are not fully supported.",
    "  fmt file.txt\n"
    "  fmt -w 60 file.txt\n"
    "  fmt -s file.txt",
    "fold(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", FMT_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/fmt.rs, fmt_cmd); the C++ cost-DP formatter
  // is deleted. ctx.raw_args passes the operands verbatim because GNU
  // fmt.c parses its own getopt run (the dispatcher's obsolete-args rewrite
  // hook runs before this body). GNU fmt reads stdin only when there is no
  // file operand (a "-" operand reads stdin too), so the whole-buffer v1
  // entry is called exactly there.
  const bool reads_stdin =
      ctx.positionals.empty() ||
      std::ranges::any_of(ctx.positionals,
                          [](std::string_view a) { return a == "-"; });
  if (reads_stdin) {
    return winux::rust2::run_command_with_stdin("fmt", ctx.raw_args);
  }
  return winux::rust2::run_command("fmt", ctx.raw_args);
}
