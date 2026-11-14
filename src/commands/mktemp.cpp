// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for mktemp.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr MKTEMP_OPTIONS = std::array{
    // [GNU]
    OPTION("-d", "--directory", "make a directory instead of a file",
           BOOL_TYPE),
    // [GNU]
    OPTION("-u", "--dry-run", "do not actually create anything", BOOL_TYPE),
    // [GNU]
    OPTION("-q", "--quiet", "suppress diagnostics", BOOL_TYPE),
    // [GNU]
    OPTION("", "--suffix",
           "append SUFFIX to TEMPLATE; SUFFIX must not contain a path "
           "separator",
           STRING_TYPE),
    // [GNU]
    OPTION("-t", "",
           "interpret TEMPLATE as a single file name component, relative to "
           "a directory"),
    // [GNU]
    OPTION("-p", "--tmpdir",
           "interpret TEMPLATE relative to DIR; if DIR is not specified, use "
           "$TMPDIR",
           STRING_TYPE)};

REGISTER_COMMAND(
    mktemp, "mktemp", "mktemp [OPTION]... [TEMPLATE]",
    "Create a temporary file or directory, safely, and print its name.\n"
    "\n"
    "TEMPLATE must contain at least 3 consecutive 'X's in last component.\n"
    "If TEMPLATE is not specified, use tmp.XXXXXXXXXX instead.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This implementation uses Windows GetTempFileName API.\n"
    "Custom templates are partially supported.",
    "  mktemp                          # create temp file\n"
    "  mktemp -d                        # create temp directory\n"
    "  mktemp -u                        # just print name\n"
    "  mktemp -t /tmp                   # use specific directory\n"
    "  mktemp --quiet                   # suppress errors",
    "tempfile(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", MKTEMP_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/mktemp.rs); the C++ side keeps only this
  // registration. ctx.raw_args passes the operands verbatim because the GNU
  // applet parses its own argv.
  return winux::rust2::run_command("mktemp", ctx.raw_args);
}
