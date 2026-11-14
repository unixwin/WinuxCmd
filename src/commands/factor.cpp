// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for factor command.
/// @Version: 0.3.0
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
    // [GNU] -h is the GNU spelling of --exponents. The 1.x `-e` alias is
    // RETIRED with this wiring: GNU factor 9.x has no -e (it is an invalid
    // option, rc 1), and the rust2 body implements the GNU grammar only.
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
  // (rust2/crates/winux-cmds/src/factor.rs, factor_cmd — the probed
  // coreutils 9.4 operand grammar, Miller-Rabin + Pollard-Brent for the
  // 0..=u64::MAX range, the documented beyond-u64 subset). The C++ side
  // keeps only this registration.
  //
  // stdin shape: GNU factor reads fd 0 only when NO operand stands (a "-"
  // operand is an invalid operand, not stdin). The entry is chosen
  // unconditionally because the v2 streaming form is a lazy PULL callback:
  // when the command never reads, fd 0 stays untouched for the next
  // pipeline stage, so the operand shape needs no branch — and the command
  // keeps its own GNU read-error shape instead of the v1 slurp pre-check.
  return winux::rust2::run_command_with_stdin_streaming("factor", ctx.raw_args);
}
