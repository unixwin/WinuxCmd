// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for whereis (locate binary/source/manual
/// files).
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

auto constexpr WHEREIS_OPTIONS = std::array{
    OPTION("-b", "", "search only for binaries"),
    OPTION("-m", "", "search only for manual sections"),
    OPTION("-s", "", "search only for sources"),
    OPTION("-u", "", "search for unusual entries"),
    OPTION("-B", "", "define a binary lookup directory", STRING_TYPE),
    OPTION("-M", "", "define a manual lookup directory", STRING_TYPE),
    OPTION("-S", "", "define a source lookup directory", STRING_TYPE),
    OPTION("-f", "", "terminate the last directory list"),
    OPTION("-l", "", "list the effective lookup paths"),
    OPTION("-g", "", "search using glob patterns", BOOL_TYPE),
    OPTION("-h", "", "", BOOL_TYPE), OPTION("-V", "", "", BOOL_TYPE)};

REGISTER_COMMAND(
    whereis, "whereis", "whereis [OPTION]... NAME...",
    "Locate the binary, source, and manual-page files for each NAME.  By "
    "default all categories are searched in the standard locations (PATH "
    "for binaries).",
    "  whereis bash\n"
    "  whereis -b gcc\n"
    "  whereis -m ls",
    "which(1), find(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    WHEREIS_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/whereis.rs, whereis_cmd).
  return winux::rust2::run_command("whereis", ctx.raw_args);
}
