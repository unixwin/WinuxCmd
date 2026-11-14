// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for od command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr OD_OPTIONS = std::array{
    // [GNU]
    OPTION("-A", "--address-radix", "select output address radix", STRING_TYPE),
    // [GNU]
    OPTION("-a", "", "select named character output"),
    // [GNU]
    OPTION("-b", "", "select octal byte output"),
    // [GNU]
    OPTION("-c", "", "select ASCII output"),
    // [GNU]
    OPTION("-d", "", "select unsigned decimal 2-byte output"),
    // [GNU]
    OPTION("-j", "--skip-bytes", "skip bytes", STRING_TYPE),
    // [GNU]
    OPTION("-N", "--read-bytes", "limit bytes", STRING_TYPE),
    // [GNU]
    OPTION("-o", "", "select octal 2-byte output"),
    // [GNU]
    OPTION("-t", "--format", "select output type", STRING_TYPE),
    // [GNU]
    OPTION("-v", "--output-duplicates", "write all input data"),
    // [GNU]
    OPTION("-w", "--width", "output bytes per line", OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-x", "", "select hexadecimal 2-byte units"),
    // [GNU]
    OPTION("", "--endian", "byte order for multi-byte input units",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--traditional",
           "accept arguments in traditional form (e.g., od -x file)"),
    // [GNU] --strings: output strings of at least BYTES graphic characters
    // (optional argument: bare --strings means --strings=3)
    OPTION("", "--strings",
           "output strings of at least BYTES graphic characters",
           OPTIONAL_STRING_TYPE),
    // [GNU] -S: output strings of at least BYTES graphic characters
    OPTION("-S", "", "output strings of at least BYTES graphic characters",
           STRING_TYPE),
    // [GNU] -f: select floating-point output
    OPTION("-f", "", "select floating-point output"),
    // [GNU] -i: select signed decimal int output (-t dI)
    OPTION("-i", "", "select signed decimal int output"),
    // [GNU] -l: select signed decimal long output (-t dL)
    OPTION("-l", "", "select signed decimal long output"),
    // [GNU] -s: select signed decimal 2-byte output
    OPTION("-s", "", "select signed decimal 2-byte output"),
    // [GNU] od.c CASE_OLD_ARG table: the remaining traditional format
    // letters are obsolescent and undocumented in GNU, so they stay
    // hidden here too (empty description, like tsort -w). Mapping:
    // -D = u4, -e/-F = fD, -H/-X = x4, -h = x2, -I/-L = dL,
    // -O = o4, -B = o2.
    OPTION("-B", "", ""),
    OPTION("-D", "", ""),
    OPTION("-e", "", ""),
    OPTION("-F", "", ""),
    OPTION("-H", "", ""),
    OPTION("-h", "", ""),
    OPTION("-I", "", ""),
    OPTION("-L", "", ""),
    OPTION("-O", "", ""),
    OPTION("-X", "", "")};

REGISTER_COMMAND(
    od,
    /* cmd_name */ "od",
    /* cmd_synopsis */ "od [OPTION]... [FILE]...",
    /* cmd_desc */
    "Dump files in octal and other formats.\n"
    "Write an unambiguous representation of each FILE to standard output.\n"
    "With no FILE, or when FILE is -, read standard input.",
    /* examples */
    "  od -x file.bin\n"
    "  od -t x1z -v file.bin\n"
    "  echo 'Hello' | od -c\n"
    "  od -A x -t x1z -N 16 file.bin",
    /* see_also */ "hexdump, xxd",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ OD_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/od.rs, od_cmd — od.c's block dump,
  // address radixes, --endian, the -j/-N coordinate space, the
  // -S/--strings scan and the old-style [FILE] [[+]OFFSET] [[+]LABEL]
  // operand form). The C++ formatter/dump pipeline is deleted;
  // REGISTER_COMMAND keeps the help/man metadata. ctx.raw_args reaches the
  // library verbatim because od.c parses its own getopt_long run
  // ("A:aBbcDdeFfHhIij:LlN:OoS:st:vw::Xx" + long options).
  //
  // stdin shape: od reads fd 0 when there is no FILE operand or an operand
  // is "-", so the handler routes through ABI v2 STREAMING stdin — the
  // host pull callback stays lazy, so file operands never touch fd 0
  // (REPLACEMENT-RECIPE §5).
  return winux::rust2::run_command_with_stdin_streaming("od", ctx.raw_args);
}
