// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"
import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr WC_OPTIONS = std::array{
    // [GNU] option
    OPTION("-c", "--bytes", "print the byte counts"),
    // [GNU] option
    OPTION("-m", "--chars", "print the character counts"),
    // [GNU] option
    OPTION("-l", "--lines", "print the newline counts"),
    // [GNU] option
    OPTION("", "--debug", "print line-count implementation diagnostics"),
    // [GNU] option
    OPTION(
        "", "--files0-from",
        "read input from the files specified by NUL-terminated names in file F",
        STRING_TYPE),
    // [GNU] option
    OPTION("-L", "--max-line-length", "print the maximum display width"),
    // [GNU] option
    OPTION("-w", "--words", "print the word counts"),
    // [GNU] option
    OPTION("", "--total", "when to print a line with total counts",
           STRING_TYPE)};

REGISTER_COMMAND(
    wc,
    /* name */
    "wc",

    /* synopsis */
    "wc [OPTION]... [FILE]...",

    /* description */
    "Print newline, word, and byte counts for each FILE, and a total line if\n"
    "more than one FILE is specified.  A word is a non-zero-length sequence "
    "of\n"
    "printable characters delimited by white space.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "The options below may be used to select which counts are printed, always "
    "in\n"
    "the following order: newline, word, character, byte, maximum line "
    "length.\n"
    "  -c, --bytes            print the byte counts\n"
    "  -m, --chars            print the character counts\n"
    "  -l, --lines            print the newline counts\n"
    "      --files0-from=F    read input from the files specified by\n"
    "                           NUL-terminated names in file F;\n"
    "                           If F is - then read names from standard input\n"
    "  -L, --max-line-length  print the maximum display width\n"
    "  -w, --words            print the word counts\n"
    "      --total=WHEN       when to print a line with total counts;\n"
    "                           WHEN can be: auto, always, only, never\n"
    "      --help        display this help and exit\n"
    "      --version     output version information and exit",

    /* examples */
    "  wc file.txt           # Count lines, words, and bytes in file.txt\n"
    "  wc -l file.txt        # Count only lines in file.txt\n"
    "  wc -w file.txt        # Count only words in file.txt\n"
    "  wc -c file.txt        # Count only bytes in file.txt\n"
    "  wc -m file.txt        # Count only characters in file.txt\n"
    "  wc -L file.txt        # Print maximum line length in file.txt\n"
    "  wc file1.txt file2.txt # Count multiple files and show total",

    /* see_also */
    "cat(1), grep(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    WC_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/wc.rs, wc_cmd).
  const bool files0_from_stdin =
      ctx.has("--files0-from") &&
      ctx.get<std::string>("--files0-from", "") == "-";
  const bool reads_stdin =
      files0_from_stdin ||
      (!ctx.has("--files0-from") && ctx.positionals.empty()) ||
      std::ranges::any_of(ctx.positionals,
                          [](std::string_view a) { return a == "-"; });
  if (reads_stdin) {
    return winux::rust2::run_command_with_stdin("wc", ctx.raw_args);
  }
  return winux::rust2::run_command("wc", ctx.raw_args);
}
