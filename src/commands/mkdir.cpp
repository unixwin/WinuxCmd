// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Implementation for mkdir.
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

// clang-format off
auto constexpr MKDIR_OPTIONS =
    std::array{
               // [DIFFERS]
               OPTION("-m", "--mode", "set file mode (as in chmod), not a=rwx - umask", STRING_TYPE),
               // [GNU]
               OPTION("-p", "--parents", "no error if existing, make parent directories as needed"),
               // [GNU]
               OPTION("-v", "--verbose", "print a message for each created directory"),
               // [DIFFERS] SELinux contexts are not available on Windows; accepted as a no-op.
               OPTION("-Z", "--context", "set SELinux security context of each created directory (ignored on Windows)", OPTIONAL_STRING_TYPE)};
// clang-format on

REGISTER_COMMAND(
    mkdir,
    /* cmd_name */ "mkdir",
    /* cmd_synopsis */ "make directories",
    /* cmd_desc */
    "Create the DIRECTORY(ies), if they do not already exist.\n"
    "\n"
    "If the directory already exists, mkdir will fail unless the -p option is "
    "used.\n"
    "With -p, mkdir will create parent directories as needed.\n",
    /* examples */
    "  mkdir dir1                Create directory dir1\n"
    "  mkdir -p dir1/dir2/dir3    Create nested directories\n"
    "  mkdir -v dir1 dir2         Verbose create directories\n"
    "  mkdir -pv dir1/dir2        Verbose create nested directories",
    /* see_also */ "rmdir(1), rm(1), cp(1), mv(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    MKDIR_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/mkdir.rs, mkdir_cmd); the C++ side keeps
  // only this registration — the creation pipeline and the mode mapping
  // are deleted. ctx.raw_args passes the operands verbatim because GNU
  // mkdir.c parses its own getopt run; created-directory messages are
  // DATA and reach stdout through the bridge's raw (non-translating)
  // writers.
  return winux::rust2::run_command("mkdir", ctx.raw_args);
}
