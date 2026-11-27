/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr TZSET_OPTIONS = std::array{
    // [EXT] option
    OPTION("", "--help", "output usage information and exit", BOOL_TYPE),
    // [EXT] option
    OPTION("-V", "--version", "output version information and exit",
           BOOL_TYPE)};

REGISTER_COMMAND(
    tzset, "tzset", "tzset [OPTION]",
    "Print POSIX-compatible timezone ID from current Windows timezone setting",
    "  tzset\n  export TZ=$(tzset)", "date", "WinuxCmd",
    "Copyright © 2026 WinuxCmd", TZSET_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/tzset.rs, tzset_cmd).
  return winux::rust2::run_command("tzset", ctx.raw_args);
}
