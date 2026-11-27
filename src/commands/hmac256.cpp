// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for hmac256.
/// @Version: 0.2.0
/// @License: MIT

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr HMAC256_OPTIONS =
    // [EXT]
    std::array{OPTION("", "", "key string", STRING_TYPE)};

REGISTER_COMMAND(hmac256,
                 /* name */
                 "hmac256",

                 /* synopsis */
                 "hmac256 [KEY] [FILE]",

                 /* description */
                 "Compute HMAC-SHA256 checksum.",

                 /* examples */
                 "  hmac256 secret file.txt",

                 /* see_also */
                 "sha256sum(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 HMAC256_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/hmac256.rs, hmac256_cmd).
  return winux::rust2::run_command_with_stdin("hmac256", ctx.raw_args);
}
