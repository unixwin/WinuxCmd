// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for dirname.
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

// [GNU] -z, --zero: matches GNU coreutils dirname
// [GNU] -z, --zero: separate output with NUL rather than newline
auto constexpr DIRNAME_OPTIONS = std::array{OPTION(
    "-z", "--zero", "separate output with NUL rather than newline", BOOL_TYPE)};

REGISTER_COMMAND(
    dirname, "dirname", "dirname [OPTION] NAME...",
    "Output each NAME with its last non-slash component and trailing slashes\n"
    "removed; if NAME contains no /'s, output '.' (meaning the current "
    "directory).",
    "  dirname /path/to/file.txt\n"
    "  dirname dir1/file1 dir2/file2\n"
    "  dirname /foo/bar/",
    "basename(1), realpath(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    DIRNAME_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/dirname.rs, dirname_cmd); the C++ side
  // keeps only this registration. ctx.raw_args passes the operands
  // verbatim because GNU dirname.c parses its own getopt run; the result
  // line is DATA and reaches stdout through the bridge's raw
  // (non-translating) writers. GNU treats only '/' as a separator; the
  // old C++ body also stripped '\' — a documented deviation the library
  // does not inherit.
  return winux::rust2::run_command("dirname", ctx.raw_args);
}
