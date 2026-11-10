// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for basename.
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

auto constexpr BASENAME_OPTIONS = std::array{
    // [GNU]
    OPTION("-a", "--multiple",
           "support multiple arguments and treat each as a NAME", BOOL_TYPE),
    // [GNU]
    OPTION("", "--mul", "alias for --multiple", BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--suffix", "remove a trailing SUFFIX; implies -a",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--suf", "alias for --suffix", STRING_TYPE),
    // [GNU]
    OPTION("-z", "--zero", "end each output line with NUL, not newline",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--ze", "alias for --zero", BOOL_TYPE)};

REGISTER_COMMAND(basename, "basename",
                 "basename NAME [SUFFIX]\n"
                 "basename OPTION... NAME...",
                 "Print NAME with any leading directory components removed.\n"
                 "If specified, also remove a trailing SUFFIX.",
                 "  basename /path/to/file.txt\n"
                 "  basename /path/to/file.txt .txt\n"
                 "  basename -a file1.txt file2.txt\n"
                 "  basename -s .txt file1 file2",
                 "dirname(1), realpath(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", BASENAME_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/basename.rs, basename_cmd); the C++ side
  // keeps only this registration. ctx.raw_args passes the operands
  // verbatim because GNU basename.c parses its own getopt run. The result
  // line is DATA and reaches stdout through the bridge's raw
  // (non-translating) writers. GNU treats only '/' as a separator; the
  // old C++ body also stripped '\' — a documented deviation the library
  // does not inherit.
  return winux::rust2::run_command("basename", ctx.raw_args);
}
