// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for [ command (alias for test).
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

// Shared GNU-faithful test/[ parser and stat() helpers (issues 236, 963,
// 1063).  Must come after `import utils;` for native_path::.
#include "commands/test_expr.hpp"

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr BRACKET_OPTIONS =
    // [GNU] -n
    std::array{
        OPTION("-n", "", "string length is non-zero"),
        // [DIFFERS] Windows exposes last-access timestamps, but semantics
        // depend on system policy.
        OPTION("-N", "",
               "file exists and has been modified since it was last read"),
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
        // [DIFFERS] Unix effective group IDs have no Windows equivalent.
        OPTION("-G", "",
               "file is owned by effective group ID (unsupported on Windows)"),
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
        // [DIFFERS] Windows sockets are not filesystem socket nodes.
        OPTION(
            "-S", "",
            "file is a socket (unsupported for filesystem paths on Windows)"),
        // [GNU] -t
        OPTION("-t", "", "file descriptor is a terminal"),
        // [GNU] -u
        OPTION("-u", "", "file has set-user-ID bit"),
        // [GNU] -w
        OPTION("-w", "", "file is writable"),
        // [GNU] -x
        OPTION("-x", "", "file is executable"),
        // [DIFFERS] Unix effective user IDs have no direct Windows equivalent.
        OPTION("-O", "",
               "file is owned by effective user ID (unsupported on Windows)"),
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
        // [GNU] -o
        OPTION("-o", "", "logical or"),
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
        OPTION("-ef", "", "file1 and file2 have the same device and inode")};

namespace bracket_command {
auto materialize_raw_args(const std::vector<std::string_view>& raw_args)
    -> std::vector<std::string> {
  std::vector<std::string> args;
  args.reserve(raw_args.size());
  for (auto arg : raw_args) args.emplace_back(arg);
  return args;
}

auto evaluate_bracket_expression(std::span<const std::string> args,
                                 std::string* error_message) -> int {
  // [GNU] `[` shares test's parser exactly (test_expr.hpp), mirroring
  // coreutils test.c semantics and diagnostics.
  test_expr::Parser parser(args);
  const int status = parser.parse();
  if (error_message != nullptr) {
    *error_message = parser.error();
  }
  return status;
}
}  // namespace bracket_command

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(bracket,
                 /* cmd_name */ "[",
                 /* cmd_synopsis */ "[ expression",
                 /* cmd_desc */
                 "Evaluate conditional expression (requires closing ]).",
                 /* examples */
                 "  [ -f /etc/passwd ]\n"
                 "  [ -n \"$var\" ]\n"
                 "  [ \"$a\" -eq \"$b\" ]\n"
                 "  [ -d /tmp ] && echo 'Directory exists'",
                 /* see_also */ "test, bash",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ BRACKET_OPTIONS) {
  auto args = bracket_command::materialize_raw_args(ctx.raw_args);
  if (args.empty() || args.back() != "]") {
    safeErrorPrintLn("[: missing ']'");
    return 2;
  }

  args.pop_back();
  std::string error_message;
  const int status =
      bracket_command::evaluate_bracket_expression(args, &error_message);
  if (!error_message.empty()) {
    safeErrorPrintLn("[: " + winux::i18n::translate_error(error_message));
  }
  return status;
}
