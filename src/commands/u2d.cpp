// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for u2d command (alias for unix2dos).
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

auto constexpr U2D_OPTIONS = std::array{
    // [EXT]
    OPTION("-v", "--verbose", "print a message for each file", BOOL_TYPE)};

REGISTER_COMMAND(u2d,
                 /* name */
                 "u2d",

                 /* synopsis */
                 "u2d [OPTION]... [FILE]...",
                 "Convert Unix line endings to DOS line endings.\n"
                 "\n"
                 "Replace LF (\\n) with CRLF (\\r\\n) in each specified file.\n"
                 "\n"
                 "Options:\n"
                 "  -v, --verbose  print a message for each file",
                 "  u2d file.txt\n"
                 "  u2d -v *.txt\n"
                 "  cat unix_file.txt | u2d > dos_file.txt",

                 /* see also */
                 "unix2dos(1), dos2unix(1), d2u(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", U2D_OPTIONS) {
  if (ctx.positionals.empty()) {
    return winux::rust2::run_command_with_stdin("u2d", ctx.raw_args);
  }
  return winux::rust2::run_command("u2d", ctx.raw_args);
}
