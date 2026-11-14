// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for rev command.
/// @Version: 0.2.0
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

auto constexpr REV_OPTIONS =
    // [EXT]
    std::array{OPTION("-0", "--zero", "use the NUL byte as line separator")};

REGISTER_COMMAND(
    rev_cmd,
    /* name */
    "rev",

    /* synopsis */
    "rev [OPTION] [FILE]...",
    "Reverse lines characterwise.\n"
    "\n"
    "Print each specified record in reverse character order. Records are "
    "newline-terminated by default; -0, --zero uses NUL as the separator. If "
    "no "
    "FILE is specified, read from standard input. This follows util-linux "
    "rev's "
    "record model, including preserving unterminated final records.",
    "  echo 'hello' | rev\n"
    "  rev input.txt\n"
    "  rev -0 names.bin",

    /* see also */
    "tac(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", REV_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/rev.rs, rev_cmd — the util-linux rev
  // 2.39.3 model: `-0/--zero`, `-` opened literally (not stdin), locale
  // -driven byte/UTF-8 reversal units, GNU-shaped option diagnostics); the
  // C++ side keeps only this registration. ctx.raw_args passes the operands
  // verbatim because rev.c parses its own options (argopt getopt_long run,
  // permuted).
  //
  // stdin mode (ABI v1 whole-buffer): rev reads fd 0 ONLY when no FILE
  // operand stands — `-` is NOT stdin for rev (util-linux opens it
  // literally, "rev: cannot open -: ..."), so unlike the digest family the
  // stdin shape is exactly the no-operand invocation (echo hello | rev).
  if (ctx.positionals.empty()) {
    return winux::rust2::run_command_with_stdin("rev", ctx.raw_args);
  }
  return winux::rust2::run_command("rev", ctx.raw_args);
}
