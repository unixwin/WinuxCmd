// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for b2sum.
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

auto constexpr B2SUM_OPTIONS = std::array{
    // [GNU]
    OPTION("-l", "--length", "digest length in bits; must be multiple of 8",
           STRING_TYPE),
    // [GNU]
    OPTION("-b", "--binary", "read in binary mode (default)", BOOL_TYPE),
    // [GNU]
    OPTION("-c", "--check", "read BLAKE2 sums from the FILEs and check them",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files", BOOL_TYPE),
    // [GNU]
    OPTION("-t", "--text", "read in text mode", BOOL_TYPE),
    // [GNU]
    OPTION("", "--quiet", "don't print OK for each successfully verified file",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--status", "don't output anything, status code shows success",
           BOOL_TYPE),
    // [GNU]
    OPTION("-w", "--warn", "warn about improperly formatted checksum lines",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--tag", "create a BSD-style checksum", BOOL_TYPE),
    // [GNU]
    OPTION("-z", "--zero", "end each output line with NUL, not newline",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--strict", "with --check, exit non-zero for any invalid input",
           BOOL_TYPE)};

REGISTER_COMMAND(
    b2sum, "b2sum", "b2sum [OPTION]... [FILE]...",
    "Print or check BLAKE2 (512-bit) checksums.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Uses WinuxCmd's portable BLAKE2b implementation, matching GNU b2sum's "
    "default 512-bit digest and --length truncation behavior.",
    "  b2sum file.txt\n"
    "  echo \"test\" | b2sum\n"
    "  b2sum *.txt > checksums.b2",
    "sha1sum(1), sha256sum(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    B2SUM_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/digestsum.rs, b2sum_cmd — digest.c output
  // shapes, -l/--length validation and the full --check machinery); the C++
  // side keeps only this registration. ctx.raw_args passes the operands
  // verbatim because GNU digest.c parses its own getopt run.
  //
  // stdin mode (ABI v1 whole-buffer): GNU digest.c reads fd 0 when no FILE
  // operand stands, and "-" as a per-operand stdin marker, so the bridge
  // slurps fd 0 exactly for those shapes (echo "abc" | b2sum, and the `-c`
  // checkfile from stdin via "-").
  bool stdin_is_input = ctx.positionals.empty();
  for (auto arg : ctx.positionals) {
    if (arg == "-") stdin_is_input = true;
  }
  if (stdin_is_input) {
    return winux::rust2::run_command_with_stdin("b2sum", ctx.raw_args);
  }
  return winux::rust2::run_command("b2sum", ctx.raw_args);
}
