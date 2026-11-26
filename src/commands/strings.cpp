// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for strings.
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

auto constexpr STRINGS_OPTIONS = std::array{
    // [GNU] -a, --all
    OPTION("-a", "--all", "scan each file in its entirety (default)"),
    // [GNU] -d, --data
    OPTION("-d", "--data",
           "scan only object data sections (placeholder/no-op: BFD section "
           "parsing is not linked in this Windows build)"),
    // [GNU] -f, --print-file-name
    OPTION("-f", "--print-file-name", "print the file name before each string"),
    // [GNU] -n, --bytes
    OPTION("-n", "--bytes",
           "print sequences of at least MIN printable characters (default 4)",
           STRING_TYPE),
    // [GNU] -NUM
    OPTION("-NUM", "", "same as --bytes=MIN", INT_TYPE),
    // [GNU] -t, --radix
    OPTION("-t", "--radix",
           "print the offset within the file before each string", STRING_TYPE),
    // [GNU] -w, --include-all-whitespace
    OPTION("-w", "--include-all-whitespace",
           "include all whitespace as valid string characters"),
    // [GNU] -e, --encoding
    OPTION("-e", "--encoding",
           "select character encoding: s=7-bit-ascii, S=8-bit-UTF8, "
           "b=16-bit-big-endian, l=16-bit-little-endian, B=32-bit-big-endian, "
           "L=32-bit-little-endian",
           STRING_TYPE),
    // [GNU] -o
    OPTION("-o", "", "print offset before each string (alias for -t o)"),
    // [GNU] -s, --output-separator
    OPTION("-s", "--output-separator",
           "string used to separate parsed strings in output", STRING_TYPE),
    // [GNU] -T, --target
    OPTION("-T", "--target",
           "specify binary file format (placeholder/no-op: BFD target "
           "selection is not linked in this Windows build)",
           STRING_TYPE),
    // [GNU] -U, --unicode
    OPTION("-U", "--unicode",
           "unicode display mode (placeholder/no-op except non-default modes "
           "select UTF-8 scanning)",
           STRING_TYPE),
    // [GNU] -V: version shorthand (probed: `strings -V`).
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(
    strings, "strings", "strings [OPTION]... [FILE]...",
    "Print printable strings from FILE(s).\n"
    "\n"
    "For each FILE, write to standard output all printable character "
    "sequences\n"
    "that are at least MIN characters long. With no FILE, read standard "
    "input.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -a, --all                scan each file in its entirety (default)\n"
    "  -n, --bytes=MIN          print sequences of at least MIN characters "
    "(default 4)\n"
    "  -t, --radix=RADIX        print offset before each string: o=octal, "
    "d=decimal, x=hex\n"
    "  -e, --encoding=ENCODING  select character encoding:\n"
    "                             s=7-bit-ascii (default), S=8-bit-UTF8,\n"
    "                             b=16-bit-big-endian, "
    "l=16-bit-little-endian,\n"
    "                             B=32-bit-big-endian, L=32-bit-little-endian\n"
    "  -o                       print offset before each string (alias for -t "
    "o)\n"
    "\n"
    "Exit status:\n"
    "  0  if any string was found\n"
    "  1  if no string was found or error occurred",
    "  strings a.out            print strings from binary\n"
    "  strings -n 10 a.out     print strings of at least 10 chars\n"
    "  strings -t x a.out      print strings with hex offsets\n"
    "  strings -e S a.out      print UTF-8 strings",
    "grep(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", STRINGS_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/strings.rs, strings_cmd).
  const bool reads_stdin =
      ctx.positionals.empty() ||
      std::ranges::any_of(ctx.positionals,
                          [](std::string_view a) { return a == "-"; });
  if (reads_stdin) {
    return winux::rust2::run_command_with_stdin("strings", ctx.raw_args);
  }
  return winux::rust2::run_command("strings", ctx.raw_args);
}
