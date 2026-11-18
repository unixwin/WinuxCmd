// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
///   - @description:
/// @Description: Implemention for rm.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief RM command options definition
 *
 * This array defines all the options supported by the rm command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -f, @a --force: Ignore nonexistent files and arguments, never prompt
 * [IMPLEMENTED]
 * - @a -i: Prompt before every removal [IMPLEMENTED]
 * - @a -I: Prompt once before removing more than three files, or when removing
 * recursively [IMPLEMENTED]
 * - @a -d, @a --dir: Remove empty directories [IMPLEMENTED]
 * - @a -r, @a --recursive: Remove directories and their contents recursively
 * [IMPLEMENTED]
 * - @a -R, @a --recursive: Remove directories and their contents recursively
 * [IMPLEMENTED]
 * - @a -v, @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a --interactive: Prompt according to WHEN: never, once (-I), or always
 * (-i) [IMPLEMENTED]
 * - @a --one-file-system: When removing a hierarchy recursively, skip any
 *
 * directory that is on a file system different from that of the corresponding

 * * command line argument [APPROXIMATE ON WINDOWS]
 * - @a --no-preserve-root:
 * Do not treat '/' specially [IMPLEMENTED]
 * - @a --preserve-root: Do not
 * remove '/' (default) [IMPLEMENTED]
 */
// clang-format off
constexpr auto RM_OPTIONS = std::array{
// [GNU]
    OPTION("-f", "--force", "ignore nonexistent files and arguments, never prompt"),
// [GNU]
    OPTION("-i", "", "prompt before every removal"),
// [GNU]
    OPTION("-I", "", "prompt once before removing more than three files, or when removing recursively"),
// [GNU]
    OPTION("-d", "--dir", "remove empty directories"),
// [GNU]
    OPTION("-r", "--recursive", "remove directories and their contents recursively"),
// [GNU]
    OPTION("-R", "--recursive", "remove directories and their contents recursively"),
// [GNU]
    OPTION("-v", "--verbose", "explain what is being done"),
// [GNU]
    OPTION("", "--interactive", "prompt according to WHEN: never, once (-I), or always (-i)", OPTIONAL_STRING_TYPE),
// [GNU]
    OPTION("", "--one-file-system", "when removing a hierarchy recursively, skip any directory that is on a file system different from that of the corresponding command line argument"),
// [DIFFERS]
    OPTION("", "--no-preserve-root", "do not treat '/' specially"),
// [DIFFERS]
    OPTION("", "--preserve-root", "do not remove '/' (default)", OPTIONAL_STRING_TYPE)};
// clang-format on

REGISTER_COMMAND(
    rm,
    /* cmd_name */ "rm",
    /* cmd_synopsis */ "remove files or directories",
    /* cmd_desc */
    "Remove the FILE(s).\n"
    "\n"
    "By default, rm does not remove directories. Use the --recursive (-r) "
    "option\n"
    "to remove each listed directory, too, along with all of its contents.\n",
    /* examples */
    "  rm file.txt               Remove file.txt\n"
    "  rm -r dir/                Recursively remove directory dir/\n"
    "  rm -v file1.txt file2.txt Verbose remove\n"
    "  rm -i file.txt            Interactive remove (prompt before removal)",
    /* see_also */ "cp(1), mv(1), mkdir(1), rmdir(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    RM_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/rm.rs, rm::{opts,tests}); the C++ side keeps
  // only this registration plus the original-argv abbreviation guard below.
  // The dispatcher expands unambiguous long-option prefixes before the
  // handler, which would erase the spelling rm.c refuses ("rm --no-p f" must
  // die with "you may not abbreviate the --no-preserve-root option"), so the
  // guard keeps reading the process argv (__wargv) exactly as the 1.x body
  // did. The streaming entry feeds stdin on demand, so the -i/-I prompts read
  // the terminal/pipe like GNU. The 1.x fixes re-derived on the rust2 side:
  // #1650, #1672 (empty operand), #1433 (reparse links removed as links).
  {
    constexpr std::string_view kNoPreserveRoot = "--no-preserve-root";
    bool end_of_options = false;
    for (int i = 1; i < __argc; ++i) {
      const std::string token = wstring_to_utf8(__wargv[i]);
      std::string_view arg(token);
      if (end_of_options || arg == "--") {
        end_of_options = true;
        continue;
      }
      if (!arg.starts_with("--")) continue;
      const auto eq = arg.find('=');
      const bool has_arg = eq != std::string_view::npos;
      const std::string_view name = has_arg ? arg.substr(0, eq) : arg;
      if (name.size() <= 2 || !kNoPreserveRoot.starts_with(name)) continue;
      if (has_arg) {
        // getopt_long reports the argument on the *resolved* option name
        // before rm's abbreviation check runs.
        safeErrorPrintLn(
            "rm: option '--no-preserve-root' doesn't allow an argument");
        safeErrorPrintLn("Try 'rm --help' for more information.");
        return 1;
      }
      if (name.size() < kNoPreserveRoot.size()) {
        safeErrorPrintLn(
            "rm: you may not abbreviate the --no-preserve-root option");
        return 1;
      }
    }
  }

  return winux::rust2::run_command_with_stdin_streaming("rm", ctx.raw_args);
}
