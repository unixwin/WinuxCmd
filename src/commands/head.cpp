// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief HEAD command options definition
 *
 * This array defines all the options supported by the head command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -c, @a --bytes: Print the first NUM bytes of each file; with the leading
 * '-', print all but the last NUM bytes [IMPLEMENTED]
 * - @a -n, @a --lines: Print the first NUM lines instead of the first 10; with
 * the
 *   leading '-', print all but the last NUM lines [IMPLEMENTED]
 * - @a
 * -NUM: Obsolete GNU-compatible shorthand for -n NUM [IMPLEMENTED]
 * - @a -q,
 * @a --quiet: Never print headers giving file names for multiple files
 * [IMPLEMENTED]
 * - @a --silent: Never print headers giving file names for
 * multiple files [IMPLEMENTED]
 * - @a -v, @a --verbose: Always print headers giving file names for multiple
 * files [IMPLEMENTED]
 * - @a -z, @a --zero-terminated: Line delimiter is NUL, not newline
 * [IMPLEMENTED]
 */
auto constexpr HEAD_OPTIONS = std::array{
    // [GNU]
    OPTION("-c", "--bytes",
           "print the first NUM bytes of each file; with the leading '-',\n"
           "print all but the last NUM bytes",
           STRING_TYPE),
    // [GNU]
    OPTION("-n", "--lines",
           "print the first NUM lines instead of the first 10; with the\n"
           "leading '-', print all but the last NUM lines",
           STRING_TYPE),
    // [GNU]
    OPTION("-q", "--quiet",
           "never print headers giving file names for multiple files"),
    // [GNU]
    OPTION("", "--silent",
           "never print headers giving file names for multiple files"),
    // [GNU]
    OPTION("-v", "--verbose",
           "always print headers giving file names for multiple files"),
    // [GNU]
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

REGISTER_COMMAND(
    head, "head", "head [OPTION]... [FILE]...",
    "Print the first 10 lines of each FILE to standard output.\n"
    "With more than one FILE, precede each with a header giving the file "
    "name.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.",
    "  head file.txt\n"
    "  head -n 20 file.txt\n"
    "  head -20 file.txt\n"
    "  head -c 64 file.txt\n"
    "  head -n -5 file.txt\n"
    "  head -v a.txt b.txt",
    "tail(1), cat(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", HEAD_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/head.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // stdin is pulled on demand through the streaming ABI, so fd 0 stays
  // untouched until (and unless) the command actually reads it.
  return winux::rust2::run_command_with_stdin_streaming("head", ctx.raw_args);
}
