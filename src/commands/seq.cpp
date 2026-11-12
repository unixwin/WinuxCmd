// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for seq.
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

auto constexpr SEQ_OPTIONS = std::array{
    // [GNU]
    OPTION("-f", "--format", "use printf style floating-point FORMAT",
           STRING_TYPE),
    // [GNU]
    OPTION("-s", "--separator", "use STRING to separate numbers", STRING_TYPE),
    // [GNU] seq has no -t/--terminator option; that pair was invented and
    // is removed (#1355). Output always ends with a newline.
    // [GNU]
    OPTION("-w", "--equal-width",
           "equalize width by padding with leading zeroes", BOOL_TYPE),
    // The shared option parser runs before seq can inspect operands. These
    // hidden sentinels let negative numeric operands reach seq's parser.
    // [GNU]
    OPTION("-0", "", "", BOOL_TYPE), OPTION("-1", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-2", "", "", BOOL_TYPE), OPTION("-3", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-4", "", "", BOOL_TYPE), OPTION("-5", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-6", "", "", BOOL_TYPE), OPTION("-7", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-8", "", "", BOOL_TYPE), OPTION("-9", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-.", "", "", BOOL_TYPE), OPTION("-+", "", "", BOOL_TYPE),
    // [EXT]
    OPTION("--", "", "", BOOL_TYPE), OPTION("-a", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-A", "", "", BOOL_TYPE), OPTION("-e", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-E", "", "", BOOL_TYPE), OPTION("-F", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-i", "", "", BOOL_TYPE), OPTION("-I", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-n", "", "", BOOL_TYPE), OPTION("-N", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-inf", "", "", BOOL_TYPE), OPTION("-Inf", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-INF", "", "", BOOL_TYPE), OPTION("-infinity", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-Infinity", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-INFINITY", "", "", BOOL_TYPE)};
REGISTER_COMMAND(seq, "seq",
                 "seq [OPTION]... LAST\n"
                 "seq [OPTION]... FIRST LAST\n"
                 "seq [OPTION]... FIRST INCREMENT LAST",
                 "Print numbers from FIRST to LAST, in steps of INCREMENT.",
                 "  seq 10\n"
                 "  seq 1 10\n"
                 "  seq 1 2 10\n"
                 "  seq -s ' ' 5 10\n"
                 "  seq -w 1 10",
                 "printf(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 SEQ_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/seq.rs, seq_cmd); the C++ long double
  // engine is deleted. ctx.raw_args passes the operands verbatim because
  // GNU seq.c parses its own getopt run; seq never reads stdin, so the
  // plain (stdin-less) bridge entry carries the call.
  return winux::rust2::run_command("seq", ctx.raw_args);
}
