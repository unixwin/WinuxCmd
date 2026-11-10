// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 arookieofc <2128194521@qq.com>
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for pwd.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright  2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr PWD_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-L", "--logical",
           "use PWD from environment when it names the current directory"),
    // [DIFFERS]
    OPTION("-P", "--physical", "avoid all symlinks")};

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    pwd,
    /* name */
    "pwd",

    /* synopsis */
    "print name of current/working directory",

    /* description */
    "Print the full filename of the current working directory.\n"
    "\n"
    "The -L option uses PWD from environment when it still names the current\n"
    "directory.\n"
    "The -P option avoids all symlinks.",

    /* examples */
    "  pwd                  Print the current working directory\n"
    "  pwd -L               Print logical current directory\n"
    "  pwd -P               Print physical current directory",

    /* see also */
    "cd(1), ls(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright  2026 WinuxCmd",

    /* options */
    PWD_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/pwd.rs, pwd_cmd); the C++ side keeps only
  // this registration. The handler receives ctx.raw_args — the operands
  // verbatim, before the metadata option parse — because GNU pwd.c parses
  // its own argv (POSIXLY_CORRECT flips the default; -L validates $PWD
  // against the same directory and falls back silently).
  return winux::rust2::run_command("pwd", ctx.raw_args);
}
