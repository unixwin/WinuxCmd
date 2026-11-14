// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for shuf.
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

auto constexpr SHUF_OPTIONS = std::array{
    // [GNU] -e, --echo
    OPTION("-e", "--echo", "treat each ARG as an input line", BOOL_TYPE),
    // [GNU] -i, --input-range
    OPTION("-i", "--input-range",
           "treat each number LO through HI as an input line", STRING_TYPE),
    // [GNU] -n, --head-count
    OPTION("-n", "--head-count", "output at most COUNT lines", STRING_TYPE),
    // [GNU] -o, --output
    OPTION("-o", "--output", "write result to FILE instead of standard output",
           STRING_TYPE),
    // [GNU] -r, --repeat
    OPTION("-r", "--repeat", "output lines can be repeated", BOOL_TYPE),
    // [DIFFERS from GNU] --random-seed is a WinuxCmd extension.
    // GNU coreutils has no --random-seed; reproducibility is achieved via
    // --random-source with a fixed seed file.
    OPTION("", "--random-seed",
           "use STRING to initialize a reproducible random permutation",
           STRING_TYPE),
    // [GNU] --random-source
    OPTION("", "--random-source", "get random bytes from FILE", STRING_TYPE),
    // [GNU] -z, --zero-terminated
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline",
           BOOL_TYPE)};

REGISTER_COMMAND(
    shuf, "shuf", "shuf [OPTION]... [FILE]",
    "Shuffle randomize lines.\n"
    "\n"
    "Write a random permutation of the input lines to standard output.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: --random-seed uses a reproducible SHA3-256 + ChaCha12 path,\n"
    "and --random-source follows the GNU/Microsoft-compatible entropy-\n"
    "recycling adapter.",
    "  shuf file.txt\n"
    "  shuf -n 5 file.txt\n"
    "  shuf -e a b c d e\n"
    "  shuf -i 1-10\n"
    "  shuf -r -n 10 -i 1-6",
    "sort(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SHUF_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/shuf.rs, shuf_cmd — GNU shuf.c record model,
  // --random-source's entropy-recycling adapter, and the documented
  // --random-seed extension's SHA3-256 + ChaCha12 path that keeps 1.x
  // reproducible output byte-identical). The C++ side keeps only this
  // registration.
  //
  // stdin shape: shuf reads fd 0 when no FILE operand stands and -e/-i did
  // not supply records, and a "-" operand names stdin. The v2 streaming
  // entry is a lazy PULL callback, so choosing it unconditionally costs
  // nothing when the command never reads (fd 0 stays live for the next
  // pipeline stage) and avoids a whole-buffer slurp for the stdin shapes.
  return winux::rust2::run_command_with_stdin_streaming("shuf", ctx.raw_args);
}
