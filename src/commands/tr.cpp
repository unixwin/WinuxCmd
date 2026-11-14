// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for tr.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <fcntl.h>
#include <io.h>

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr TR_OPTIONS = std::array{
    // [GNU] -c, --complement
    OPTION("-c", "--complement", "use the complement of SET1", BOOL_TYPE),
    // [EXT] -C
    OPTION("-C", "", "same as -c", BOOL_TYPE),
    // [GNU] -d, --delete
    OPTION("-d", "--delete", "delete characters in SET1, do not translate",
           BOOL_TYPE),
    // [GNU] -s, --squeeze-repeats
    OPTION("-s", "--squeeze-repeats",
           "replace each input sequence of a repeated character that is listed "
           "in SET1 with a single occurrence of that character",
           BOOL_TYPE),
    // [GNU] -t, --truncate-set1
    OPTION("-t", "--truncate-set1", "first truncate SET1 to length of SET2",
           BOOL_TYPE),
    // [GNU] -A: undocumented historical no-op, accepted and ignored
    OPTION("-A", "", "", BOOL_TYPE)};

REGISTER_COMMAND(
    tr, "tr", "tr [OPTION]... SET1 [SET2]",
    "Translate, squeeze, and/or delete characters from standard input,\n"
    "writing to standard output.\n"
    "\n"
    "SET1 and SET2 specify arrays of characters to translate, delete,\n"
    "or squeeze. Interpreted sequences are:\n"
    "  \\\\NNN  character with octal value NNN (1 to 3 digits)\n"
    "  \\\\\\\\\\  backslash\n"
    "  \\a    audible BEL\n"
    "  \\b    backspace\n"
    "  \\f    form feed\n"
    "  \\n    new line\n"
    "  \\r    return\n"
    "  \\t    horizontal tab\n"
    "  \\v    vertical tab\n"
    "CHAR1-CHAR2  all characters from CHAR1 to CHAR2 in ascending order\n"
    "[:CLASS:]    ASCII classes: alnum, alpha, blank, cntrl, digit,\n"
    "             graph, lower, print, punct, space, upper, xdigit",
    "  echo 'hello' | tr 'a-z' 'A-Z'\n"
    "  echo 'hello   world' | tr -s ' '\n"
    "  echo 'Hello World' | tr -d 'a-z'\n"
    "  echo 'Hello World' | tr -c 'A-Z' '*'",
    "expand(1), unexpand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    TR_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/tr.rs, tr_cmd — SET expansion/quoting,
  // range and class validation, translate/delete/squeeze with the probed
  // GNU diagnostics). The C++ set-expansion tables, thread_local state and
  // stream loop are deleted; REGISTER_COMMAND keeps the help/man metadata.
  // ctx.raw_args reaches the library verbatim because tr.c parses its own
  // getopt run ("+cCdst"; -A is GNU's undocumented BSD no-op).
  //
  // stdin shape: tr always reads fd 0, so the handler routes through ABI
  // v2 STREAMING stdin — 64 KiB segments through the host pull callback
  // instead of a whole-buffer slurp, with the library owning GNU's
  // read-error diagnostic (REPLACEMENT-RECIPE §5).
  return winux::rust2::run_command_with_stdin_streaming("tr", ctx.raw_args);
}
