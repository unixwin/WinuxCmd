// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for locate (find files by name in database).
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

auto constexpr LOCATE_OPTIONS = std::array{
    OPTION("-0", "--null", "separate output entries with NUL, not newline"),
    OPTION("-A", "--all", "only print entries that match all patterns"),
    OPTION("-b", "--basename", "match on filename, not full path"),
    OPTION("-c", "--count", "count matching entries"),
    OPTION("-d", "--database", "use given database", STRING_TYPE),
    OPTION("-e", "--existing", "print only entries that exist on disk"),
    OPTION("-E", "--non-existing",
           "print only entries that do not exist on disk"),
    OPTION("-i", "--ignore-case", "ignore case in search"),
    OPTION("-l", "--limit", "limit results to at most N entries", UINT_TYPE),
    OPTION("-L", "--follow",
           "accepted for GNU compatibility; the database stores "
           "plain paths and never follows links"),
    OPTION("-m", "--mmap", "accepted for GNU compatibility; ignored"),
    OPTION("-P", "--nofollow",
           "accepted for GNU compatibility; the database stores "
           "plain paths and never follows links"),
    OPTION("-p", "--print", "print matches (default)"),
    OPTION("-r", "--regex",
           "treat patterns as POSIX basic regular expressions"),
    OPTION("-S", "--statistics", "print database statistics"),
    OPTION("-s", "--stdio", "accepted for GNU compatibility; ignored"),
    OPTION("-w", "--wholename",
           "match patterns against the whole path (default)")};

REGISTER_COMMAND(locate, "locate", "locate [OPTION]... PATTERN...",
                 "Search a database of file paths for matching names.\n"
                 "Run 'updatedb' first to build the database.\n"
                 "When a pattern contains any of *?[]\\ it is matched as a\n"
                 "shell glob against whole paths; otherwise a substring\n"
                 "search is performed.",
                 "  locate myprogram\n"
                 "  locate -i readme\n"
                 "  locate -c -i test\n"
                 "  locate -l 5 '*.cpp'\n"
                 "  locate -r '/doc.*\\.md$'",
                 "updatedb(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 LOCATE_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/locate.rs, locate_cmd).
  return winux::rust2::run_command("locate", ctx.raw_args);
}
