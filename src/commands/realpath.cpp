// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
/// @Description: Implementation for realpath - print the absolute path of files
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

auto constexpr REALPATH_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-e", "--canonicalize-existing",
           "all components of the path must exist"),
    // [EXT] not in GNU coreutils realpath
    OPTION("-E", "--canonicalize", "all components but the last must exist"),
    // [DIFFERS]
    OPTION("-m", "--canonicalize-missing", "no path components need to exist"),
    // [DIFFERS]
    OPTION("-L", "--logical", "resolve '..' and '.' before symlinks"),
    // [DIFFERS]
    OPTION("-P", "--physical", "resolve symlinks before '..' and '.'"),
    // [DIFFERS]
    OPTION("-q", "--quiet", "suppress error messages"),
    OPTION("-s", "--strip", "do not expand symlinks"),
    OPTION("", "--no-symlinks", "do not expand symlinks"),
    // [DIFFERS]
    OPTION("", "--relative-to", "print the resolved path relative to DIR",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--relative-base", "print relative paths below DIR",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("-z", "--zero", "end output with NUL byte instead of newline")};

REGISTER_COMMAND(
    realpath,
    /* name */
    "realpath",

    /* synopsis */
    "print the resolved absolute path",
    "Print the resolved absolute path for each FILE. A FILE operand\n"
    "is required.\n\n"
    "By default, all but the last component of each path must exist,\n"
    "and symlinks are followed.\n\n"
    "Options:\n"
    "  -E, --canonicalize           all but the last component must exist\n"
    "  -m, --canonicalize-missing   no path components need to exist\n"
    "  -e, --canonicalize-existing   all components must exist\n"
    "  -L, --logical                 resolve '..' before symlinks (accepted)\n"
    "  -P, --physical                resolve symlinks before '..' (accepted)\n"
    "  -q, --quiet                   suppress most error messages\n"
    "  -s, --strip, --no-symlinks    do not expand symlinks\n"
    "      --relative-to=DIR         print the resolved path relative to DIR\n"
    "      --relative-base=DIR       print relative paths below DIR\n"
    "  -z, --zero                    end output with NUL instead of newline",
    "  realpath /tmp/../etc/passwd\n"
    "  realpath -s /tmp/\n"
    "  realpath file.txt",
    "readlink(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd",
    REALPATH_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/realpath.rs, realpath_cmd); the C++ side
  // keeps only this registration — the canonicalization pipeline and the
  // relativize step are deleted. ctx.raw_args passes the operands
  // verbatim because GNU realpath.c parses its own getopt run. Resolved
  // paths are DATA and reach stdout through the bridge's raw
  // (non-translating) writers.
  return winux::rust2::run_command("realpath", ctx.raw_args);
}
