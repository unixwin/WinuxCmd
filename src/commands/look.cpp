// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for look (display lines beginning with a
/// string).
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr LOOK_OPTIONS = std::array{
    OPTION("-d", "--alphanum", "compare only alphanumeric and blanks"),
    OPTION("-f", "--ignore-case", "ignore case when comparing"),
    OPTION("-a", "--alternative", "use alternative dictionary order"),
    OPTION("-t", "--terminate", "terminate string at CHAR", STRING_TYPE),
    // [GNU] -V, --version (util-linux look probes: `look -V` prints version)
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(
    look, "look", "look [OPTION]... STRING [FILE]",
    "Display lines beginning with STRING.  Performs a binary search, so "
    "FILE must be sorted.  With no FILE, a default dictionary is used "
    "(not available on Windows, so an explicit FILE is required).",
    "  look apple words.txt\n"
    "  look -f apple words.txt",
    "grep(1), sort(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", LOOK_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/look.rs, look_cmd).
  return winux::rust2::run_command("look", ctx.raw_args);
}
