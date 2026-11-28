// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for lsattr.
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

auto constexpr LSATTR_OPTIONS = std::array{
    // [EXT]
    OPTION("-R", "--recursive", "recursively list attributes of directories"),
    // [EXT]
    OPTION("-d", "--directory", "list directories themselves, not contents"),
    // [GNU]
    OPTION("-a", "--all", "list all files including those starting with '.'"),
    // [GNU]
    OPTION("-V", "--version", "display program version"),
    // [GNU]
    OPTION("-l", "--long",
           "print options using long names instead of abbreviations")};

REGISTER_COMMAND(
    lsattr, "lsattr", "lsattr [-RVadl] [FILE]...",
    "List Windows file attributes in a stable flag string.\n"
    "Flags are RHSADCEITOPL for readonly, hidden, system, "
    "archive, directory, compressed, encrypted, not indexed, "
    "temporary, offline, sparse, and reparse point.\n"
    "[DIFFERS] Attribute flags differ from GNU lsattr (Linux ext4).",
    "  lsattr file.txt\n"
    "  lsattr -d directory",
    "chattr(1), stat(1), attrib.exe", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    LSATTR_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/chattr.rs, lsattr_cmd).
  return winux::rust2::run_command("lsattr", ctx.raw_args);
}
