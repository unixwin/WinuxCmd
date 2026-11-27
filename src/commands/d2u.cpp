// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for d2u command (alias for dos2unix).
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

auto constexpr D2U_OPTIONS = std::array{
    // [EXT]
    OPTION("-v", "--verbose", "print a message for each file", BOOL_TYPE)};

REGISTER_COMMAND(d2u,
                 /* name */
                 "d2u",

                 /* synopsis */
                 "d2u [OPTION]... [FILE]...",
                 "Convert DOS line endings to Unix line endings.\n"
                 "\n"
                 "Replace CRLF (\\r\\n) with LF (\\n) in each specified file.\n"
                 "\n"
                 "Options:\n"
                 "  -v, --verbose  print a message for each file",
                 "  d2u file.txt\n"
                 "  d2u -v *.txt\n"
                 "  cat dos_file.txt | d2u > unix_file.txt",

                 /* see also */
                 "dos2unix(1), unix2dos(1), u2d(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", D2U_OPTIONS) {
  if (ctx.positionals.empty()) {
    return winux::rust2::run_command_with_stdin("d2u", ctx.raw_args);
  }
  return winux::rust2::run_command("d2u", ctx.raw_args);
}
