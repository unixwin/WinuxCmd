// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for unix2dos command.
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

auto constexpr UNIX2DOS_OPTIONS = std::array{
    // [EXT] option
    OPTION("-v", "--verbose", "print a message for each file", BOOL_TYPE)};

REGISTER_COMMAND(unix2dos,
                 /* name */
                 "unix2dos",

                 /* synopsis */
                 "unix2dos [OPTION]... [FILE]...",
                 "Convert Unix line endings to DOS line endings.\n"
                 "\n"
                 "Replace LF (\\n) with CRLF (\\r\\n) in each specified file.\n"
                 "This is an alias for the u2d command.\n"
                 "\n"
                 "Options:\n"
                 "  -v, --verbose  print a message for each file",
                 "  unix2dos file.txt\n"
                 "  unix2dos -v *.txt\n"
                 "  cat unix_file.txt | unix2dos > dos_file.txt",

                 /* see also */
                 "dos2unix(1), d2u(1), u2d(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", UNIX2DOS_OPTIONS) {
  if (ctx.positionals.empty()) {
    return winux::rust2::run_command_with_stdin("unix2dos", ctx.raw_args);
  }
  return winux::rust2::run_command("unix2dos", ctx.raw_args);
}
