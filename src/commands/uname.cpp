// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for uname.
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

auto constexpr UNAME_OPTIONS = std::array{
    // [DIFFERS] option
    OPTION("-a", "--all", "print all information", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-s", "--kernel-name", "print the kernel name", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-n", "--nodename", "print the network node hostname", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-r", "--kernel-release", "print the kernel release", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-v", "--kernel-version", "print the kernel version", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-m", "--machine", "print the machine hardware name", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-p", "--processor", "print the processor type", BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-i", "--hardware-platform", "print the hardware platform",
           BOOL_TYPE),
    // [DIFFERS] option
    OPTION("-o", "--operating-system", "print the operating system", BOOL_TYPE),
    // [GNU] --sysname: obsolescent alias for -s/--kernel-name; hidden
    OPTION("", "--sysname", "", BOOL_TYPE),
    // [GNU] --release: obsolescent alias for -r/--kernel-release; hidden
    OPTION("", "--release", "", BOOL_TYPE)};

REGISTER_COMMAND(
    uname, "uname", "uname [OPTION]...",
    "Print certain system information.\n"
    "\n"
    "With no OPTION, same as -s.\n"
    "\n"
    "Note: This implementation provides Windows-specific information.",
    "  uname -a\n"
    "  uname -m\n"
    "  uname -r\n"
    "  uname -n",
    "arch(1), hostname(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    UNAME_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/uname.rs, uname_cmd); the C++ side keeps
  // only this registration — the RtlGetVersion/GetNativeSystemInfo value
  // pipeline is deleted. ctx.raw_args passes the operands verbatim because
  // coreutils uname.c parses its own option run (extra operand, #1215).
  return winux::rust2::run_command("uname", ctx.raw_args);
}
