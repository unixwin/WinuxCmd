// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for ls.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief LS command options definition
 *
 * This array defines all the options supported by the ls command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -a, @a --all: Do not ignore entries starting with . [IMPLEMENTED]
 * - @a -A, @a --almost-all: Do not list implied . and .. [IMPLEMENTED]
 * - @a -b, @a --escape: Print C-style escapes for nongraphic characters
 * [IMPLEMENTED]
 * - @a -B, @a --ignore-backups: Do not list implied entries ending with ~
 * [IMPLEMENTED]
 * - @a -c: With -lt: sort by, and show, ctime; with -l: show ctime and sort by
 * name; otherwise: sort by ctime, newest first [IMPLEMENTED]
 * - @a -C: List entries by columns [IMPLEMENTED]
 * - @a -d, @a --directory: List directories themselves, not their contents
 * [IMPLEMENTED]
 * - @a -f: List all entries in directory order [IMPLEMENTED]
 * - @a -F, @a --classify: Append indicator (one of *=>@|) to entries
 * [IMPLEMENTED]
 * - @a -g: Like -l, but do not list owner [IMPLEMENTED]
 * - @a -h, @a --human-readable: With -l and -s, print sizes like 1K 234M 2G
 * etc. [IMPLEMENTED]
 * - @a -i, @a --inode: Print the index number of each file [IMPLEMENTED]
 * -
 * @a -k, @a --kibibytes: Default to 1024-byte blocks for file system usage
 * [IMPLEMENTED]
 * - @a -L, @a --dereference: When showing file information for a symbolic link,
 * show information for the file the link references [IMPLEMENTED]
 * - @a -l, @a --long, @a --long-list: Use a long listing format [IMPLEMENTED]
 * - @a -m: Fill width with a comma separated list of entries [IMPLEMENTED]
 * - @a -n, @a --numeric-uid-gid: Like -l, but list numeric user and group IDs
 * [IMPLEMENTED]
 * - @a -N, @a --literal: Print entry names without quoting [IMPLEMENTED]
 * - @a -o: Like -l, but do not list group information [IMPLEMENTED]
 * - @a -p, @a --indicator-style=WORD: Append /, file-type, or classify
 *
 * suffixes to entries
 * [IMPLEMENTED]
 * - @a -q, @a --hide-control-chars:
 * Print ? instead of nongraphic characters [IMPLEMENTED]
 * - @a -Q, @a --quote-name: Enclose entry names in double quotes
 * [IMPLEMENTED]
 * - @a -r, @a --reverse: Reverse order while sorting [IMPLEMENTED]
 * - @a -R, @a --recursive: List subdirectories recursively [IMPLEMENTED]
 * - @a -s, @a --size: Print the allocated size of each file, in blocks
 *
 * [IMPLEMENTED]
 * - @a -S: Sort by file size, largest first [IMPLEMENTED]
 * - @a -t: Sort by time, newest first [IMPLEMENTED]
 * - @a -T, @a --tabsize: Assume tab stops at each COLS instead of 8
 * [IMPLEMENTED]
 * - @a -u: With -lt: sort by, and show, access time; with -l: show access time
 * and sort by name; otherwise: sort by access time, newest first [IMPLEMENTED]
 * - @a -U: Do not sort; list entries in directory order [IMPLEMENTED]
 * - @a -v: Natural sort of (version) numbers within text [IMPLEMENTED]
 * - @a -w, @a --width: Set output width to COLS. 0 means no limit [IMPLEMENTED]
 * - @a -x: List entries by lines instead of by columns [IMPLEMENTED]
 * - @a -X: Sort alphabetically by entry extension [IMPLEMENTED]
 * - @a -Z, @a --context: Print any security context of each file [IMPLEMENTED]
 * - @a -1: List one file per line [IMPLEMENTED]
 */
auto constexpr LS_OPTIONS = std::array{
    OPTION("-a", "--all", "do not ignore entries starting with ."),
    OPTION("-A", "--almost-all", "do not list implied . and .."),
    OPTION("-b", "--escape", "print C-style escapes for nongraphic characters"),
    OPTION("-B", "--ignore-backups",
           "do not list implied entries ending with ~"),
    OPTION("-c", "",
           "with -lt: sort by, and show, ctime; with -l: show ctime and sort "
           "by name; otherwise: sort by ctime, newest first"),
    OPTION("-C", "", "list entries by columns"),
    OPTION("-d", "--directory",
           "list directories themselves, not their contents"),
    OPTION("-f", "", "list all entries in directory order"),
    OPTION("-F", "--classify", "append indicator (one of */=>@|) to entries"),
    OPTION("-g", "", "like -l, but do not list owner"),
    OPTION("-h", "--human-readable",
           "with -l and -s, print sizes like 1K 234M 2G etc."),
    OPTION("-i", "--inode", "print the index number of each file"),
    OPTION("-k", "--kibibytes",
           "default to 1024-byte blocks for file system usage"),
    OPTION("-I", "--ignore", "ignore entries matching PATTERN", STRING_TYPE),
    OPTION("-L", "--dereference",
           "when showing file information for a symbolic link, show "
           "information for the file the link references"),
    // [GNU] -l has no long form in GNU ls; "--long"/"--long-list" are
    // invented (#1411).
    OPTION("-l", "", "use a long listing format"),
    OPTION("-m", "", "fill width with a comma separated list of entries"),
    OPTION("-n", "--numeric-uid-gid",
           "like -l, but list numeric user and group IDs"),
    OPTION("-N", "--literal", "print entry names without quoting"),
    OPTION("-o", "", "like -l, but do not list group information"),
    OPTION("-p", "", "append / indicator to directories"),
    OPTION("-q", "--hide-control-chars",
           "print ? instead of nongraphic characters"),
    OPTION("-Q", "--quote-name", "enclose entry names in double quotes"),
    OPTION("-r", "--reverse", "reverse order while sorting"),
    OPTION("-R", "--recursive", "list subdirectories recursively"),
    OPTION("-s", "--size", "print the allocated size of each file, in blocks"),
    OPTION("-S", "", "sort by file size, largest first"),
    OPTION("-t", "", "sort by time, newest first"),
    // [GNU] -T takes a number in strtoumax base-0 syntax, so "0xff" is 255
    // while "-T=0xff" hands us the literal "=0xff" and is invalid.
    OPTION("-T", "--tabsize", "assume tab stops at each COLS instead of 8",
           STRING_TYPE),
    OPTION(
        "-u", "",
        "with -lt: sort by, and show, access time; with -l: show access time "
        "and sort by name; otherwise: sort by access time, newest first"),
    OPTION("-U", "", "do not sort; list entries in directory order"),
    OPTION("-v", "", "natural sort of (version) numbers within text"),
    // [GNU] -w takes a number in xstrtoumax base-0 syntax via
    // decode_line_length (ls.c:1867): "0x20" is 32, "010" is 8, and 0
    // or an overflowing value means "no limit" — none of which the
    // framework's base-10 int parse can express, so (like -T above) the
    // raw string is kept and decoded by ls itself (#1536).
    OPTION("-w", "--width", "set output width to COLS. 0 means no limit",
           STRING_TYPE),
    OPTION("-x", "", "list entries by lines instead of by columns"),
    OPTION("-X", "", "sort alphabetically by entry extension"),
    OPTION("-Z", "--context", "print any security context of each file"),
    OPTION("", "--sort", "sort entries by WORD", STRING_TYPE),
    OPTION("", "--format", "set output format", STRING_TYPE),
    OPTION("", "--time", "change time style", STRING_TYPE),
    OPTION("", "--block-size", "scale sizes by SIZE", STRING_TYPE),
    OPTION("", "--quoting-style", "use quoting style WORD", STRING_TYPE),
    OPTION("", "--show-control-chars",
           "show nongraphic characters as-is in file names"),
    OPTION("", "--indicator-style", "append indicator using WORD", STRING_TYPE),
    OPTION("", "--file-type", "append file type indicators, without *"),
    OPTION("", "--color",
           "colorize the output; WHEN can be 'always', 'auto', or 'never'",
           OPTIONAL_STRING_TYPE),
    OPTION("-1", "", "list one file per line"),
    OPTION("-D", "--dired", "generate output designed for Emacs dired mode"),
    OPTION("-G", "--no-group", "in a long listing, don't print group names"),
    OPTION("", "--group-directories-first", "group directories before files"),
    OPTION("", "--author", "show author in long format"),
    OPTION("-H", "--dereference-command-line",
           "follow symlinks listed on the command line"),
    OPTION("", "--dereference-command-line-symlink-to-dir",
           "follow each command-line symlink to a directory"),
    OPTION("", "--dereference-command-line-symlinks-to-dir",
           "follow each command-line symlink to a directory"),
    OPTION("", "--hide", "do not list implied entries matching PATTERN",
           STRING_TYPE),
    OPTION("", "--hyperlink",
           "hyperlink file names; WHEN can be 'always', 'auto', or 'never'",
           OPTIONAL_STRING_TYPE),
    OPTION("", "--si", "like -h, but use powers of 1000 not 1024"),
    OPTION("", "--full-time", "like -l --time-style=full-iso"),
    OPTION("", "--time-style",
           "time/date format with -l (e.g. full-iso, long-iso, iso, locale, "
           "+FORMAT)",
           STRING_TYPE),
    OPTION("", "--zero", "end each output line with NUL instead of newline")};

REGISTER_COMMAND(
    ls,
    /* cmd_name */ "ls",
    /* cmd_synopsis */ "list directory contents",
    /* cmd_desc */
    "List information about the FILEs (the current directory by default).\n"
    "Sort entries alphabetically if none of -cftuvSUX nor --sort is "
    "specified.\n"
    "\n"
    "With no FILE, list the current directory contents. With a FILE that is a\n"
    "directory, list the files and subdirectories inside that directory.\n"
    "With a FILE that is not a directory, list just that file.\n",
    /* examples */
    "  ls                      List files in current directory\n"
    "  ls -l                   Long listing format\n"
    "  ls -la                  Long listing format including hidden files\n"
    "  ls -lh                  Long listing format with human-readable sizes",
    /* see_also */ "find(1), grep(1), sort(1), wc(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    LS_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/ls.rs, ls::{opts,tests}); the C++ side keeps
  // only this registration. The 1.x pipeline (permission synthesis, reparse
  // handling, column/grid layout, quoting) is deleted. ctx.raw_args carries
  // the operands verbatim because GNU ls.c parses its own getopt run; the
  // streaming entry only pulls fd 0 on demand, which this command never does.
  // The 1.x fixes re-derived on the rust2 side: #1639/#1643/#1644/#1645/
  // #1646, #1504/#1536/#1145/#1572 (see the ls.rs module header).
  return winux::rust2::run_command_with_stdin_streaming("ls", ctx.raw_args);
}
