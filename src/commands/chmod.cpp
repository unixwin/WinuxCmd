// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for chmod.
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
 * @brief CHMOD command options definition
 *
 * This array defines all the options supported by the chmod command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -c, @a --changes: Like verbose but report only when a change is made
 * [IMPLEMENTED]
 * - @a -f, @a --silent, @a --quiet: Suppress most error messages [IMPLEMENTED]
 * - @a -v, @a --verbose: Output a diagnostic for every file processed
 * [IMPLEMENTED]
 * - @a -R, @a --recursive: Change files and directories recursively
 * [IMPLEMENTED]
 * - @a --reference: Use RFILE's mode instead of MODE values [IMPLEMENTED]
 */
auto constexpr CHMOD_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-c", "--changes",
           "like verbose but report only when a change is made"),
    // [DIFFERS]
    OPTION("-f", "--silent", "suppress most error messages"),
    // [DIFFERS]
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
    // [DIFFERS]
    OPTION("-R", "--recursive", "change files and directories recursively"),
    // [DIFFERS]
    OPTION("", "--quiet", "suppress most error messages"),
    // [DIFFERS]
    OPTION("", "--reference", "use RFILE's mode instead of MODE values",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("-H", "", "traverse command-line symlinks to directories"),
    // [DIFFERS]
    OPTION("-L", "", "traverse every symlink to a directory"),
    // [DIFFERS]
    OPTION("-P", "", "do not traverse any symbolic links (default)"),
    // [DIFFERS]
    OPTION("", "--dereference", "affect the referent of each symbolic link"),
    // [DIFFERS] Windows file-attribute operations follow reparse points.
    OPTION("-h", "--no-dereference",
           "affect symbolic links instead of referenced files"),
    // [DIFFERS]
    OPTION("", "--preserve-root", "fail to operate recursively on '/'"),
    // [DIFFERS]
    OPTION("", "--no-preserve-root", "do not treat '/' specially")};

REGISTER_COMMAND(
    chmod, "chmod", "change file mode bits",
    "Change the mode of each FILE to MODE.\n"
    "\n"
    "Each MODE is of the form '[ugoa]*([-+=]([rwxXst]*|[ugo]))+'.\n"
    "\n"
    "  -H  traverse command-line symlinks to directories\n"
    "  -L  traverse every symlink to a directory\n"
    "  -P  do not traverse any symbolic links (default)\n"
    "\n"
    "Note: On Windows, this command simulates Unix permissions using\n"
    "file attributes. Write permission is mapped to the read-only attribute.",
    "  chmod 755 script.sh        Set permissions to rwxr-xr-x\n"
    "  chmod 644 file.txt        Set permissions to rw-r--r--\n"
    "  chmod u+x script.sh       Add execute for user\n"
    "  chmod go-w file.txt       Remove write for group and other\n"
    "  chmod -R 755 dir/         Recursively set permissions",
    "chown(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd", CHMOD_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/chmod.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // The command never reads stdin: the pass-nothing run_command contract
  // keeps fd 0 untouched for later pipeline stages.
  return winux::rust2::run_command("chmod", ctx.raw_args);
}
