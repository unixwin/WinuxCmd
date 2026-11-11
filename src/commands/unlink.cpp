// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for unlink command.
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

auto constexpr UNLINK_OPTIONS =
    // [GNU] option
    std::array{OPTION("", "", "remove single file", STRING_TYPE)};

REGISTER_COMMAND(unlink,
                 /* name */
                 "unlink",

                 /* synopsis */
                 "unlink FILE",
                 "Remove a specified file.\n"
                 "\n"
                 "Unlink the file named FILE. If FILE is a symbolic link, the "
                 "symbolic link\n"
                 "is removed, not the file it points to.",
                 "  unlink file.txt",

                 /* see also */
                 "rm(1), link(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 UNLINK_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/unlink.rs, unlink_cmd); the C++ side keeps
  // only this registration — the DeleteFileW/RemoveDirectoryW removal path
  // is deleted. ctx.raw_args passes the operands verbatim because GNU
  // unlink.c parses its own argv (exactly one operand, no options).
  return winux::rust2::run_command("unlink", ctx.raw_args);
}
