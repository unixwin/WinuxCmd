// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 WinuxCmd
/// @Description: Implementation for find (rust2 library registration).
/// @Version: 0.2.0
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
 * @brief FIND command options definition
 *
 * This array defines all the options supported by the find command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -name: Base of file name (the path with the leading directories
 * removed)
 * matches shell pattern PATTERN [IMPLEMENTED]
 * - @a -iname: Like
 * -name, but the match is case insensitive [IMPLEMENTED]
 * - @a -path: File
 * name matches shell pattern PATTERN [IMPLEMENTED]
 * - @a -ipath: Like -path,
 * but the match is case insensitive [IMPLEMENTED]
 * - @a -type: File is of
 *
 * type c: b,d,p,f,l,s,D [only d,f,l,p are supported]
 * [IMPLEMENTED]
 * - @a
 * -size: File uses n units of space [IMPLEMENTED]
 * - @a -empty: File is empty
 * and is either a regular file or a directory
 * [IMPLEMENTED]
 * - @a -mtime:
 * File data was last modified n*24 hours ago [IMPLEMENTED]
 * - @a -mmin: File
 * data was last modified n minutes ago [IMPLEMENTED]
 * - @a
 * -mindepth:
 * Descend at least LEVELS levels of directories before tests
 * [IMPLEMENTED]

 * * - @a -maxdepth: Descend at most LEVELS levels of directories below
 * starting-points [IMPLEMENTED]
 * - @a -print: Print the full file name on the standard output [IMPLEMENTED]
 * - @a -print0: Print the full file name on the standard output, followed by a
 * null character [IMPLEMENTED]
 * - @a -L: Follow symbolic links [IMPLEMENTED]
 * - @a -H: Do not follow
 * symbolic links, except while processing command line arguments [IMPLEMENTED]
 * - @a -P: Never follow symbolic links (default) [IMPLEMENTED]
 * - @a -delete: Delete files [IMPLEMENTED]
 * - @a -exec: Execute command
 * [IMPLEMENTED: ; and {} + forms]
 * - @a -ok: Execute command after
 * confirmation [IMPLEMENTED: ; form]
 * -
 * @a -printf: Print format [PARTIAL:
 %p,%f,%h,%l,%H,%P,%y,%Y,%s,%b,%k,%d,%D,%F,%g,%G,%o,%u,%U,%S,%m,%M,%n,%i,%A@,%B@,%C@,%T@,%AY,%Am,%Ad,%AH,%AM,%AS,%Aj,%BY,%Bm,%Bd,%BH,%BM,%BS,%Bj,%CY,%Cm,%Cd,%CH,%CM,%CS,%Cj,%TY,%Tm,%Td,%TH,%TM,%TS,%Tj,%%]
 * - @a -prune:

 * * Prune tree [IMPLEMENTED]
 * - @a -quit: Exit immediately [IMPLEMENTED]
 * -
 * @a -true: Always true [IMPLEMENTED]
 * - @a -false: Always false
 * [IMPLEMENTED]
 * - @a -regex: Whole path matches regular expression
 * [IMPLEMENTED]
 * - @a -iregex: Like -regex, but case insensitive
 * [IMPLEMENTED]
 * - @a -newer: File was modified more recently than reference
 * [IMPLEMENTED]
 * - @a -depth: Process directory contents before the directory
 * [IMPLEMENTED]
 */
auto constexpr FIND_OPTIONS = std::array{
    // [GNU]
    OPTION("-name", "",
           "base of file name (the path with the leading directories removed) "
           "matches shell pattern PATTERN",
           STRING_TYPE),
    // [GNU]
    OPTION("-iname", "", "like -name, but the match is case insensitive",
           STRING_TYPE),
    // [GNU]
    OPTION("-path", "", "file name matches shell pattern PATTERN", STRING_TYPE),
    // [GNU]
    OPTION("-ipath", "", "like -path, but the match is case insensitive",
           STRING_TYPE),
    // [GNU]
    OPTION("-wholename", "", "same as -path", STRING_TYPE),
    // [GNU]
    OPTION("-iwholename", "", "same as -ipath, but case insensitive",
           STRING_TYPE),
    // [GNU]
    OPTION("-type", "",
           "file is of type c: b,c,d,p,f,l,s,D [D: Solaris doors are not "
           "supported on this platform]",
           STRING_TYPE),
    // [GNU]
    OPTION("-xtype", "",
           "like -type but checks the opposite symlink resolution state "
           "[D: Solaris doors are not supported on this platform]",
           STRING_TYPE),
    // [GNU]
    OPTION("-execdir", "",
           "execute command from the subdirectory containing the matched file",
           TERMINATED_STRING_TYPE),
    // [GNU] -D/--debug (Windows diagnostics differ from findutils categories)
    OPTION("-D", "--debug",
           "print find traversal diagnostics [DIFFERS: Windows diagnostics]",
           STRING_TYPE),
    // [GNU] warning and readdir-race policy controls
    OPTION("-nowarn", "", "turn off file traversal warnings"),
    OPTION("-warn", "", "turn on file traversal warnings"),
    OPTION("-ignore_readdir_race", "",
           "ignore errors when entries disappear during traversal"),
    OPTION("-noignore_readdir_race", "",
           "report errors when entries disappear during traversal"),
    // [GNU]
    OPTION("-nouser", "", "file has no user corresponding to its owner ID"),
    // [GNU]
    OPTION("-used", "",
           "file was last accessed n days after its status was last changed",
           STRING_TYPE),
    // [GNU]
    OPTION("-perm", "",
           "file's permission bits match MODE [PARTIAL: octal modes only]",
           STRING_TYPE),
    // [GNU]
    OPTION("-readable", "", "file can be read"),
    // [GNU]
    OPTION("-writable", "", "file can be written"),
    // [GNU]
    OPTION("-executable", "", "file can be executed or searched"),
    // [GNU]
    OPTION("-inum", "", "file has inode/file-index number N", STRING_TYPE),
    // [GNU]
    OPTION("-links", "", "file has N hard links", STRING_TYPE),
    // [GNU]
    OPTION("-user", "", "file is owned by user name or UID", STRING_TYPE),
    // [GNU]
    OPTION("-group", "", "file belongs to group name or GID", STRING_TYPE),
    OPTION("-nogroup", "",
           "file has no group corresponding to its group ID [DIFFERS: Windows "
           "account mapping]"),
    // [GNU]
    OPTION("-uid", "", "file owner's numeric UID matches N", STRING_TYPE),
    // [GNU]
    OPTION("-gid", "", "file group's numeric GID matches N", STRING_TYPE),
    // [GNU]
    OPTION("-size", "", "file uses n units of space", STRING_TYPE),
    // [GNU]
    OPTION("-empty", "",
           "file is empty and is either a regular file or a directory"),
    // [GNU]
    OPTION("-amin", "", "file was last accessed n minutes ago", STRING_TYPE),
    // [GNU]
    OPTION("-atime", "", "file was last accessed n*24 hours ago", STRING_TYPE),
    // [GNU]
    OPTION("-cmin", "", "file status was last changed n minutes ago",
           STRING_TYPE),
    // [GNU]
    OPTION("-ctime", "", "file status was last changed n*24 hours ago",
           STRING_TYPE),
    // [GNU]
    OPTION("-mtime", "", "file data was last modified n*24 hours ago",
           STRING_TYPE),
    // [GNU]
    OPTION("-mmin", "", "file data was last modified n minutes ago",
           STRING_TYPE),
    // [GNU]
    OPTION("-mindepth", "",
           "descend at least LEVELS levels of directories before tests",
           INT_TYPE),
    // [GNU]
    OPTION("-maxdepth", "",
           "descend at most LEVELS levels of directories below starting-points",
           INT_TYPE),
    // [GNU]
    OPTION("-print", "", "print the full file name on the standard output"),
    // [GNU]
    OPTION("-print0", "",
           "print the full file name on the standard output, followed by a "
           "null character"),
    // [GNU]
    OPTION("-fprint", "", "print the full file name into FILE", STRING_TYPE),
    // [GNU]
    OPTION("-fprint0", "",
           "print the full file name into FILE, followed by a null character",
           STRING_TYPE),
    // [GNU]
    OPTION("-L", "", "follow symbolic links"),
    // [GNU]
    OPTION("-H", "",
           "do not follow symbolic links, except while processing command line "
           "arguments"),
    // [GNU]
    OPTION("-P", "", "never follow symbolic links (default)"),
    // [GNU]
    OPTION("-follow", "", "dereference symbolic links"),
    // [GNU]
    OPTION("-O", "", "set optimization level (0-3, currently ignored)",
           INT_TYPE),
    // [GNU]
    OPTION("-delete", "", "delete files"),
    // [GNU]
    OPTION("-exec", "", "execute command", TERMINATED_STRING_TYPE),
    // [GNU]
    OPTION("-ok", "", "execute command after confirmation",
           TERMINATED_STRING_TYPE),
    OPTION("-okdir", "",
           "execute command from the subdirectory after confirmation [DIFFERS: "
           "Windows command prompt]",
           TERMINATED_STRING_TYPE),
    OPTION("-ls", "",
           "list current file in ls -dils format [DIFFERS: Windows metadata]"),
    OPTION("-fls", "",
           "list current file in ls -dils format into FILE [DIFFERS: Windows "
           "metadata]",
           STRING_TYPE),
    // [GNU]
    OPTION(
        "-printf", "",
        "print format [PARTIAL: "
        "%p,%f,%h,%l,%H,%P,%y,%Y,%s,%b,%k,%d,%D,%F,%g,%G,%o,%u,%U,%S,%m,%M,%n,%"
        "i,%a,%c,%t,%A@,%B@,%C@,%T@,%AY,%Am,%Ad,%AH,%AM,%AS,%Aj,%BY,%Bm,%Bd,%"
        "BH,%BM,%BS,"
        "%Bj,%CY,%Cm,%Cd,%CH,%CM,%CS,%Cj,%TY,%Tm,%Td,%TH,%TM,%TS,%Tj,%%]",
        STRING_TYPE),
    // [GNU]
    OPTION("-fprintf", "", "print format into FILE", STRING_TYPE),
    // [GNU]
    OPTION("-prune", "", "prune tree"),
    // [GNU]
    OPTION("-quit", "", "exit immediately"),
    // [GNU]
    OPTION("-true", "", "always true"),
    // [GNU]
    OPTION("-false", "", "always false"),
    // [GNU]
    OPTION("-regex", "", "whole path matches regular expression", STRING_TYPE),
    // [GNU]
    OPTION("-iregex", "",
           "whole path matches regular expression, case insensitive",
           STRING_TYPE),
    // [GNU]
    OPTION("-newer", "",
           "file was modified more recently than the reference file",
           STRING_TYPE),
    OPTION("-anewer", "",
           "file was accessed more recently than the reference file",
           STRING_TYPE),
    OPTION("-cnewer", "",
           "file status changed more recently than the reference file "
           "[DIFFERS: Windows change time]",
           STRING_TYPE),
    OPTION("-fstype", "",
           "file is on a file system of type TYPE [DIFFERS: Windows volume "
           "file system name]",
           STRING_TYPE),
    OPTION(
        "-ilname", "",
        "symbolic link target matches shell pattern PATTERN, case insensitive",
        STRING_TYPE),
    OPTION("-lname", "", "symbolic link target matches shell pattern PATTERN",
           STRING_TYPE),
    // [GNU]
    OPTION("-neweraa", "", "compare access time to reference access time",
           STRING_TYPE),
    // [GNU]
    OPTION("-neweraB", "", "compare access time to reference birth time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerac", "", "compare access time to reference change time",
           STRING_TYPE),
    // [GNU]
    OPTION("-neweram", "", "compare access time to reference modify time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerat", "", "compare access time to literal time", STRING_TYPE),
    // [GNU]
    OPTION("-newerBa", "", "compare birth time to reference access time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerBB", "", "compare birth time to reference birth time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerBc", "", "compare birth time to reference change time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerBm", "", "compare birth time to reference modify time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerBt", "", "compare birth time to literal time", STRING_TYPE),
    // [GNU]
    OPTION("-newerca", "", "compare change time to reference access time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newercB", "", "compare change time to reference birth time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newercc", "", "compare change time to reference change time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newercm", "", "compare change time to reference modify time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newerct", "", "compare change time to literal time", STRING_TYPE),
    // [GNU]
    OPTION("-newerma", "", "compare modify time to reference access time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newermB", "", "compare modify time to reference birth time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newermc", "", "compare modify time to reference change time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newermm", "", "compare modify time to reference modify time",
           STRING_TYPE),
    // [GNU]
    OPTION("-newermt", "", "compare modify time to literal time", STRING_TYPE),
    // [GNU]
    OPTION("-samefile", "",
           "file refers to the same file as the reference file", STRING_TYPE),
    // [GNU]
    OPTION("-files0-from", "", "read starting points from NUL-delimited file",
           STRING_TYPE),
    // [GNU]
    OPTION("-depth", "",
           "process each directory's contents before the directory itself"),
    // [GNU]
    OPTION("-d", "", "same as -depth"),
    // [GNU]
    OPTION("-daystart", "", "measure times from start of today"),
    // [GNU]
    OPTION("-mount", "", "do not descend into other file systems"),
    // [GNU]
    OPTION("-xdev", "", "same as -mount"),
    // [GNU]
    OPTION("-noleaf", "", "do not optimize by assuming 2+ hard links"),
    // [GNU]
    OPTION("-regextype", "", "set regex syntax for later -regex/-iregex",
           STRING_TYPE),
    // [GNU]
    OPTION("!", "", "negate expression"),
    // [GNU]
    OPTION("-not", "", "negate expression"),
    // [GNU]
    OPTION("-a", "", "and expression"),
    // [GNU]
    OPTION("-and", "", "and expression"),
    // [GNU]
    OPTION("-o", "", "or expression"),
    // [GNU]
    OPTION("-or", "", "or expression"),
    // [GNU] findutils accepts -help/-version (single dash) like every
    // other predicate-style option.
    OPTION("-help", "", "display this help and exit"),
    // [GNU]
    OPTION("-version", "", "output version information and exit"),
    // [GNU] SELinux context predicate: on a system without SELinux GNU
    // find reports "invalid predicate -context: SELinux is not enabled."
    OPTION("-context", "", "file matches SELinux security context CTX",
           STRING_TYPE)};

REGISTER_COMMAND(find, "find", "find [path...] [expression]",
                 "Search for files in a directory hierarchy.\n"
                 "If no path is given, '.' is used.",
                 "  find . -name '*.cpp'\n"
                 "  find src -type f -maxdepth 2\n"
                 "  find . -iname 'readme*'",
                 "grep(1), ls(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FIND_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/find.rs + find/); the 1.x C++ traversal,
  // predicate evaluator, -printf renderer and -exec pipeline are deleted.
  // ctx.raw_args passes the operands verbatim because GNU findutils parses
  // its own leading options and expression language off argv (no getopt
  // permutation; policy in command_context.cppm keeps every dash token an
  // operand for this command). The dispatcher's --help/--version
  // interception is disabled for find (dispatcher.cppm): both spellings,
  // and the single-dash -help/-version forms, belong to find's expression
  // parser, which prints the GNU help/version blocks byte-for-byte.
  // The streaming stdin entry is the right bridge route for find: the
  // command reads fd 0 ONLY for `-ok`/`-okdir` prompt replies and for
  // `-files0-from -`, both lazily and late in the run. The v1 whole-buffer
  // entry would block on a slurp before find ever starts (and return EOF
  // for the prompt), and the v1 allowlist has no way to express "reads fd 0
  // only in some shapes". The pull callback keeps stdin untouched for the
  // common traversal, so pipeline consumers and -exec children still own
  // the live handle.
  return winux::rust2::run_command_with_stdin_streaming("find",
                                                       ctx.raw_args);
}
