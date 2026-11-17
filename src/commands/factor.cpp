// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for factor command.
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

auto constexpr FACTOR_OPTIONS = std::array{
    // [GNU] -h is the GNU spelling of --exponents; -e is a documented
    // WinuxCmd alias (GNU factor 9.x rejects -e).
    OPTION("-e", "--exponents",
           "print repeated factors in form p^e unless e is 1"),
    OPTION("-h", "--exponents",
           "print repeated factors in form p^e unless e is 1")};

REGISTER_COMMAND(factor_cmd,
                 /* name */
                 "factor",

                 /* synopsis */
                 "factor [OPTION]... [NUMBER]...",
                 "Print the prime factors of each specified integer NUMBER.\n"
                 "\n"
                 "If no NUMBER is specified, read from standard input.\n"
                 "\n"
                 "  -h, --exponents   print repeated factors in form p^e "
                 "unless e is 1\n"
                 "  -e                same as --exponents\n"
                 "      --help        display this help and exit\n"
                 "      --version     output version information and exit",
                 "  factor 12\n"
                 "  factor 100\n"
                 "  factor -h 72\n"
                 "  factor --exponents 17\n"
                 "  echo '24' | factor",

                 /* see also */
                 "primes(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FACTOR_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/factor.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // factor reads stdin only when no NUMBER operates: the streaming entry
  // leaves fd 0 untouched for operand-only runs and pulls lines on demand
  // otherwise.
  return winux::rust2::run_command_with_stdin_streaming("factor", ctx.raw_args);
}
