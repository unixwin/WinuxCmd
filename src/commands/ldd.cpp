// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Print PE import dependencies.
/// @Version: 0.2.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr LDD_OPTIONS = std::array{
    // [GNU] -d, --data-relocs: process data relocations
    OPTION("-d", "--data-relocs", "process data relocations"),
    // [GNU] -r, --function-relocs: process data and function relocations
    OPTION("-r", "--function-relocs", "process data and function relocations"),
    // [GNU]
    OPTION("-n", "--name", "print only imported DLL names"),
    // [GNU]
    OPTION("-u", "--unused", "print unused direct dependencies"),
    // [GNU]
    OPTION("-v", "--verbose", "print all information")};

REGISTER_COMMAND(ldd, "ldd", "ldd [OPTION]... FILE...",
                 "Print PE import dependencies for Windows executables.",
                 "  ldd winuxcmd.exe\n"
                 "  ldd --name winuxcmd.exe",
                 "pldd(1), file(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 LDD_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/ldd.rs, ldd_cmd).
  return winux::rust2::run_command("ldd", ctx.raw_args);
}
