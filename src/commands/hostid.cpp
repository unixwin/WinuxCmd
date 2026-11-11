// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for hostid command.
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

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr HOSTID_OPTIONS =
    // [DIFFERS]
    std::array{OPTION("", "", "print machine identifier", STRING_TYPE)};

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    hostid,
    /* cmd_name */ "hostid",
    /* cmd_synopsis */ "hostid [OPTION]",
    /* cmd_desc */
    "Print the numeric identifier of the current host.\n"
    "Print a 32-bit identifier, in hexadecimal, for the current host.\n"
    "On Windows, this is derived from the machine GUID.",
    /* examples */
    "  hostid\n"
    "  hostid | xxd -r -p",
    /* see_also */ "hostname, whoami",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ HOSTID_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/hostid.rs, hostid_cmd); the C++ side keeps
  // only this registration — the MachineGuid registry read and the hash
  // are deleted. The Windows value model follows the rust2 library (stable
  // per-machine id over the machine identity the environment carries), so
  // the printed id may differ from the 1.x exe while staying stable and
  // GNU-shaped; the known 1.x divergences (hash input) are not inherited.
  return winux::rust2::run_command("hostid", ctx.raw_args);
}
