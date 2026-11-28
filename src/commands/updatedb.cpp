// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for updatedb (update the locate database).
/// @Version: 0.3.0
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

auto constexpr UPDATEDB_OPTIONS = std::array{
    OPTION("-d", "--directory", "directory to start walking", STRING_TYPE),
    OPTION("-e", "--exclude", "prune entries whose path contains this pattern",
           STRING_TYPE),
    OPTION("-f", "", "accepted for compatibility; ignored", BOOL_TYPE),
    OPTION("", "--localpaths",
           "walk these roots (semicolon separated) instead of --directory",
           STRING_TYPE),
    OPTION("-o", "--output", "database output file", STRING_TYPE),
    OPTION("", "--prunepaths",
           "directories (semicolon separated) not to descend into",
           STRING_TYPE),
    OPTION("-s", "", "accepted for compatibility; ignored", BOOL_TYPE),
    OPTION("-v", "--verbose", "be verbose", BOOL_TYPE),
    OPTION("-x", "--exclude-dir", "directory name pattern to exclude",
           STRING_TYPE)};

REGISTER_COMMAND(updatedb, "updatedb", "updatedb [OPTION]...",
                 "Update the filename database used by locate.\n"
                 "Walks the filesystem and writes a sorted path list.\n"
                 "The database is written to a temporary file and renamed\n"
                 "into place, replacing any previous database atomically.",
                 "  updatedb\n"
                 "  updatedb -d /path\n"
                 "  updatedb --localpaths='C:/Users;D:/data' "
                 "--prunepaths='C:/Users/Default'\n"
                 "  updatedb -d C:\\Windows -v",
                 "locate(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 UPDATEDB_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/updatedb.rs, updatedb_cmd).
  return winux::rust2::run_command("updatedb", ctx.raw_args);
}
