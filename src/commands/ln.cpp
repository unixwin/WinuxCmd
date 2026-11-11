// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for ln.
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
 * @brief LN command options definition
 *
 * This array defines all the options supported by the ln command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -s, @a --symbolic: Make symbolic links instead of hard links
 * [IMPLEMENTED]
 * - @a -f, @a --force: Remove existing destination files [IMPLEMENTED]
 * - @a -v, @a --verbose: Print name of each linked file [IMPLEMENTED]
 * - @a -n, @a --no-dereference: Treat LINK_NAME as a normal file if it is a
 * symbolic link to a directory [NOT SUPPORT]
 */
auto constexpr LN_OPTIONS = std::array{
    // [GNU]
    OPTION("-s", "--symbolic", "make symbolic links instead of hard links"),
    // [GNU]
    OPTION("-f", "--force", "remove existing destination files"),
    // [GNU]
    OPTION("-v", "--verbose", "print name of each linked file"),
    // [DIFFERS] -n: on Windows, directory-symlink target detection uses
    // GetFileAttributesW; behaviour differs from GNU.
    OPTION("-n", "--no-dereference",
           "treat LINK_NAME as a normal file if it is a symbolic link to a "
           "directory"),
    // [GNU]
    OPTION("-i", "--interactive", "prompt whether to remove destinations"),
    // [DIFFERS] -L/--logical: on Windows, CreateHardLinkW and the default
    // file-open behaviour already dereference symlinks, making this flag
    // effectively a no-op for hard links.
    OPTION("-L", "--logical", "dereference TARGETs that are symbolic links"),
    // [DIFFERS] -P/--physical: on Windows, hard links are always created
    // directly to the target (symlinks are not followed); this is the
    // default behaviour.
    OPTION("-P", "--physical", "make hard links directly to symbolic links"),
    // [DIFFERS] --dereference: alias for -L; same Windows caveats apply.
    OPTION("", "--dereference", "dereference TARGETs that are symbolic links"),
    // [GNU]
    OPTION("-b", "", "like --backup but does not accept an argument"),
    OPTION("", "--backup", "make a backup of each existing destination file",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-S", "--suffix", "override the usual backup suffix", STRING_TYPE),
    // [DIFFERS] -r/--relative: on Windows, relative symlink targets are
    // computed using std::filesystem::relative which may differ from POSIX
    // path resolution.
    OPTION("-r", "--relative",
           "with -s, create links relative to link location"),
    // [GNU]
    OPTION("-t", "--target-directory",
           "specify the DIRECTORY in which to create the links", STRING_TYPE),
    // [GNU]
    OPTION("-T", "--no-target-directory",
           "treat LINK_NAME as a normal file always"),
    // [DIFFERS] -d/--directory: hard links to directories are not supported
    // on Windows; an error is reported at runtime.
    OPTION("-d", "--directory",
           "allow the hard link to be a directory (privileged)"),
    // [DIFFERS] -F: alias for -d; same Windows limitation applies.
    OPTION("-F", "", "allow hard link to directory (alias for -d)")};
REGISTER_COMMAND(
    ln, "ln", "make links between files",
    "Create links between files. By default, make hard links.\n"
    "\n"
    "  -s, --symbolic         make symbolic links instead of hard links\n"
    "  -f, --force            remove existing destination files\n"
    "  -i, --interactive      prompt whether to remove destinations\n"
    "  -b, --backup           make a backup of each existing destination\n"
    "  -S, --suffix=SUFFIX    override the usual backup suffix\n"
    "  -t, --target-directory=DIRECTORY  specify the target directory\n"
    "  -T, --no-target-directory  treat LINK_NAME as a normal file\n"
    "  -r, --relative         create links relative to link location\n"
    "\n"
    "On Windows, hard links and symbolic links are supported.\n"
    "Note: Creating symbolic links may require administrator privileges.",
    "  ln source link         Create a hard link\n"
    "  ln -s source link      Create a symbolic link\n"
    "  ln -sf source link     Force create, overwrite if exists\n"
    "  ln -sv source link     Verbose symbolic link creation\n"
    "  ln -t dir/ src1 src2   Create links in dir/\n"
    "  ln -b source link      Backup existing link before creating",
    "link(1), symlink(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    LN_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/ln.rs, ln_cmd); the C++ CreateHardLinkW /
  // create_symlink pipeline (with its ALLOW_UNPRIVILEGED_CREATE retry) is
  // deleted. ctx.raw_args passes the operands verbatim because GNU ln.c
  // parses its own getopt run. GNU ln reads stdin only to read
  // -i/--interactive prompt answers (prompt(), ln.c), so the whole-buffer
  // v1 entry is called exactly there.
  if (ctx.has("-i") || ctx.has("--interactive")) {
    return winux::rust2::run_command_with_stdin("ln", ctx.raw_args);
  }
  return winux::rust2::run_command("ln", ctx.raw_args);
}
