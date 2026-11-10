// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for test command.
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

// ======================================================
// Options (constexpr)
// Note: For test command, all operators like -f, -d, -e need to be listed
// in OPTIONS so the parser recognizes them, even though they're treated
// as positionals in the handler logic.
// ======================================================

auto constexpr TEST_OPTIONS =
    // [GNU] -n
    std::array{
        OPTION("-n", "", "string length is non-zero"),
        // [GNU] -z
        OPTION("-z", "", "string length is zero"),
        // [GNU] -b
        OPTION("-b", "", "file is block special"),
        // [GNU] -c
        OPTION("-c", "", "file is character special"),
        // [GNU] -d
        OPTION("-d", "", "file is a directory"),
        // [GNU] -e
        OPTION("-e", "", "file exists"),
        // [GNU] -f
        OPTION("-f", "", "file is a regular file"),
        // [GNU] -g
        OPTION("-g", "", "file has set-group-ID bit"),
        // [GNU] -G
        OPTION("-G", "", "file is owned by effective group ID"),
        // [GNU] -h
        OPTION("-h", "", "file is a symbolic link"),
        // [GNU] -L
        OPTION("-L", "", "file is a symbolic link"),
        // [GNU] -k
        OPTION("-k", "", "file has sticky bit"),
        // [GNU] -p
        OPTION("-p", "", "file is a named pipe"),
        // [GNU] -r
        OPTION("-r", "", "file is readable"),
        // [GNU] -s
        OPTION("-s", "", "file size is non-zero"),
        // [GNU] -S
        OPTION("-S", "", "file is a socket"),
        // [GNU] -t
        OPTION("-t", "", "file descriptor is a terminal"),
        // [GNU] -u
        OPTION("-u", "", "file has set-user-ID bit"),
        // [GNU] -w
        OPTION("-w", "", "file is writable"),
        // [GNU] -x
        OPTION("-x", "", "file is executable"),
        // [GNU] -O
        OPTION("-O", "", "file is owned by effective user ID"),
        // [GNU] -eq
        OPTION("-eq", "", "integer equal"),
        // [GNU] -ne
        OPTION("-ne", "", "integer not equal"),
        // [GNU] -lt
        OPTION("-lt", "", "integer less than"),
        // [GNU] -le
        OPTION("-le", "", "integer less than or equal"),
        // [GNU] -gt
        OPTION("-gt", "", "integer greater than"),
        // [GNU] -ge
        OPTION("-ge", "", "integer greater than or equal"),
        // [GNU] -a
        OPTION("-a", "", "logical and"),
        // [EXT] -and
        OPTION("-and", "", "logical and"),
        // [GNU] -o
        OPTION("-o", "", "logical or"),
        // [EXT] -or
        OPTION("-or", "", "logical or"),
        // [GNU] !
        OPTION("!", "", "logical not"),
        // [GNU] =
        OPTION("=", "", "string equal"),
        // [GNU] ==
        OPTION("==", "", "string equal"),
        // [GNU] !=
        OPTION("!=", "", "string not equal"),
        // [GNU] -nt
        OPTION("-nt", "", "file1 is newer than file2"),
        // [GNU] -ot
        OPTION("-ot", "", "file1 is older than file2"),
        // [GNU] -ef
        OPTION("-ef", "", "file1 and file2 have the same device and inode"),
        // [GNU] -N
        OPTION("-N", "",
               "file exists and has been modified since it was last "
               "read")};

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    test,
    /* cmd_name */ "test",
    /* cmd_synopsis */ "test [EXPRESSION]",
    /* cmd_desc */
    "Evaluate conditional expression.\n"
    "Return exit status of 0 or 1 depending on evaluation of conditional\n"
    "expression EXPRESSION. Exits with status 0 if EXPRESSION is true; 1\n"
    "if false; 2 if an error occurred.",
    /* examples */
    "  test -f /etc/passwd\n"
    "  test -n \"$var\"\n"
    "  [ -d /tmp ]\n"
    "  test \"$a\" -eq \"$b\"",
    /* see_also */ "[, bash",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ TEST_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/test_cmd.rs, test_cmd); the C++ side
  // keeps only this registration — the shared test_expr.hpp parser and
  // its stat() helpers are deleted with it. test/[ never call getopt:
  // the dispatcher honors --help/--version only as the sole argument and
  // every other token is an operand, so ctx.raw_args is exactly the
  // expression language GNU test.c parses.
  return winux::rust2::run_command("test", ctx.raw_args);
}
