// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for xargs.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief XARGS command options definition
 *
 * This array defines all the options supported by the xargs command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -n, --max-args: Use at most max-args arguments per command line
 * [IMPLEMENTED]
 * - @a -I: Replace occurrences of replace-str in the initial-arguments with
 * names [IMPLEMENTED]
 * - @a -i, --replace: Deprecated alias for -I [IMPLEMENTED]
 * - @a -0, --null: Input items are terminated by a null character [IMPLEMENTED]
 * - @a -d, --delimiter: Input items are terminated by the specified character
 * [IMPLEMENTED]
 * - @a -o, --open-tty: Reopen standard input as the console for children
 * [IMPLEMENTED ON WINDOWS CONSOLE]
 * - @a -t, --verbose: Print the command line on the standard error before
 * executing it [IMPLEMENTED]
 * - @a -p, --interactive: Prompt before running each command line
 * [IMPLEMENTED]
 * - @a -r, --no-run-if-empty: If the standard input is completely empty, do
 * not run the command [IMPLEMENTED]
 * - @a -P, --max-procs: Run up to max-procs processes at a time [IMPLEMENTED]
 * - @a -s, --max-chars: Use at most max-chars chars per command line
 * [IMPLEMENTED]
 * - @a -x, --exit: Exit if the size (see -s) is exceeded [IMPLEMENTED]
 * - @a -L, --max-lines: Use at most max-lines nonblank input lines per
 * command line [IMPLEMENTED]
 * - @a -a, --arg-file: Read items from file instead of standard input
 * [IMPLEMENTED]
 * - @a -E: Set the logical EOF string [IMPLEMENTED]
 * - @a -e, --eof: Set or disable the logical EOF string [IMPLEMENTED]
 * - @a --show-limits: Display command-line length limits [IMPLEMENTED]
 * - @a --process-slot-var: Set an environment variable to the process slot
 * [IMPLEMENTED]
 */
auto constexpr XARGS_OPTIONS = std::array{
    // [GNU] option
    OPTION("-n", "--max-args",
           "use at most max-args arguments per command line", INT_TYPE),
    // [GNU] option
    OPTION("-I", "",
           "replace occurrences of replace-str in the initial-arguments with "
           "names",
           STRING_TYPE),
    // [GNU] option
    OPTION("-i", "--replace",
           "deprecated alias for -I; replace occurrences of replace-str",
           OPTIONAL_STRING_TYPE),
    // [GNU] option
    OPTION("-0", "--null", "input items are terminated by a null character"),
    // [GNU] option
    OPTION("-d", "--delimiter",
           "input items are terminated by the specified character",
           STRING_TYPE),
    // [GNU] option
    OPTION("-o", "--open-tty",
           "reopen standard input as the console in the child process"),
    // [GNU] option
    OPTION("-t", "--verbose",
           "print the command line on the standard error before executing it"),
    // [GNU] option
    OPTION("-p", "--interactive", "prompt before running each command line"),
    // [GNU] option (xargs.c: "-r, --no-run-if-empty"; findutils runs nothing
    // whenever no arguments were accumulated, not merely when the raw input
    // stream was byte-empty)
    OPTION("-r", "--no-run-if-empty",
           "if no arguments were read from standard input, do not run the "
           "command"),
    // [GNU] option
    OPTION("-P", "--max-procs", "run up to max-procs processes at a time",
           INT_TYPE),
    // [GNU] option
    OPTION("-s", "--max-chars", "use at most max-chars chars per command line",
           INT_TYPE),
    // [GNU] option
    OPTION("-x", "--exit", "exit if the size (see -s) is exceeded"),
    // [GNU] option
    OPTION("-L", "",
           "use at most max-lines nonblank input lines per command line",
           INT_TYPE),
    // [GNU] option
    OPTION("", "--max-lines",
           "use at most max-lines nonblank input lines per command line",
           OPTIONAL_INT_TYPE),
    // [GNU] option
    OPTION("-l", "",
           "deprecated alias for -L; use max-lines nonblank input lines",
           OPTIONAL_INT_TYPE),
    // [GNU] option
    OPTION("-a", "--arg-file", "read items from file instead of standard input",
           STRING_TYPE),
    // [GNU] option
    OPTION("-E", "", "set the logical EOF string", STRING_TYPE),
    // [GNU] option
    OPTION("", "--eof", "set or disable the logical EOF string",
           OPTIONAL_STRING_TYPE),
    // [GNU] option
    OPTION("-e", "", "deprecated alias for --eof", OPTIONAL_STRING_TYPE),
    // [GNU] option
    OPTION("", "--show-limits", "display command-line length limits"),
    // [GNU] option
    OPTION("", "--process-slot-var",
           "set an environment variable to the child process slot",
           STRING_TYPE)};

REGISTER_COMMAND(
    xargs, "xargs", "build and execute command lines from standard input",
    "Build and execute command lines from standard input.\n"
    "\n"
    "Items are separated by blanks. The result command line is executed\n"
    "after each group of max-args items is read.",
    "  find . -name '*.cpp' | xargs rm -f     Delete all cpp files\n"
    "  echo file1 file2 | xargs cat         Concatenate files\n"
    "  find . -name '*.txt' | xargs -n1 grep 'pattern'  Search one file at a "
    "time",
    "find(1), grep(1), sed(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    XARGS_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/xargs.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // xargs consumes stdin as an item stream (delimiter/-0/-L/-n), so the
  // streaming entry is required: a whole-buffer slurp would break the
  // child-spawn pipe semantics.
  return winux::rust2::run_command_with_stdin_streaming("xargs", ctx.raw_args);
}
