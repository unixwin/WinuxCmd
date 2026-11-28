// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for stdbuf command.
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

auto constexpr STDBUF_OPTIONS =
    std::array{OPTION("-i", "--input", "adjust standard input stream buffering",
                      STRING_TYPE),
               OPTION("-o", "--output",
                      "adjust standard output stream buffering", STRING_TYPE),
               OPTION("-e", "--error", "adjust standard error stream buffering",
                      STRING_TYPE)};

REGISTER_COMMAND(
    stdbuf, "stdbuf", "stdbuf OPTION... COMMAND",
    "Run COMMAND, with modified buffering operations for its standard "
    "streams.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "  -i, --input=MODE   adjust standard input stream buffering\n"
    "  -o, --output=MODE  adjust standard output stream buffering\n"
    "  -e, --error=MODE   adjust standard error stream buffering\n"
    "\n"
    "If MODE is 'L' the corresponding stream will be line buffered.\n"
    "This option is invalid with standard input.\n"
    "\n"
    "If MODE is '0' the corresponding stream will be unbuffered.\n"
    "\n"
    "Otherwise MODE is a number which may be followed by one of the "
    "following:\n"
    "KB 1000, K 1024, MB 1000*1000, M 1024*1024, and so on for G, T, P, E, Z, "
    "Y.",
    "  stdbuf -oL command\n"
    "  stdbuf -i0 -o0 -e0 command",
    "setvbuf(3)", "WinuxCmd", "Copyright © 2026 WinuxCmd", STDBUF_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/stdbuf.rs, stdbuf_cmd).
  return winux::rust2::run_command("stdbuf", ctx.raw_args);
}
