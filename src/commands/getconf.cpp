// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for getconf command.
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

auto constexpr GETCONF_OPTIONS =
    // [GNU]
    std::array{OPTION("-a", "--all", "display all configuration variables"),
               // [GNU] -v SPEC: give values for compilation environment SPEC
               OPTION("-v", "", "give values for compilation environment SPEC",
                      STRING_TYPE),
               // [GNU]
               OPTION("", "", "get system configuration values", STRING_TYPE)};

REGISTER_COMMAND(
    getconf,
    /* name */
    "getconf",

    /* synopsis */
    "getconf [OPTION]... [VARIABLE_NAME]...\n"
    "getconf [OPTION]... PATH_VAR PATH",
    "Display configuration variable values.\n"
    "\n"
    "With two operands, the first is a path variable and the second a\n"
    "path it is evaluated for. With no VARIABLE_NAME (and no -a) this\n"
    "is a usage error, as in glibc getconf.\n"
    "On Windows, this provides limited system configuration information.\n"
    "\n"
    "Options:\n"
    "  -a, --all  display all configuration variables\n"
    "  -v SPEC    give values for compilation environment SPEC",
    "  getconf\n"
    "  getconf PATH_MAX\n"
    "  getconf -a",

    /* see also */
    "sysconf(3)", "WinuxCmd", "Copyright © 2026 WinuxCmd", GETCONF_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/getconf.rs, getconf_cmd); the C++ side
  // keeps only this registration — the Windows variable table
  // (#1134 ARG_MAX, MAX_PATH, SYSTEM_INFO page/CPU/memory values) is
  // deleted. ctx.raw_args passes the operands verbatim because glibc
  // getconf parses its own option run (-a, -v SPEC, pathconf form).
  return winux::rust2::run_command("getconf", ctx.raw_args);
}
