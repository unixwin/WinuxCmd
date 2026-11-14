/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: sed registration (the implementation is the rust2 library, #1488)
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

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr SED_OPTIONS = std::array{
    // [GNU]
    OPTION("-n", "--quiet", "suppress automatic printing of pattern space"),
    // [GNU]
    OPTION("", "--silent", "alias for -n"),
    // [GNU]
    OPTION("-s", "--separate",
           "consider files as separate rather than as a single continuous "
           "long stream"),
    // [GNU]
    OPTION("-z", "--null-data", "separate lines by NUL characters"),
    // [GNU]
    OPTION("", "--zero-terminated", "alias for -z"),
    // [GNU]
    OPTION("-u", "--unbuffered", "buffer input and output minimally"),
    // [DIFFERS]
    OPTION("-b", "--binary", "open files in binary mode"),
    // [GNU]
    OPTION("-i", "--in-place", "edit files in place", OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-e", "--expression",
           "add the script to the commands to be executed", STRING_TYPE),
    // [GNU]
    OPTION("-f", "--file", "add the script from FILE", STRING_TYPE),
    // [GNU]
    OPTION("-l", "--line-length", "specify line-wrap length for the l command",
           INT_TYPE),
    // [GNU]
    OPTION("-E", "--regexp-extended", "use extended regular expressions"),
    // [DIFFERS] accepted for GNU compatibility; no execution annotation is
    // produced on this platform
    OPTION("", "--debug", "annotate program execution (accepted, no output)"),
    // [DIFFERS] accepted for GNU compatibility; symlinks are always opened
    // directly on this platform
    OPTION("", "--follow-symlinks",
           "follow symlinks when processing in place (accepted, no-op)"),
    // [GNU]
    OPTION("", "--sandbox", "restrict file system access in the script"),
    // [GNU]
    OPTION("", "--posix", "disable GNU extensions and follow POSIX sed"),
    // [GNU]
    OPTION("-r", "", "alias for -E")};

REGISTER_COMMAND(
    sed, "sed", "sed [OPTION]... {script} [FILE]...",
    "Apply basic sed scripts (s///, p, d, a, i, c) to each line of input.",
    "  sed \"s/foo/bar/\" file.txt\n"
    "  sed -n \"s/foo/bar/p\" file.txt",
    "grep, awk", "WinuxCmd", "Copyright © 2026 WinuxCmd", SED_OPTIONS) {
  // #1488 mechanical replacement: the stream editor lives in the rust2
  // library (rust2/crates/winux-cmds/src/sed.rs + sed/compile.rs +
  // sed/exec.rs, a structural GNU sed 4.9 port on winux-regex). The C++
  // side keeps only this registration for help/man/completion metadata;
  // ctx.raw_args reaches the library verbatim because sed.c owns its
  // getopt_long run and dispatcher pre-parse (option_policy_for_command
  // "sed": all_args_are_positionals), including -e/-f in-order
  // compilation, -i[SUF] expansion and --posix.
  //
  // stdin shape: sed reads fd 0 whenever no input-file operand stands (or
  // an operand is "-"), so the handler routes through ABI v2 STREAMING
  // stdin — the host pull callback stays lazy, which keeps fd 0 untouched
  // for the file-operand invocations and hands the pattern stream over in
  // 64 KiB segments instead of a whole-buffer slurp (REPLACEMENT-RECIPE
  // §5). The library emits the GNU read-error diagnostics itself on a
  // closed/bad stdin (#973 family).
  return winux::rust2::run_command_with_stdin_streaming("sed",
                                                        ctx.raw_args);
}
