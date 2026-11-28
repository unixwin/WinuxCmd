// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for mkpasswd.
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

auto constexpr MKPASSWD_OPTIONS = std::array{
    // [EXT]
    OPTION("-c", "--current", "print only the current process user"),
    // [EXT] [DIFFERS] GNU mkpasswd: -l means password length; here it means
    // local users
    OPTION("-l", "--local", "enumerate local machine users (default)"),
    // [EXT]
    OPTION("-p", "--path-prefix", "home directory prefix", STRING_TYPE),
    // [EXT] [DIFFERS] GNU mkpasswd: -s means salt; here it means login shell
    OPTION("-s", "--shell", "login shell path", STRING_TYPE),
    // [DIFFERS] GNU mkpasswd: read password from file descriptor; not
    // applicable on Windows
    OPTION("-P", "--password-fd", "read password from file descriptor",
           STRING_TYPE),
    // [DIFFERS] GNU mkpasswd: encrypt method (des,md5,sha-256,sha-512); not
    // applicable on Windows
    OPTION("-S", "--method", "encrypt method (not supported on Windows)",
           STRING_TYPE)};

REGISTER_COMMAND(
    mkpasswd, "mkpasswd",
    "mkpasswd [-c] [-l] [-p PREFIX] [-s SHELL] [-P FD] [-S METHOD]",
    "Generate passwd-like entries from Windows accounts.",
    "  mkpasswd --current\n"
    "  mkpasswd --path-prefix /home --shell /bin/sh\n"
    "  mkpasswd -P 3 -S sha-512  # [DIFFERS] not supported",
    "id(1), whoami(1), mkgroup(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    MKPASSWD_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/mkpasswd.rs, mkpasswd_cmd).
  return winux::rust2::run_command("mkpasswd", ctx.raw_args);
}
