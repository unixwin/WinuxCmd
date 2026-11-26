// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for cat.
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

// clang-format off
auto constexpr CAT_OPTIONS =
    // [GNU]
    std::array{OPTION("-A", "--show-all", "equivalent to -vET"),
    // [GNU]
               OPTION("-b", "--number-nonblank", "number nonempty output lines, overrides -n"),
    // [GNU]
               OPTION("-e", "", "equivalent to -vE"),
    // [GNU]
               OPTION("-E", "--show-ends", "display $ at end of each line"),
    // [GNU]
               OPTION("-n", "--number", "number all output lines"),
    // [GNU]
               OPTION("-s", "--squeeze-blank", "suppress repeated empty output lines"),
    // [GNU]
               OPTION("-t", "", "equivalent to -vT"),
    // [GNU]
               OPTION("-T", "--show-tabs", "display TAB characters as ^I"),
    // [GNU]
               OPTION("-u", "", "(ignored, for POSIX compatibility)"),
    // [GNU]
               OPTION("-v", "--show-nonprinting", "use ^ and M- notation, except for LFD and TAB")};
// clang-format on

REGISTER_COMMAND(cat, "cat",
                 "concatenate files and print on the standard output",
                 "Concatenate FILE(s) to standard output.\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\nExamples:\n"
                 "  cat f g  Output f's contents, then g's contents.\n"
                 "  cat      Copy standard input to standard output.",
                 "  cat file.txt              Display contents of file.txt\n"
                 "  cat -n file.txt           Number all output lines\n"
                 "  cat file1.txt file2.txt   Concatenate multiple files\n"
                 "  cat                       Read from standard input",
                 "tac(1), head(1), tail(1), more(1), less(1)", "caomengxuan666",
                 "Copyright © 2026 WinuxCmd", CAT_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/cat.rs, cat_cmd).
  const bool reads_stdin =
      ctx.positionals.empty() ||
      std::ranges::any_of(ctx.positionals,
                          [](std::string_view a) { return a == "-"; });
  if (reads_stdin) {
    return winux::rust2::run_command_with_stdin("cat", ctx.raw_args);
  }
  return winux::rust2::run_command("cat", ctx.raw_args);
}
