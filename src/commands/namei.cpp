// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for namei (display path resolution
///     steps), util-linux 2.39.3 misc-utils/namei.c option set.
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

auto constexpr NAMEI_OPTIONS = std::array{
    OPTION("-l", "--long", "long format (modes, owners, vertical)"),
    OPTION("-m", "--modes", "show file modes in rwx form"),
    OPTION("-n", "--nosymlinks", "do not follow symbolic links"),
    OPTION("-o", "--owners", "show owner and group of each component"),
    OPTION("-v", "--vertical", "show the path components vertically"),
    OPTION("-x", "--mountpoints", "show mount point directories with a 'D'"),
    OPTION("-Z", "--context", "print the security context"),
    // [GNU] -V: version shorthand (probed: `namei -V`).
    OPTION("-V", "--version", "output version information and exit")};

REGISTER_COMMAND(namei, "namei", "namei [OPTION]... PATH...",
                 "Call namei on each PATH, showing any symlink resolution.\n"
                 "Displays the resolution steps for each path component.",
                 "  namei /usr/bin/ls\n"
                 "  namei -l /path/to/file",
                 "readlink(1), realpath(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", NAMEI_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/namei.rs, namei_cmd).
  return winux::rust2::run_command("namei", ctx.raw_args);
}
