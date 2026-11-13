// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sha384sum.
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

auto constexpr SHA384SUM_OPTIONS = std::array{
    // [DIFFERS] -b, --binary
    OPTION("-b", "--binary", "read in binary mode (default)", BOOL_TYPE),
    // [GNU] -c, --check
    OPTION("-c", "--check", "read SHA384 sums from the FILEs and check them",
           BOOL_TYPE),
    // [GNU] --ignore-missing
    OPTION("", "--ignore-missing",
           "don't fail or report status for missing files", BOOL_TYPE),
    // [DIFFERS] -t, --text
    OPTION("-t", "--text", "read in text mode", BOOL_TYPE),
    // [GNU] -q, --quiet
    OPTION("", "--quiet", "don't print OK for each successfully verified file",
           BOOL_TYPE),
    // [GNU] -s, --status
    OPTION("", "--status", "don't output anything, status code shows success",
           BOOL_TYPE),
    // [GNU] -w, --warn
    OPTION("-w", "--warn", "warn about improperly formatted checksum lines",
           BOOL_TYPE),
    // [GNU] --tag
    OPTION("", "--tag", "create a BSD-style checksum", BOOL_TYPE),
    // [GNU] -z, --zero
    OPTION("-z", "--zero", "end each output line with NUL, not newline",
           BOOL_TYPE),
    // [GNU] --strict
    OPTION("", "--strict", "with --check, exit non-zero for any invalid input",
           BOOL_TYPE)};

REGISTER_COMMAND(sha384sum, "sha384sum", "sha384sum [OPTION]... [FILE]...",
                 "Compute and check SHA384 message digest.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "SHA384 produces a 384-bit (48-byte) hash value, typically "
                 "rendered as a 96-digit hexadecimal number.\n"
                 "Uses WinuxCmd's portable SHA-384 implementation with the "
                 "FIPS-180-2 initial state and digest length.",
                 "  sha384sum file.txt\n"
                 "  echo \"test\" | sha384sum\n"
                 "  sha384sum *.txt > checksums.sha384",
                 "md5sum(1), sha1sum(1), sha256sum(1), sha512sum(1)",
                 "WinuxCmd", "Copyright © 2026 WinuxCmd", SHA384SUM_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/digestsum.rs, sha384sum_cmd — digest.c
  // output shapes plus the full --check machinery); the C++ side keeps only
  // this registration. ctx.raw_args passes the operands verbatim because GNU
  // digest.c parses its own getopt run.
  //
  // stdin mode (ABI v2 streaming, #1488 digest-family adoption): GNU
  // digest.c reads fd 0 when no FILE operand stands, and "-" as a
  // per-operand stdin marker, so the bridge reads fd 0 in chunks exactly
  // for those shapes (echo "abc" | sha384sum, and the `-c` checkfile from
  // stdin via "-") — no whole-buffer slurp residency; a host read failure
  // flows through the command's own read-error rc (silent rc 1,
  // GNU-exact for the "-" input) instead of a host-side pre-check.
  bool stdin_is_input = ctx.positionals.empty();
  for (auto arg : ctx.positionals) {
    if (arg == "-") stdin_is_input = true;
  }
  if (stdin_is_input) {
    return winux::rust2::run_command_with_stdin_streaming("sha384sum",
                                                          ctx.raw_args);
  }
  return winux::rust2::run_command("sha384sum", ctx.raw_args);
}
