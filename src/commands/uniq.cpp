// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for uniq.
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

auto constexpr UNIQ_OPTIONS = std::array{
    // [GNU] option
    OPTION("-c", "--count", "prefix lines by the number of occurrences"),
    // [GNU] option
    OPTION("-d", "--repeated", "only print duplicate lines"),
    // [GNU] option
    OPTION("-D", "--all-repeated", "print all duplicate lines",
           OPTIONAL_STRING_TYPE),
    // [GNU] option
    OPTION("-f", "--skip-fields", "avoid comparing the first N fields",
           UINT_CLAMP_TYPE),
    // [GNU] option
    OPTION("-i", "--ignore-case", "ignore differences in case"),
    // [GNU] option
    OPTION("-s", "--skip-chars", "avoid comparing the first N characters",
           UINT_CLAMP_TYPE),
    // [GNU] option
    OPTION("-u", "--unique", "only print unique lines"),
    // [GNU] option
    OPTION("-w", "--check-chars", "compare no more than N characters",
           UINT_CLAMP_TYPE),
    // [GNU] -NUM: obsolete skip-fields count
    OPTION("-NUM", "", "", UINT_CLAMP_TYPE),
    // [GNU] option
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline"),
    // [GNU] option
    OPTION("", "--group", "show all items, separating groups",
           OPTIONAL_STRING_TYPE)};

REGISTER_COMMAND(
    uniq, "uniq", "uniq [OPTION]... [INPUT [OUTPUT]]",
    "Filter adjacent matching lines from INPUT (or standard input),\n"
    "writing to OUTPUT (or standard output).",
    "  uniq data.txt\n"
    "  sort a.txt | uniq -c\n"
    "  uniq -i -d words.txt",
    "sort(1), grep(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", UNIQ_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/uniq.rs, uniq_cmd).
  // Stdin is read when no file operand is given (ignoring obsolete +NUM
  // operands) or when the first file operand is "-".
  bool has_input_file = false;
  bool input_is_dash = false;
  for (std::string_view arg : ctx.positionals) {
    if (arg.size() > 1 && arg[0] == '+' &&
        std::ranges::all_of(arg.substr(1),
                            [](char c) { return c >= '0' && c <= '9'; })) {
      continue;
    }
    has_input_file = true;
    input_is_dash = (arg == "-");
    break;
  }
  if (!has_input_file || input_is_dash) {
    return winux::rust2::run_command_with_stdin("uniq", ctx.raw_args);
  }
  return winux::rust2::run_command("uniq", ctx.raw_args);
}
