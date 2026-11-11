// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for users.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h

#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr USERS_OPTIONS = std::array{
    // [EXT] option
    OPTION("", "", "print who is currently logged in", STRING_TYPE)
    // users has no options
};

REGISTER_COMMAND(
    users, "users", "users [OPTION]...",
    "Print the user names of users currently logged in to the current host.",
    "  users", "who(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    USERS_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/users.rs, users_cmd); the C++ side keeps
  // only this registration — the WTSEnumerateSessionsW session walk, the
  // GetUserNameW fallback and the operand checks are deleted. The printed
  // user list follows the rust2 Windows value model, so the WTS-vertex
  // quirks of the 1.x exe (session-state filter) are not inherited; the
  // GNU-shaped rc errors (extra operand, missing FILE) move to Rust.
  return winux::rust2::run_command("users", ctx.raw_args);
}
