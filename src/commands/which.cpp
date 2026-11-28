/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for which.
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

auto constexpr WHICH_OPTIONS = std::array{
    OPTION("-a", "--all", "print all matching pathnames of each argument"),
    // [GNU] -i: read aliases from stdin (same as --read-alias); hidden
    OPTION("-i", "", "", BOOL_TYPE),
    // [GNU] -v: print version (declared so --version interception applies)
    OPTION("-v", "--version", "print version information"),
    OPTION("-s", "--silent", "suppress all normal output"),
    OPTION("", "--quiet", "suppress all normal output"),
    OPTION("", "--skip-dot", "skip directories in PATH that start with a dot"),
    OPTION("", "--skip-tilde",
           "skip directories in PATH that start with a tilde and HOME matches"),
    OPTION("", "--show-dot",
           "if a directory in PATH starts with a dot, print ./COMMAND"),
    OPTION("", "--show-tilde", "output a tilde for the HOME directory"),
    OPTION("", "--tty-only", "only show results on a terminal"),
    OPTION("", "--read-alias", "read list of aliases from stdin"),
    OPTION("", "--skip-alias", "do not read aliases"),
    OPTION("", "--read-functions", "read shell functions from stdin"),
    OPTION("", "--skip-functions", "do not read shell functions")};

REGISTER_COMMAND(which, "which", "which [OPTION]... COMMAND...",
                 "Locate COMMAND in PATH.",
                 "  which ls\n"
                 "  which -a python",
                 "where(1), command(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", WHICH_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/which.rs, which_cmd).
  return winux::rust2::run_command("which", ctx.raw_args);
}
