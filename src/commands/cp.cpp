// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implemention for cp.
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

/**
 * @brief CP command options definition
 *
 * This array defines all the options supported by the cp command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -a, @a --archive: Same as -dR --preserve=all [IMPLEMENTED]
 * - @a -b: Like --backup but does not accept an argument [IMPLEMENTED]
 * - @a -d: Same as --no-dereference --preserve=links [IMPLEMENTED]
 * - @a -f, @a --force: If an existing destination file cannot be opened, remove
 * it and try again [TODO]
 * - @a -i, @a --interactive: Prompt before overwrite [IMPLEMENTED]
 * - @a -H: Follow command-line symbolic links in SOURCE [TODO]
 * - @a -l, @a --link: Hard link files instead of copying [TODO]
 * - @a -L, @a --dereference: Always follow symbolic links in SOURCE [TODO]
 * - @a -n, @a --no-clobber: Do not overwrite an existing file and do not fail
 * [TODO]
 * - @a -P, @a --no-dereference: Never follow symbolic links in SOURCE
 * [IMPLEMENTED]
 * - @a -p: Same as --preserve=mode,ownership,timestamps [TODO]
 * - @a -R, @a --recursive: Copy directories recursively [IMPLEMENTED]
 * - @a -r, @a --recursive: Copy directories recursively [IMPLEMENTED]
 * - @a -s, @a --symbolic-link: Make symbolic links instead of copying [TODO]
 * - @a -S, @a --suffix: Override the usual backup suffix [IMPLEMENTED]
 * - @a -t, @a --target-directory: Copy all SOURCE arguments into DIRECTORY
 * [IMPLEMENTED]
 * - @a -T, @a --no-target-directory: Treat DEST as a normal file [IMPLEMENTED]
 * - @a -u: Equivalent to --update[=older] [IMPLEMENTED]
 * - @a -v, @a --verbose: Explain what is being done [IMPLEMENTED]
 * - @a -x, @a --one-file-system: Stay on this file system [TODO]
 * - @a -Z: Set SELinux security context of destination file to default type
 * [TODO]
 * - @a --parent, @a --parents: Use full source file name under DIRECTORY
 * [IMPLEMENTED]
 * - @a --debug: Explain how a file is copied [IMPLEMENTED AS VERBOSE]
 */

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

// [GNU] -a, --archive: same as -dR --preserve=all
// [GNU] -b: like --backup but does not accept an argument
// [GNU] --backup: make a backup of each existing destination file
// [GNU] -d: same as --no-dereference --preserve=links
// [GNU] -f, --force: remove existing destination and try again
// [GNU] -i, --interactive: prompt before overwrite
// [GNU] -H: follow command-line symbolic links in SOURCE
// [GNU] -l, --link: hard link files instead of copying
// [GNU] -L, --dereference: always follow symbolic links in SOURCE
// [GNU] -n, --no-clobber: do not overwrite an existing file
// [GNU] -P, --no-dereference: never follow symbolic links in SOURCE
// [GNU] -p: same as --preserve=mode,ownership,timestamps
// [GNU] -R, --recursive: copy directories recursively
// [GNU] -r, --recursive: copy directories recursively (alias for -R)
// [GNU] -s, --symbolic-link: make symbolic links instead of copying
// [GNU] -S, --suffix: override the usual backup suffix
// [GNU] -t, --target-directory: copy all SOURCE arguments into DIRECTORY
// [GNU] -T, --no-target-directory: treat DEST as a normal file
// [GNU] --strip-trailing-slashes: remove trailing slashes from SOURCE
// [GNU] -u, --update: equivalent to --update[=older]
// [GNU] -v, --verbose: explain what is being done
// [GNU] --debug: explain how a file is copied; implies --verbose
// [GNU] -x, --one-file-system: stay on this file system
// [DIFFERS] -Z: SELinux contexts are unavailable on Windows
// [IMPLEMENTED] --remove-destination: remove each existing destination file
// before open [GNU] --attributes-only: don't copy the file data, just the
// attributes [GNU] --parents: use full source file name under DIRECTORY [GNU]
// --parent: alias for --parents [GNU] --sparse: control creation of sparse
// files [GNU] --reflink: control clone/CoW copies [GNU] --preserve: preserve
// the specified attributes [GNU] --no-preserve: don't preserve the specified
// attributes [GNU] --copy-contents: copy contents of special files when
// recursive [IMPLEMENTED] --keep-directory-symlink: follow existing symlink to
// directory Windows directory operations follow an existing destination
// directory symlink.
auto constexpr CP_OPTIONS = std::array{
    OPTION("", "--keep-directory-symlink",
           "follow existing symlink to directory"),
    OPTION("-a", "--archive", "same as -dR --preserve=all"),
    OPTION("-b", "", "like --backup but does not accept an argument"),
    OPTION("", "--backup", "make a backup of each existing destination file",
           OPTIONAL_STRING_TYPE),
    OPTION("-d", "", "same as --no-dereference --preserve=links"),
    OPTION("-f", "--force",
           "if an existing destination file cannot be opened, remove it and "
           "try again"),
    OPTION("-i", "--interactive", "prompt before overwrite"),
    OPTION("-H", "", "follow command-line symbolic links in SOURCE"),
    OPTION("-l", "--link", "hard link files instead of copying"),
    OPTION("-L", "--dereference", "always follow symbolic links in SOURCE"),
    OPTION("-n", "--no-clobber",
           "do not overwrite an existing file and do not fail"),
    OPTION("-P", "--no-dereference", "never follow symbolic links in SOURCE"),
    OPTION("-p", "", "same as --preserve=mode,ownership,timestamps"),
    OPTION("-R", "--recursive", "copy directories recursively"),
    OPTION("-r", "--recursive", "copy directories recursively"),
    OPTION("-s", "--symbolic-link", "make symbolic links instead of copying"),
    OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
    OPTION("-t", "--target-directory",
           "copy all SOURCE arguments into DIRECTORY", STRING_TYPE),
    OPTION("-T", "--no-target-directory", "treat DEST as a normal file"),
    OPTION("", "--strip-trailing-slashes",
           "remove any trailing slashes from each SOURCE argument"),
    OPTION("-u", "--update", "equivalent to --update[=older]",
           OPTIONAL_STRING_TYPE),
    OPTION("-v", "--verbose", "explain what is being done"),
    OPTION("", "--debug", "explain how a file is copied; implies --verbose"),
    // [GNU] cp has no -g/--progress-bar; that is a WinuxCmd extension
    // dropped in #1411.
    OPTION("-x", "--one-file-system", "stay on this file system"),
    // [DIFFERS] SELinux security contexts are unavailable on Windows.
    OPTION("-Z", "",
           "set SELinux security context of destination file to default type"),
    OPTION(
        "", "--remove-destination",
        "remove each existing destination file before attempting to open it"),
    OPTION("", "--attributes-only",
           "don't copy the file data, just the attributes"),
    OPTION("", "--parents", "use full source file name under DIRECTORY"),
    OPTION("", "--parent", "use full source file name under DIRECTORY"),
    // [GNU] --path: deprecated compatibility alias for --parents (kept in
    // the GNU option table since the fileutils era) (#1066).
    OPTION("", "--path", "use full source file name under DIRECTORY"),
    OPTION("", "--sparse", "control creation of sparse files", STRING_TYPE),
    OPTION("", "--reflink", "control clone/CoW copies", OPTIONAL_STRING_TYPE),
    OPTION("", "--preserve", "preserve the specified attributes", STRING_TYPE),
    OPTION("", "--no-preserve", "don't preserve the specified attributes",
           STRING_TYPE),
    OPTION("", "--copy-contents",
           "copy contents of special files when recursive"),
    // [GNU] cp has no -c short option; the SELinux context option is
    // spelled -Z or --context only (#1355).
    OPTION("", "--context",
           "set SELinux security context of destination file to default type")};

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    cp,
    /* name */
    "cp",

    /* synopsis */
    "copy files and directories",

    /* description */
    "Copy SOURCE to DEST, or multiple SOURCE(s) to DIRECTORY.\n"
    "\n"
    "In the first form, copy SOURCE to DEST.\n"
    "In the second form, copy each SOURCE to DIRECTORY.",

    /* examples */
    "  cp file1.txt file2.txt       Copy file1.txt to file2.txt\n"
    "  cp -r dir1 dir2              Recursively copy dir1 to dir2\n"
    "  cp -v file.txt dir/           Verbose copy file.txt to dir/\n"
    "  cp -i file.txt file.txt       Interactive copy (prompt before "
    "overwrite)",

    /* see also */
    "mv(1), rm(1), ln(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    CP_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/cp.rs, cp::{opts,copy,winfs}); the C++
  // copy pipeline (link resolution, backup, prompt, preserve, win32 copy
  // ladder) is deleted. ctx.raw_args carries the operands verbatim because
  // GNU cp.c parses its own getopt run. The streaming entry feeds stdin on
  // demand, so `-i` prompts read the terminal/pipe exactly like GNU while
  // non-interactive runs never touch fd 0.
  return winux::rust2::run_command_with_stdin_streaming("cp", ctx.raw_args);
}
