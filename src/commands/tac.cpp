// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for tac.
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

auto constexpr TAC_OPTIONS = std::array{
    // [GNU] -b, --before
    OPTION("-b", "--before",
           "attach the separator before instead of after each record"),
    // [GNU] -r, --regex
    OPTION("-r", "--regex", "treat the separator as a regular expression"),
    // [GNU] -s, --separator
    OPTION("-s", "--separator", "use STRING as the record separator",
           STRING_TYPE),
};

REGISTER_COMMAND(
    tac, "tac", "tac [OPTION]... [FILE]...",
    "Concatenate and print files in reverse.\n"
    "\n"
    "Write each FILE to standard output, last line first.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Records are separated by newline by default. Use -s to choose another "
    "separator,\n"
    "-b to attach separators before records, and -r to treat the separator as "
    "a regex.\n",
    "  tac file.txt\n"
    "  echo -e 'line1\\nline2\\nline3' | tac\n"
    "  tac -s : file.txt",
    "cat(1), rev(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TAC_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/tac.rs — tac.c literal/BRE separator walk,
  // right-to-left match discovery, NUL separator rule); the C++ side keeps
  // only this registration. ctx.raw_args passes the operands verbatim
  // because GNU tac.c parses its own getopt run.
  //
  // stdin mode (ABI v2 streaming, #1488 digest-family adoption): GNU
  // tac.c reads fd 0 when no FILE operand stands, and "-" as a per-operand
  // stdin marker, so the bridge reads fd 0 in chunks exactly for those
  // shapes (echo "a\nb" | tac, `tac -`) — no whole-buffer slurp residency
  // and no ABI v1 stdin_buf copy; a host read failure flows through the
  // command's own read-error rc (tac: silent rc 1, GNU-exact for the "-"
  // input) instead of a host-side pre-check.
  bool stdin_is_input = ctx.positionals.empty();
  for (auto arg : ctx.positionals) {
    if (arg == "-") stdin_is_input = true;
  }
  if (stdin_is_input) {
    return winux::rust2::run_command_with_stdin_streaming("tac", ctx.raw_args);
  }
  return winux::rust2::run_command("tac", ctx.raw_args);
}
