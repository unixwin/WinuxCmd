// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for chattr.
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

auto constexpr CHATTR_OPTIONS = std::array{
    // [EXT]
    OPTION("-R", "--recursive", "recursively change attributes of directories"),
    // [EXT]
    OPTION("-V", "--verbose", "print changed files"),
    // [GNU]
    OPTION("-f", "--force", "suppress most error messages")};

REGISTER_COMMAND(
    chattr, "chattr", "chattr [-RVf] MODE FILE...",
    "Change Windows file attributes.\n"
    "Supported flags are R,H,S,A,I,T,O for readonly, hidden, "
    "system, archive, not indexed, temporary, and offline.\n"
    "[DIFFERS] Attribute flags differ from GNU chattr (Linux ext4).",
    "  chattr +RH file.txt\n"
    "  chattr -R -H directory",
    "lsattr(1), chmod(1), attrib.exe", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    CHATTR_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/chattr.rs, chattr_cmd).
  return winux::rust2::run_command("chattr", ctx.raw_args);
}
