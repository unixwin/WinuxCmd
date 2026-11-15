// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email@example.com>
///   - @contributor3 <email@example.com>
/// @description:
/// @Description: Implemention for mv.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

/**
 * @brief MV command options definition
 *
 * This array defines all the options supported by the mv command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -b: Like --backup but does not accept an argument [IMPLEMENTED]
 * - @a -f, @a --force: Do not prompt before overwriting [IMPLEMENTED]
 * - @a -i: Prompt before overwrite [IMPLEMENTED]
 * - @a -n, @a --no-clobber: Do not overwrite an existing file [IMPLEMENTED]
 * - @a --strip-trailing-slashes: Remove any trailing slashes from each SOURCE
 * argument [IMPLEMENTED]
 * - @a -S, @a --suffix: Override the usual backup suffix [IMPLEMENTED]
 * - @a -t, @a --target-directory: Move all SOURCE arguments into DIRECTORY
 * [IMPLEMENTED]
 * - @a -T, @a --no-target-directory: Treat DEST as a normal file [IMPLEMENTED]
 * - @a -u: Move only when the SOURCE file is newer than the destination file or
 * when the destination file is missing [IMPLEMENTED]
 * - @a -v, @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a -Z, @a --context: Set SELinux security context of destination file to
 * default type [TODO]
 * - @a --backup: Make a backup of each existing destination file [IMPLEMENTED]
 * - @a --force: Do not prompt before overwriting [IMPLEMENTED]
 * - @a --interactive: Prompt according to WHEN: never, once, or always
 * (-i) [IMPLEMENTED]
 * - @a --no-clobber: Do not overwrite an existing file [IMPLEMENTED]
 * - @a --suffix: Override the usual backup suffix [IMPLEMENTED]
 * - @a --target-directory: Move all SOURCE arguments into DIRECTORY
 * [IMPLEMENTED]
 * - @a --no-target-directory: Treat DEST as a normal file [IMPLEMENTED]
 * - @a --update: Move only when the SOURCE file is newer than the destination
 * file or when the destination file is missing [IMPLEMENTED]
 * - @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a --context: Set SELinux security context of destination file to default
 */

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// clang-format off
auto constexpr MV_OPTIONS =
    std::array{
               // [GNU]
               OPTION("-b", "", "like --backup but does not accept an argument"),
               // [GNU] --debug: explain how a file is moved
               OPTION("", "--debug", "explain how a file is moved"),
               // [DIFFERS] --exchange is a 9.5 option; GNU 9.4 rejects it, and
               // so does the rust2 parser (unrecognized option). Kept in the
               // help table for the next coreutils bump.
               OPTION("", "--exchange", "exchange source and destination"),
               // [GNU]
               OPTION("-f", "--force", "do not prompt before overwriting"),
               // [GNU]
               OPTION("-i", "", "prompt before overwrite"),
               // [GNU] mv has no -I option (that is rm's); the "once" prompt
               // mode is reached only via --interactive=once (#1355).
               // [GNU]
               OPTION("-n", "--no-clobber", "do not overwrite an existing file"),
               // [GNU]
               OPTION("", "--no-copy", "do not copy if renaming fails"),
               // [GNU]
               OPTION("", "--strip-trailing-slashes", "remove any trailing slashes from each SOURCE argument"),
               // [GNU]
               OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
               // [GNU]
               OPTION("-t", "--target-directory", "move all SOURCE arguments into DIRECTORY", STRING_TYPE),
               // [GNU]
               OPTION("-T", "--no-target-directory", "treat DEST as a normal file"),
               // [GNU]
               OPTION("-u", "", "equivalent to --update[=older]"),
               // [GNU]
               OPTION("-v", "--verbose", "explain what is being done"),
               // [DIFFERS]
               OPTION("-Z", "--context", "set SELinux security context of destination file to default type"),
               // [GNU]
               OPTION("", "--backup", "make a backup of each existing destination file", OPTIONAL_STRING_TYPE),
               // [GNU]
               OPTION("", "--interactive", "prompt according to WHEN: never, once, or always (-i)", OPTIONAL_STRING_TYPE),
               // [GNU]
               OPTION("", "--no-clobber", "do not overwrite an existing file"),
               // [GNU]
               OPTION("", "--suffix", "override the usual backup suffix", STRING_TYPE),
               // [GNU]
               OPTION("", "--target-directory", "move all SOURCE arguments into DIRECTORY", STRING_TYPE),
               // [GNU]
               OPTION("", "--no-target-directory", "treat DEST as a normal file"),
               // [GNU]
               OPTION("", "--update", "control which existing files are updated; UPDATE={all,none,older(default)}", OPTIONAL_STRING_TYPE),
               // [GNU]
               OPTION("", "--verbose", "explain what is being done"),
               // [DIFFERS]
               OPTION("", "--context", "set SELinux security context of destination file to default type")};
// clang-format on

REGISTER_COMMAND(
    mv,
    /* cmd_name */ "mv",
    /* cmd_synopsis */ "move (rename) files",
    /* cmd_desc */
    "Rename SOURCE to DEST, or move SOURCE(s) to DIRECTORY.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options "
    "too.\n",
    /* examples */
    "  mv file1 file2            Rename file1 to file2\n"
    "  mv file1 file2 dir        Move file1 and file2 to directory dir\n"
    "  mv -i file1 file2         Prompt before overwriting file2\n"
    "  mv -v file1 file2         Verbose output\n",
    /* see_also */ "cp(1), rm(1), ln(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    MV_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/mv.rs, mv::{opts,tests}); the C++ side
  // keeps only this registration — the copy/rename ladder, backup naming,
  // junction handling and the operand plumbing are deleted. ctx.raw_args
  // carries the operands verbatim because GNU mv.c parses its own getopt
  // run. The streaming entry feeds stdin on demand, so the `-i` prompt
  // reads the terminal/pipe exactly like GNU while non-interactive runs
  // never touch fd 0 (the cp precedent). The merged 1.x fixes re-derived
  // from GNU 9.4 on the rust2 side: #1649 (directory onto non-directory
  // refusal, copy.c:2441-2504), same-file detection, `-f`/`-i`/`-n`
  // last-wins, and the `--exchange` rejection noted above.
  return winux::rust2::run_command_with_stdin_streaming("mv", ctx.raw_args);
}
