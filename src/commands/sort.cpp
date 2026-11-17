// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sort.
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

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr SORT_OPTIONS = std::array{
    // [GNU] -b, --ignore-leading-blanks
    OPTION("-b", "--ignore-leading-blanks", "ignore leading blanks"),
    // [GNU] -d, --dictionary-order
    OPTION("-d", "--dictionary-order",
           "consider only blanks and alphanumeric characters"),
    // [GNU] -f, --ignore-case
    OPTION("-f", "--ignore-case", "fold lower case to upper case"),
    // [GNU] -g, --general-numeric-sort
    OPTION("-g", "--general-numeric-sort",
           "compare according to string numerical value"),
    // [GNU] -i, --ignore-nonprinting
    OPTION("-i", "--ignore-nonprinting", "consider only printable characters"),
    // [GNU] -h, --human-numeric-sort
    OPTION("-h", "--human-numeric-sort",
           "compare human readable numbers (e.g., 1K, 2M)"),
    // [GNU] -M, --month-sort
    OPTION("-M", "--month-sort", "compare as month names"),
    // [GNU] -m, --merge
    OPTION("-m", "--merge", "merge already sorted files"),
    // [GNU] -n, --numeric-sort
    OPTION("-n", "--numeric-sort",
           "compare according to string numerical value"),
    // [GNU] -V, --version-sort
    OPTION("-V", "--version-sort", "compare version numbers naturally"),
    // [GNU] -R, --random-sort
    OPTION("-R", "--random-sort", "sort by random hash of keys"),
    // [GNU] --random-source
    OPTION("", "--random-source", "get random bytes from FILE", STRING_TYPE),
    // [GNU] -r, --reverse
    OPTION("-r", "--reverse", "reverse the result of comparisons"),
    // [GNU] -S, --buffer-size
    OPTION("-S", "--buffer-size",
           "use SIZE for the main memory buffer; accepted as a memory hint",
           STRING_TYPE),
    // [GNU] --parallel
    OPTION("", "--parallel",
           "change the number of sorts run concurrently to N; accepted as a "
           "concurrency hint",
           STRING_TYPE),
    // [GNU] -s, --stable
    OPTION("-s", "--stable",
           "stabilize sort by disabling last-resort comparison"),
    // [GNU] -u, --unique
    OPTION("-u", "--unique", "output only the first of equal runs"),
    // [GNU] -z, --zero-terminated
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline"),
    // [GNU] --files0-from: read input from the file specified
    OPTION("", "--files0-from", "read input from the file specified",
           STRING_TYPE),
    // [GNU] -c takes no argument (a glued "-cquiet" is an invalid option);
    // --check accepts the optional =diagnose-first|quiet|silent argument.
    OPTION("-c", "", "check whether input is sorted; diagnose-first"),
    OPTION("", "--check",
           "check whether input is sorted; accepts =diagnose-first|quiet|"
           "silent",
           OPTIONAL_STRING_TYPE),
    // [GNU] -C is the short form of --check=quiet/--check=silent; GNU has
    // no "--check-silent" long option (#1355).
    OPTION("-C", "", "check whether input is sorted quietly"),
    // [GNU] --debug
    OPTION("", "--debug", "print sort key diagnostics to standard error"),
    // [GNU] -o, --output
    OPTION("-o", "--output", "write result to FILE instead of standard output",
           STRING_TYPE),
    // [GNU] option
    OPTION(
        "", "--files0-from",
        "read input from the files specified by NUL-terminated names in FILE",
        STRING_TYPE),
    // [GNU] --batch-size
    OPTION("", "--batch-size",
           "merge at most NMERGE inputs at once; accepted as a merge hint",
           STRING_TYPE),
    // [GNU] --compress-program
    OPTION("", "--compress-program",
           "compress temporaries with PROG; accepted as a compression hint",
           STRING_TYPE),
    // [GNU] -T, --temporary-directory
    OPTION("-T", "--temporary-directory",
           "use DIR for temporaries; accepted as a temporary-directory hint",
           STRING_TYPE),
    // [GNU] --sort
    OPTION("", "--sort", "set sort order; 'version' enables version sort",
           STRING_TYPE),
    // [GNU] -t, --field-separator
    OPTION("-t", "--field-separator",
           "use SEP instead of non-blank to blank transition", STRING_TYPE),
    // [GNU] -k, --key
    OPTION("-k", "--key", "sort via a key; KEYDEF has form F[.C][,F[.C]]",
           STRING_TYPE)};

REGISTER_COMMAND(sort, "sort", "sort [OPTION]... [FILE]...",
                 "Sort lines of text files.\n"
                 "With no FILE, or when FILE is -, read standard input.",
                 "  sort a.txt\n"
                 "  sort -n -r data.txt\n"
                 "  sort -u -k 1 names.txt",
                 "uniq(1), grep(1), head(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", SORT_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/sort.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // stdin is pulled on demand through the streaming ABI, so fd 0 stays
  // untouched until (and unless) the command actually reads it.
  return winux::rust2::run_command_with_stdin_streaming("sort", ctx.raw_args);
}
