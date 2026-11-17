// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Implementation for df - display disk space usage
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
 * @brief DF command options definition
 *
 * This array defines all the options supported by the df command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -h, @a --human-readable: print sizes in powers of 1024 [IMPLEMENTED]
 * - @a -H, @a --si: print sizes in powers of 1000 [IMPLEMENTED]
 * - @a -i, @a --inodes: list inode information instead of block usage
 * [IMPLEMENTED]
 * - @a -k: like --block-size=1K [IMPLEMENTED]
 * - @a -T, @a --print-type: print file system type [IMPLEMENTED]
 * - @a -B, @a --block-size=SIZE: scale sizes by SIZE [IMPLEMENTED]
 * - @a --total: produce a grand total [IMPLEMENTED]
 * - @a --output[=FIELD_LIST]: use GNU-style output fields [IMPLEMENTED]
 * - @a -a, @a --all: include all file systems [ACCEPTED]
 * - @a --sync, @a --no-sync: sync control [ACCEPTED]
 */
auto constexpr DF_OPTIONS = std::array{
    OPTION("-a", "--all", "include all file systems"),
    OPTION("-B", "--block-size", "scale sizes by SIZE before printing them",
           STRING_TYPE),
    OPTION("-h", "--human-readable",
           "print sizes in powers of 1024 (e.g., 1023M)"),
    OPTION("-H", "--si", "print sizes in powers of 1000 (e.g., 1.1G)"),
    OPTION("-i", "--inodes", "list inode information instead of block usage"),
    OPTION("-k", "", "like --block-size=1K"),
    // [GNU] -m is a synonym for --block-size=1M (uutils #11565)
    OPTION("-m", "", "like --block-size=1M"),
    OPTION("-l", "--local", "limit listing to local file systems"),
    OPTION("-T", "--print-type", "print file system type"),
    OPTION("-t", "--type", "limit listing to file systems of type TYPE",
           STRING_TYPE),
    // [GNU] -F: obsolete Solaris synonym for -t/--type; hidden
    OPTION("-F", "", "", STRING_TYPE),
    OPTION("-x", "--exclude-type",
           "limit listing to file systems not of type TYPE", STRING_TYPE),
    OPTION("", "--total", "produce a grand total"),
    OPTION("", "--sync", "invoke sync before getting usage info"),
    OPTION("", "--no-sync", "do not invoke sync before getting usage info"),
    OPTION("", "--output", "use the output format defined by FIELD_LIST",
           OPTIONAL_STRING_TYPE),
    OPTION("-P", "--portability", "use the POSIX output format"),
    // [GNU]
    // [DIFFERS] GNU: -v is "(ignored)"; WinuxCmd: was version display
    OPTION("-v", "", "(ignored) [DIFFERS: GNU ignores -v]")};

// ======================================================
// Pipeline components
// ======================================================

REGISTER_COMMAND(
    df,
    /* name */
    "df",

    /* synopsis */
    "report file system disk space usage",

    /* description */
    "The df command displays the amount of available disk space on file\n"
    "systems of which the invoking user has adequate read access.\n\n"
    "On Windows, it reports information about volumes that contain the\n"
    "specified paths, including total size, used space, and available space.",

    /* examples */
    "  df\n"
    "  df -h C:\\Users\n"
    "  df -k\n"
    "  df -T\n"
    "  df -t NTFS\n"
    "  df -x tmpfs\n"
    "  df -l",

    /* see_also */
    "du(1)",

    /* author */
    "caomengxuan666",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    DF_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/df.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // The command never reads stdin: the pass-nothing run_command contract
  // keeps fd 0 untouched for later pipeline stages.
  return winux::rust2::run_command("df", ctx.raw_args);
}
