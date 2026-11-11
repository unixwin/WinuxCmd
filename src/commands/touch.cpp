// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
 * @brief TOUCH command options definition
 *
 * This array defines all the options supported by the touch command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -a: Change only the access time [IMPLEMENTED]
 * - @a -c, @a --no-create: Do not create any files [IMPLEMENTED]
 * - @a -d, @a --date: Parse STRING and use it instead of current time
 * [IMPLEMENTED]
 * - @a -f: (ignored) [IMPLEMENTED]
 * - @a -h, @a --no-dereference: Affect symbolic link instead of referenced file
 * [IMPLEMENTED]
 * - @a -m: Change only the modification time [IMPLEMENTED]
 * - @a -r, @a --reference: Use this file's times instead of current time
 * [IMPLEMENTED]
 * - @a -t: Use [[CC]YY]MMDDhhmm[.ss] instead of current time [IMPLEMENTED]
 * - @a --time: Change the specified time (access/atime/use/modify/mtime)
 * [IMPLEMENTED]
 */
auto constexpr TOUCH_OPTIONS = std::array{
    // [DIFFERS] -a
    OPTION("-a", "", "change only the access time"),
    // [DIFFERS] -c, --no-create
    OPTION("-c", "--no-create", "do not create any files"),
    // [DIFFERS] -d, --date
    OPTION("-d", "--date", "parse STRING and use it instead of current time",
           STRING_TYPE),
    // [DIFFERS] -f
    OPTION("-f", "", "(ignored)"),
    // [DIFFERS] -h, --no-dereference
    OPTION("-h", "--no-dereference",
           "affect symbolic link instead of referenced file"),
    // [DIFFERS] -m
    OPTION("-m", "", "change only the modification time"),
    // [DIFFERS] -r, --reference
    OPTION("-r", "--reference", "use this file's times instead of current time",
           STRING_TYPE),
    // [DIFFERS] -t
    OPTION("-t", "", "use [[CC]YY]MMDDhhmm[.ss] instead of current time",
           STRING_TYPE),
    // [DIFFERS] --time
    OPTION("", "--time",
           "change the specified time (access/atime/use/modify/mtime)",
           STRING_TYPE)};

REGISTER_COMMAND(touch, "touch", "touch [OPTION]... FILE...",
                 "Update the access and modification times of each FILE to the "
                 "current time.\n"
                 "A FILE argument that does not exist is created empty, unless "
                 "-c or -h is supplied.",
                 "  touch file.txt\n"
                 "  touch -a file.txt\n"
                 "  touch -m file.txt\n"
                 "  touch -c missing.txt\n"
                 "  touch -r ref.txt target.txt\n"
                 "  touch -h link.txt",
                 "stat(1), date(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 TOUCH_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/touch.rs, touch_cmd); the C++ FILETIME
  // pipeline and the -d/-t/-r timestamp parsers are deleted. ctx.raw_args
  // passes the operands verbatim because GNU touch.c parses its own getopt
  // run; touch never reads stdin, so the plain (stdin-less) bridge entry
  // carries the call.
  return winux::rust2::run_command("touch", ctx.raw_args);
}
