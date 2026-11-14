// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sum.
/// @Version: 0.1.0
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

auto constexpr SUM_OPTIONS = std::array{
    // [GNU] -r has NO long form in GNU sum (digest.c longopts for
    // HASH_ALGO_SUM lists only --sysv); a "--bsd" spelling is an
    // unrecognized option there (audit #1462).
    OPTION("-r", "", "use BSD sum algorithm (1024-byte blocks)", BOOL_TYPE),
    // [GNU] -s, --sysv
    OPTION("-s", "--sysv", "use System V sum algorithm (512-byte blocks)",
           BOOL_TYPE)};

REGISTER_COMMAND(sum, "sum", "sum [OPTION]... [FILE]...",
                 "Checksum and count the blocks in a file.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "The BSD algorithm is used by default (1024-byte blocks).\n",
                 "  sum file.txt\n"
                 "  echo \"test\" | sum\n"
                 "  sum -r file.txt",
                 "cksum(1), md5sum(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 SUM_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/sum.rs, sum_cmd — sum.c's BSD
  // rotate/add and SysV 16-bit fold, block counts, and the last-of-(-r|-s)
  // rule). The C++ pipeline, glob expansion and stream loop are deleted;
  // REGISTER_COMMAND keeps the help/man metadata. ctx.raw_args reaches the
  // library verbatim because sum.c parses its own getopt run.
  //
  // stdin shape: sum reads fd 0 when there is no FILE operand or an
  // operand is "-", so the handler routes through ABI v2 STREAMING stdin —
  // the host pull callback stays lazy, so file operands never touch fd 0
  // (REPLACEMENT-RECIPE §5). The library owns the binary-mode read and the
  // GNU diagnostics for an unreadable operand.
  return winux::rust2::run_command_with_stdin_streaming("sum", ctx.raw_args);
}
