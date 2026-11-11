// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for fold.
/// @Version: 0.1.0
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

auto constexpr FOLD_OPTIONS = std::array{
    // [GNU]
    OPTION("-b", "--bytes", "count bytes rather than columns", BOOL_TYPE),
    // [GNU] -c/--characters (added in coreutils 9.5)
    OPTION("-c", "--characters", "count characters rather than columns",
           BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--spaces", "break at spaces", BOOL_TYPE),
    // [GNU]
    OPTION("-w", "--width", "use WIDTH columns instead of 80", STRING_TYPE)};
REGISTER_COMMAND(fold, "fold", "fold [OPTION]... [FILE]...",
                 "Wrap input lines to fit specified width.\n"
                 "\n"
                 "Write each FILE to standard output, wrapping input lines to "
                 "fit in width columns.\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "Note: This implementation supports basic folding.\n"
                 "Some locale-specific character-width details remain "
                 "approximate on Windows.",
                 "  fold file.txt\n"
                 "  fold -w 60 file.txt\n"
                 "  fold -s file.txt\n"
                 "  fold -b file.txt\n"
                 "  fold -c file.txt\n"
                 "  echo 'very long line' | fold -w 10",
                 "fmt(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FOLD_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/fold.rs, fold_cmd); the C++ wrapper is
  // deleted. ctx.raw_args passes the operands verbatim because GNU fold.c
  // parses its own getopt run (the dispatcher's obsolete-args rewrite hook
  // runs before this body, so "-3" has already become --width=3). GNU fold
  // reads stdin only when there is no file operand or a "-" operand
  // (fold.c:66-77), so the whole-buffer v1 entry is called exactly there.
  const bool reads_stdin =
      ctx.positionals.empty() ||
      std::ranges::any_of(ctx.positionals,
                          [](std::string_view a) { return a == "-"; });
  if (reads_stdin) {
    return winux::rust2::run_command_with_stdin("fold", ctx.raw_args);
  }
  return winux::rust2::run_command("fold", ctx.raw_args);
}
