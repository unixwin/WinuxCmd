// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for base32.
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

auto constexpr BASE32_OPTIONS = std::array{
    // [GNU]
    OPTION("-d", "--decode", "decode data", BOOL_TYPE),
    // [GNU]
    OPTION("-i", "--ignore-garbage",
           "when decoding, ignore non-alphabet characters", BOOL_TYPE),
    // [GNU]
    OPTION("-w", "--wrap",
           "wrap encoded lines after COLS character (default 76). Use 0 to "
           "disable line wrapping",
           STRING_TYPE)};

REGISTER_COMMAND(
    base32, "base32", "base32 [OPTION]... [FILE]",
    "Base32 encode or decode FILE, or standard input, to standard output.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "The input may contain newlines when decoding. Use --ignore-garbage to\n"
    "attempt to recover from any other non-alphabet bytes.",
    "  base32 file.txt\n"
    "  echo 'Hello' | base32\n"
    "  base32 -d encoded.txt\n"
    "  base32 -w 64 file.bin",
    "base64(1), basenc(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    BASE32_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/basenc.rs, base32_cmd); the C++ side keeps
  // only this registration. ctx.raw_args passes the operands verbatim
  // because GNU basenc.c parses its own getopt run.
  //
  // stdin mode (ABI v1 whole-buffer): GNU basenc.c reads fd 0 only when no
  // FILE operand stands, or when the single operand is "-" (the
  // extra-operand check precedes any read), so the bridge slurps fd 0 just
  // for those shapes and stays console-blocking exactly where GNU is.
  const bool stdin_is_input =
      ctx.positionals.empty() ||
      (ctx.positionals.size() == 1 && ctx.positionals[0] == "-");
  if (stdin_is_input) {
    return winux::rust2::run_command_with_stdin("base32", ctx.raw_args);
  }
  return winux::rust2::run_command("base32", ctx.raw_args);
}
