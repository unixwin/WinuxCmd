// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for tee.
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

/**
 * @brief TEE command options definition
 *
 * This array defines all the options supported by the tee command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -a, @a --append: Append to the given FILEs, do not overwrite
 * [IMPLEMENTED]
 * - @a -i, @a --ignore-interrupts: Ignore interrupt signals [IMPLEMENTED]
 * - @a -p: Diagnose write errors using warn-nopipe mode [IMPLEMENTED]
 */
auto constexpr TEE_OPTIONS = std::array{
    // [GNU] -a, --append
    OPTION("-a", "--append", "append to the given FILEs, do not overwrite"),
    // [GNU] -i, --ignore-interrupts
    OPTION("-i", "--ignore-interrupts", "ignore interrupt signals"),
    // [GNU] -p has no long form; the "--diagnose" alias was invented and
    // is removed (#1355).
    OPTION("-p", "", "write errors to standard error"),
    // [GNU] --output-error
    OPTION("", "--output-error",
           "set behavior on write error: 'warn', 'warn-nopipe', "
           "'exit', 'exit-nopipe'",
           OPTIONAL_STRING_TYPE)};
REGISTER_COMMAND(
    tee, "tee",
    "read from standard input and write to standard output and files",
    "Copy standard input to each FILE, and also to standard output.\n"
    "\n"
    "FILE operands are opened literally; '-' names a file called '-'.",
    "  echo 'Hello' | tee output.txt       Save output to file\n"
    "  echo 'World' | tee -a output.txt    Append to file\n"
    "  cat file.txt | tee backup.txt       Create backup while viewing",
    "cat(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd", TEE_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/tee.rs, tee_cmd); the C++ output-error
  // mode machine is deleted. ctx.raw_args passes the operands verbatim
  // because GNU tee.c parses its own getopt run. GNU tee always reads
  // stdin, so the whole-buffer v1 bridge entry (run_command_with_stdin)
  // carries the call; the old #973 closed-stdin EBADF probe is not
  // representable in ABI v1 (no stdin error channel) and returns with the
  // streaming-v2 adoption.
  return winux::rust2::run_command_with_stdin("tee", ctx.raw_args);
}
