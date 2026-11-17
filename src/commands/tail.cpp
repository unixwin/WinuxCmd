// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "pch/pch.h"

// include other header after pch.h

#include "core/command_macros.h"

import std;

import core;

import utils;

import rust2_bridge;

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief TAIL command options definition
 *
 * This array defines all the options supported by the tail command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -c, @a --bytes: Output the last NUM bytes; or use -c +NUM to output
 *   starting with byte NUM of each file [IMPLEMENTED]
 * - @a -f, @a --follow: Output appended data as the file grows [IMPLEMENTED]
 *
 * - @a -F: Same as --follow=name --retry [IMPLEMENTED]
 * - @a -n, @a --lines:
 * Output the last NUM lines, instead of the last 10; or
 * use -n +NUM to skip
 * NUM-1 lines at the start [IMPLEMENTED]
 * - @a -NUM: Obsolete GNU-compatible
 * shorthand for -n NUM [IMPLEMENTED]
 * - @a +NUM: Obsolete compatibility
 * shorthand for -n +NUM [IMPLEMENTED]
 * - @a
 * --max-unchanged-stats: With --follow=name, reopen a FILE which has not
 * changed
 *   size after N iterations to see if it has been renamed [IMPLEMENTED]
 * - @a --pid: With -f, terminate after process ID, PID dies [IMPLEMENTED]
 * -
 @a --debug: Output follow implementation details to stderr [IMPLEMENTED]
 * -
 * @a -q, @a --quiet: Never output headers giving file names [IMPLEMENTED]
 * - @a --silent: Never output headers giving file names [IMPLEMENTED]
 * - @a --retry: Keep trying to open a file if it is inaccessible [IMPLEMENTED]

 * * - @a -s, @a --sleep-interval: With -f, sleep for approximately N seconds
 *
 * between iterations [IMPLEMENTED]
 * - @a -v, @a --verbose: Always output
 * headers giving file names [IMPLEMENTED]
 * - @a -z, @a --zero-terminated: Line delimiter is NUL, not newline
 * [IMPLEMENTED]
 */
auto constexpr TAIL_OPTIONS = std::array{
    // [GNU] -c, --bytes
    OPTION("-c", "--bytes",
           "output the last NUM bytes; or use -c +NUM to output\n"
           "starting with byte NUM of each file",
           STRING_TYPE),
    // [GNU] --debug
    OPTION("", "--debug", "output extra follow diagnostics to stderr"),
    // [GNU] -f
    OPTION("-f", "", "output appended data as the file grows"),
    // [GNU] --follow
    OPTION("", "--follow",
           "output appended data as the file grows; with --follow=name,\n"
           "follow the file name rather than the descriptor",
           OPTIONAL_STRING_TYPE),
    // [GNU] -F
    OPTION("-F", "", "same as --follow=name --retry"),
    // [GNU] -n, --lines
    OPTION("-n", "--lines",
           "output the last NUM lines, instead of the last 10; or\n"
           "use -n +NUM to skip NUM-1 lines at the start",
           STRING_TYPE),
    // [GNU] --max-unchanged-stats
    // STRING (not INT) so an unparsable value reaches build_config
    // and gets tail's own diagnostic, not the framework's (#1462).
    OPTION("", "--max-unchanged-stats",
           "with --follow=name, reopen a FILE which has not changed\n"
           "size after N iterations to see if it has been renamed\n"
           "[IMPLEMENTED]",
           STRING_TYPE),
    // [GNU] --pid (STRING for the same reason: "invalid PID: '<v>'")
    OPTION("", "--pid",
           "with -f, terminate after process ID, PID dies [IMPLEMENTED]",
           STRING_TYPE),
    // [GNU] -q, --quiet
    OPTION("-q", "--quiet", "never output headers giving file names"),
    // [GNU] --silent
    OPTION("", "--silent", "never output headers giving file names"),
    // [GNU] --retry
    OPTION("", "--retry", "keep trying to open a file if it is inaccessible"),
    // [GNU] --use-polling
    // [DIFFERS] - not applicable; Windows uses ReadDirectoryChangesW
    OPTION("", "--use-polling",
           "disable native directory change watching and use polling instead"),
    // [GNU] -s, --sleep-interval
    OPTION("-s", "--sleep-interval",
           "with -f, sleep for approximately N seconds between iterations",
           STRING_TYPE),
    // [GNU] -v, --verbose
    OPTION("-v", "--verbose", "always output headers giving file names"),
    // [GNU] -z, --zero-terminated
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

REGISTER_COMMAND(
    tail, "tail", "tail [OPTION]... [FILE]...",
    "Print the last 10 lines of each FILE to standard output.\n"
    "With more than one FILE, precede each with a header giving the file "
    "name.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.",
    "  tail file.txt\n"
    "  tail -n 20 file.txt\n"
    "  tail -20 file.txt\n"
    "  tail -n +5 file.txt\n"
    "  tail +5 file.txt\n"
    "  tail -c 64 file.txt\n"
    "  tail -v a.txt b.txt",
    "head(1), cat(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TAIL_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/tail.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // tail's stdin class (terminal/pipe/disk) is injected as StdinMeta by
  // the rust2 registry (pipe default in the product path; the tty gate
  // composes the real class), so this side never probes a console handle.
  return winux::rust2::run_command_with_stdin_streaming("tail", ctx.raw_args);
}
