// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for dos2unix command.
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

auto constexpr DOS2UNIX_OPTIONS = std::array{
    // [EXT]
    OPTION("-v", "--verbose", "print a message for each file", BOOL_TYPE)};

REGISTER_COMMAND(dos2unix,
                 /* name */
                 "dos2unix",

                 /* synopsis */
                 "dos2unix [OPTION]... [FILE]...",
                 "Convert DOS line endings to Unix line endings.\n"
                 "\n"
                 "Replace CRLF (\\r\\n) with LF (\\n) in each specified file.\n"
                 "This is an alias for the d2u command.\n"
                 "\n"
                 "Options:\n"
                 "  -v, --verbose  print a message for each file",
                 "  dos2unix file.txt\n"
                 "  dos2unix -v *.txt\n"
                 "  cat dos_file.txt | dos2unix > unix_file.txt",

                 /* see also */
                 "unix2dos(1), d2u(1), u2d(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", DOS2UNIX_OPTIONS) {
  if (ctx.positionals.empty()) {
    return winux::rust2::run_command_with_stdin("dos2unix", ctx.raw_args);
  }
  return winux::rust2::run_command("dos2unix", ctx.raw_args);
}
