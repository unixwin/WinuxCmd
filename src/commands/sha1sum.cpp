// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sha1sum.
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

// sha1sum uses the same options as md5sum
auto constexpr SHA1SUM_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-b", "--binary", "read in binary mode (default)", BOOL_TYPE),
    // [GNU]
    OPTION("-c", "--check", "read SHA1 sums from the FILEs and check them",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files", BOOL_TYPE),
    // [DIFFERS]
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

REGISTER_COMMAND(sha1sum, "sha1sum", "sha1sum [OPTION]... [FILE]...",
                 "Compute and check SHA1 message digest.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "SHA1 produces a 160-bit (20-byte) hash value, typically "
                 "rendered as a 40-digit hexadecimal number.",
                 "  sha1sum file.txt\n"
                 "  echo \"test\" | sha1sum\n"
                 "  sha1sum *.txt > checksums.sha1",
                 "md5sum(1), sha256sum(1), sha512sum(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", SHA1SUM_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/digestsum.rs, sha1sum_cmd — digest.c output
  // shapes plus the full --check machinery); the C++ side keeps only this
  // registration. ctx.raw_args passes the operands verbatim because GNU
  // digest.c parses its own getopt run.
  //
  // stdin mode (ABI v1 whole-buffer): GNU digest.c reads fd 0 when no FILE
  // operand stands, and "-" as a per-operand stdin marker, so the bridge
  // slurps fd 0 exactly for those shapes (echo "abc" | sha1sum, and the
  // `-c` checkfile from stdin via "-"). This wave keeps the digest family
  // on the v1 whole-buffer entry (the streaming v2 swap that
  // sha224sum/sha384sum/b2sum adopted is a later optimization, not part of
  // the mechanical replacement).
  bool stdin_is_input = ctx.positionals.empty();
  for (auto arg : ctx.positionals) {
    if (arg == "-") stdin_is_input = true;
  }
  if (stdin_is_input) {
    return winux::rust2::run_command_with_stdin("sha1sum", ctx.raw_args);
  }
  return winux::rust2::run_command("sha1sum", ctx.raw_args);
}
