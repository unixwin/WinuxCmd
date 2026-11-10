// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for rmdir.
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

// [GNU] --ignore-fail-on-non-empty: matches GNU coreutils rmdir
// [GNU] -p, --parents: matches GNU coreutils rmdir
// [GNU] -v, --verbose: matches GNU coreutils rmdir
auto constexpr RMDIR_OPTIONS =
    std::array{OPTION("", "--ignore-fail-on-non-empty",
                      "ignore each failure to remove a non-empty directory"),
               OPTION("-p", "--parents", "remove DIRECTORY and its ancestors"),
               // [GNU] --path: deprecated alias for -p/--parents; hidden
               OPTION("", "--path", "", BOOL_TYPE),
               OPTION("-v", "--verbose",
                      "output a diagnostic for every directory processed")};

REGISTER_COMMAND(rmdir, "rmdir", "rmdir [OPTION]... DIRECTORY...",
                 "Remove the DIRECTORY(ies), if they are empty.",
                 "  rmdir dir\n"
                 "  rmdir -p a/b/c\n"
                 "  rmdir --ignore-fail-on-non-empty dir",
                 "mkdir(1), rm(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 RMDIR_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/rmdir.rs, rmdir_cmd); the C++ side keeps
  // only this registration — the removal pipeline and the -p ancestor
  // walk are deleted. ctx.raw_args passes the operands verbatim because
  // GNU rmdir.c parses its own getopt run; verbose diagnostics are DATA
  // and reach stdout/stderr through the bridge's raw (non-translating)
  // writers.
  return winux::rust2::run_command("rmdir", ctx.raw_args);
}
