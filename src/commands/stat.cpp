// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for stat.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"

#include "core/command_macros.h"

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// [GNU] -L, --dereference: follow symbolic links
// [GNU] -f, --file-system: display file system status
// [GNU] -c, --format: use specified FORMAT
// [GNU] -t, --terse: print information in terse form
// [GNU] --printf: like --format with backslash escapes
// [DIFFERS] --cached: GNU cache controls are unavailable on Windows
auto constexpr STAT_OPTIONS = std::array{
    OPTION("-L", "--dereference", "follow links", BOOL_TYPE),
    OPTION("-f", "--file-system",
           "display file system status instead of file status", BOOL_TYPE),
    OPTION("-c", "--format",
           "use the specified FORMAT instead of the default; output a "
           "newline after each use of FORMAT",
           STRING_TYPE),
    OPTION("-t", "--terse", "print the information in terse form", BOOL_TYPE),
    OPTION("", "--printf",
           "like --format, but interpret backslash escapes, and do not "
           "output a mandatory trailing newline; if you want a newline, "
           "include \\n in FORMAT",
           STRING_TYPE),
    // [GNU] --cached is a required-argument caching hint (never/always/
    // default). It is accepted as a no-op on Windows.
    OPTION("", "--cached",
           "specify how to use cached attributes; see MODE below (hint "
           "only; attributes are always queried fresh on Windows)",
           STRING_TYPE)};
REGISTER_COMMAND(stat, "stat", "stat [OPTION]... FILE...",
                 "Display file or file system status.\n"
                 "\n"
                 "Mandatory arguments to long options are mandatory for short "
                 "options too.\n"
                 "\n"
                 "The MODE argument of --cached can be: always, never, or "
                 "default.\n"
                 "'always' will use cached attributes if available, while\n"
                 "'never' will try to synchronize with the latest attributes, "
                 "and\n"
                 "'default' will leave it up to the underlying file system.  "
                 "On\n"
                 "Windows attributes are queried fresh, so --cached is "
                 "accepted as\n"
                 "a hint only.\n"
                 "\n"
                 "The valid format sequences for files (without "
                 "--file-system):\n"
                 "\n"
                 "  %a   permission bits in octal (note '#' and '0' printf "
                 "flags)\n"
                 "  %A   permission bits and file type in human readable "
                 "form\n"
                 "  %b   number of blocks allocated (see %B)\n"
                 "  %B   the size in bytes of each block reported by %b\n"
                 "  %d   device number in decimal (st_dev)\n"
                 "  %D   device number in hex (st_dev)\n"
                 "  %Hd  major device number in decimal\n"
                 "  %Ld  minor device number in decimal\n"
                 "  %f   raw mode in hex\n"
                 "  %F   file type\n"
                 "  %g   group ID of owner\n"
                 "  %G   group name of owner\n"
                 "  %h   number of hard links\n"
                 "  %i   inode number\n"
                 "  %m   mount point\n"
                 "  %n   file name\n"
                 "  %N   quoted file name with dereference if symbolic link\n"
                 "  %o   optimal I/O transfer size hint\n"
                 "  %s   total size, in bytes\n"
                 "  %r   device type in decimal (st_rdev)\n"
                 "  %R   device type in hex (st_rdev)\n"
                 "  %Hr  major device type in decimal, for character/block "
                 "device special files\n"
                 "  %Lr  minor device type in decimal, for character/block "
                 "device special files\n"
                 "  %t   major device type in hex, for character/block device "
                 "special files\n"
                 "  %T   minor device type in hex, for character/block device "
                 "special files\n"
                 "  %u   user ID of owner\n"
                 "  %U   user name of owner\n"
                 "  %w   time of file birth, human-readable; - if unknown\n"
                 "  %W   time of file birth, seconds since Epoch; 0 if "
                 "unknown\n"
                 "  %x   time of last access, human-readable\n"
                 "  %X   time of last access, seconds since Epoch\n"
                 "  %y   time of last data modification, human-readable\n"
                 "  %Y   time of last data modification, seconds since Epoch\n"
                 "  %z   time of last status change, human-readable\n"
                 "  %Z   time of last status change, seconds since Epoch\n"
                 "\n"
                 "Valid format sequences for file systems:\n"
                 "\n"
                 "  %a   free blocks available to non-superuser\n"
                 "  %b   total data blocks in file system\n"
                 "  %c   total file nodes in file system\n"
                 "  %d   free file nodes in file system\n"
                 "  %f   free blocks in file system\n"
                 "  %i   file system ID in hex\n"
                 "  %l   maximum length of filenames\n"
                 "  %n   file name\n"
                 "  %s   block size (for faster transfers)\n"
                 "  %S   fundamental block size (for block counts)\n"
                 "  %t   file system type in hex\n"
                 "  %T   file system type in human readable form\n"
                 "\n"
                 "--terse is equivalent to the following FORMAT:\n"
                 "    %n %s %b %f %u %g %D %i %h %t %T %X %Y %Z %W %o\n"
                 "--terse --file-system is equivalent to the following "
                 "FORMAT:\n"
                 "    %n %i %l %t %s %S %b %f %a %c %d",
                 "  stat file.txt\n"
                 "  stat -t file.txt\n"
                 "  stat -c %s file.txt",
                 "ls(1), find(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 STAT_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/stat.rs, stat_cmd); the C++ FILE_BASIC_INFO
  // pipeline and format renderer are deleted. ctx.raw_args passes the
  // operands verbatim because GNU stat.c parses its own getopt run; stat
  // never reads stdin, so the plain (stdin-less) bridge entry carries the
  // call.
  return winux::rust2::run_command("stat", ctx.raw_args);
}
