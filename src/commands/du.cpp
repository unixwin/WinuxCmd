// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Implementation for du - estimate file space usage
/// @Version: 0.1.0
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

/**
 * @brief DU command options definition
 *
 * This array defines all the options supported by the du command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -a, @a --all: write counts for all files [IMPLEMENTED]
 * - @a -A, @a --apparent-size: print apparent sizes [IMPLEMENTED]
 * - @a -B, @a --block-size=SIZE: scale sizes by SIZE [IMPLEMENTED]
 * - @a -b,
 * @a --bytes: equivalent to --apparent-size --block-size=1 [IMPLEMENTED]
 * - @a -c, @a --total: produce a grand total [IMPLEMENTED]
 * - @a -d, @a
 * --max-depth=N: print the total for a directory only if it is N or fewer
 * levels below [IMPLEMENTED]
 * - @a -h, @a --human-readable: print sizes in powers of 1024 [IMPLEMENTED]
 * - @a -H, @a --dereference-args: dereference only command line symlink args
 * [ACCEPTED]
 * - @a --si: print sizes in powers of 1000 [IMPLEMENTED]
 * - @a -k: like --block-size=1K [IMPLEMENTED]
 * - @a -s, @a --summarize: display only a total for each argument [IMPLEMENTED]
 * - @a -t, @a --threshold=SIZE: exclude entries by size threshold
 * [IMPLEMENTED]
 * - @a --exclude=PATTERN: exclude files matching shell pattern [IMPLEMENTED]
 */
auto constexpr DU_OPTIONS = std::array{
    // [GNU]
    OPTION("-a", "--all", "write counts for all files, not just directories"),
    // [GNU]
    OPTION("-A", "--apparent-size", "print apparent sizes"),
    // [GNU]
    OPTION("-B", "--block-size", "scale sizes by SIZE before printing them",
           STRING_TYPE),
    // [GNU]
    OPTION("-b", "--bytes", "equivalent to '--apparent-size --block-size=1'"),
    // [GNU]
    OPTION("-c", "--total", "produce a grand total"),
    // [GNU]
    OPTION(
        "-d", "--max-depth",
        "print the total for a directory only if it is N or fewer levels below",
        INT_TYPE),
    // [GNU]
    OPTION("-h", "--human-readable",
           "print sizes in powers of 1024 (e.g., 1023M)"),
    // [GNU]
    OPTION("-H", "--dereference-args",
           "dereference only symlinks that are command line arguments"),
    // [GNU]
    OPTION("", "--si", "print sizes in powers of 1000 (e.g., 1.1G)"),
    // [GNU]
    OPTION("-k", "", "like --block-size=1K"),
    // [GNU]
    OPTION("-L", "--dereference", "dereference all symbolic links"),
    // [GNU]
    OPTION("-P", "--no-dereference",
           "don't follow any symbolic links (default)"),
    // [GNU]
    OPTION("-s", "--summarize", "display only a total for each argument"),
    // [GNU]
    OPTION("-t", "--threshold", "exclude entries smaller/greater than SIZE",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--exclude", "exclude files that match PATTERN", STRING_TYPE),
    // [GNU]
    OPTION("-x", "--one-file-system",
           "skip directories on different file systems"),
    // [GNU]
    OPTION("", "--inodes",
           "list inode usage information instead of block usage"),
    // [GNU]
    OPTION("", "--time",
           "show time of the last modification of any file in the directory; "
           "see --time-style",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("", "--time-style", "show timestamps using STYLE", STRING_TYPE),
    // [GNU]
    OPTION("-0", "--null", "end each output line with NUL, not newline"),
    // [GNU]
    OPTION("-D", "--dereference-args",
           "dereference only symlinks that are command line arguments"),
    // [GNU]
    OPTION("", "--files0-from",
           "summarize disk usage of the NUL-terminated file names specified in "
           "file F",
           STRING_TYPE),
    // [GNU]
    OPTION("-l", "--count-links", "count sizes many times if hard linked"),
    // [GNU]
    OPTION("-m", "", "like --block-size=1M"),
    // [GNU]
    OPTION("-S", "--separate-dirs",
           "for directories, do not include size of subdirectories"),
    // [GNU]
    OPTION("-X", "--exclude-from",
           "exclude files that match any pattern in FILE", STRING_TYPE)};

// ======================================================
// Pipeline components: moved to the rust2 library (#1488)
// ======================================================

REGISTER_COMMAND(
    du,
    /* name */
    "du",

    /* synopsis */
    "estimate file space usage",

    /* description */
    "The du command displays the amount of disk space used by the specified\n"
    "files and for each subdirectory (of directory arguments). If no path\n"
    "is given, the current directory is used.\n\n"
    "On Windows, it calculates the total size of all files in a directory\n"
    "tree recursively.",

    /* examples */
    "  du\n"
    "  du -h\n"
    "  du -sh /path/to/dir\n"
    "  du -d 1",

    /* see_also */
    "df(1)",

    /* author */
    "caomengxuan666",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    DU_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/du.rs, du_cmd); the C++ side keeps only
  // this registration. The traversal, allocation model (#1652-#1657: -L/-H
  // operand liveness, LX link targets, --files0-from), --time, dedupe and
  // scaling all move to Rust. The streaming entry is used because
  // --files0-from=- reads fd 0 lazily; a normal traversal never pulls it,
  // so an interactive `du` cannot block on a stdin slurp.
  return winux::rust2::run_command_with_stdin_streaming("du", ctx.raw_args);
}
