// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for chgrp.
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

auto constexpr CHGRP_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-c", "--changes",
           "like verbose but report only when a change is made"),
    // [DIFFERS]
    OPTION("-f", "--silent", "suppress most error messages"),
    // [DIFFERS]
    OPTION("-f", "--quiet", "suppress most error messages"),
    // [DIFFERS]
    OPTION("-v", "--verbose", "output a diagnostic for every file processed"),
    // [DIFFERS]
    OPTION("-R", "--recursive", "operate on files and directories recursively"),
    // [DIFFERS]
    OPTION("-h", "--no-dereference",
           "affect symbolic links instead of any referenced file"),
    // [DIFFERS]
    OPTION("", "--dereference",
           "affect the referent of each symbolic link (default)"),
    // [DIFFERS]
    OPTION("-H", "", "command line symbolic links are followed"),
    // [DIFFERS]
    OPTION("-L", "", "indirect symbolic links are followed"),
    // [DIFFERS]
    OPTION("-P", "", "no symbolic links are followed (default)"),
    // [DIFFERS]
    OPTION("", "--from", "change only if current group is GROUP", STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--reference", "use RFILE's group instead of specifying GROUP",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--preserve-root", "fail to operate recursively on '/'"),
    // [DIFFERS]
    OPTION("", "--no-preserve-root", "do not treat '/' specially (default)")};

REGISTER_COMMAND(
    chgrp, "chgrp", "chgrp [OPTION]... GROUP FILE...",
    "Change the group of each FILE to GROUP.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -c, --changes          like verbose but report only when a change is "
    "made\n"
    "  -f, --silent           suppress most error messages\n"
    "  -f, --quiet            suppress most error messages\n"
    "  -v, --verbose          output a diagnostic for every file processed\n"
    "  -R, --recursive        operate on files and directories recursively\n"
    "  -h, --no-dereference   affect symbolic links instead of any referenced "
    "file\n"
    "      --dereference      affect the referent of each symbolic link "
    "(default)\n"
    "  -H                     command line symbolic links are followed\n"
    "  -L                     indirect symbolic links are followed\n"
    "  -P                     no symbolic links are followed (default)\n"
    "      --from=GROUP       change only if current group is GROUP\n"
    "      --reference=RFILE  use RFILE's group instead of specifying GROUP\n"
    "      --preserve-root    fail to operate recursively on '/'\n"
    "      --no-preserve-root do not treat '/' specially (default)\n"
    "\n"
    "Note: On Windows, this requires administrator privileges.\n"
    "The GROUP must be a valid Windows group name.",
    "  chgrp staff file.txt       change group to 'staff'\n"
    "  chgrp -R staff /tmp/dir    change group recursively\n"
    "  chgrp --reference=file f2  use file's group for f2",
    "chown(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CHGRP_OPTIONS) {

  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/chgrp.rs); the C++ side keeps only this
  // registration — the 1.x helper namespace and handler body are deleted.
  // ctx.raw_args carries the operands verbatim for the rust2 getopt layer.
  // The command never reads stdin: the pass-nothing run_command contract
  // keeps fd 0 untouched for later pipeline stages.
  return winux::rust2::run_command("chgrp", ctx.raw_args);
}
