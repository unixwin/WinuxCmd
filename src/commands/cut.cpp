/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for cut.
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

auto constexpr CUT_OPTIONS = std::array{
    // [GNU]
    OPTION("-b", "--bytes", "select only these bytes", STRING_TYPE),
    // [GNU]
    OPTION("-c", "--characters", "select only these characters", STRING_TYPE),
    // [GNU]
    OPTION("-d", "--delimiter", "use DELIM instead of TAB for field delimiter",
           STRING_TYPE),
    // [GNU]
    OPTION("-f", "--fields", "select only these fields", STRING_TYPE),
    // [GNU]
    OPTION("", "--complement",
           "complement the set of selected bytes, characters or fields"),
    // [GNU] -n is "(ignored)" in GNU cut (cut.c:159, 549-550). The
    // former WinuxCmd --no-partial semantics and the impl-only
    // -w/-F/-O/-M options were removed by the #1462 audit: GNU
    // rejects those spellings outright.
    OPTION("-n", "", "do not split multibyte characters (ignored)"),
    // [GNU]
    OPTION("-s", "--only-delimited",
           "do not print lines not containing delimiter"),
    // [GNU]
    OPTION("", "--output-delimiter", "use STRING as the output delimiter",
           STRING_TYPE),
    // [GNU]
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};
REGISTER_COMMAND(cut, "cut", "cut OPTION... [FILE]...",
                 "Print selected parts of lines from each FILE to standard "
                 "output.",
                 "  cut -f1,3 file.txt\n"
                 "  cut -d, -f2 data.csv\n"
                 "  cut -z -f1 -d: list.txt",
                 "paste(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 CUT_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/cut.rs, cut_cmd — a cut.c port: LIST
  // grammar/diagnostics, byte selection, the delim == record-delimiter
  // character-stream shape from #1511, and the record loop). The C++ 1.x
  // record pipeline is deleted; REGISTER_COMMAND keeps the help/man
  // metadata. ctx.raw_args passes the operands verbatim because GNU cut.c
  // parses its own getopt run ("b:c:d:f:nsz").
  //
  // stdin shape: cut reads fd 0 when there is no FILE operand or an
  // operand is "-", so the handler routes through ABI v2 STREAMING stdin
  // — the host pull callback stays lazy, so file-operand invocations never
  // touch fd 0 and stdin arrives in bounded segments instead of a
  // whole-buffer slurp (REPLACEMENT-RECIPE §5). The library owns the GNU
  // read-error diagnostics for a closed stdin (#973 family).
  return winux::rust2::run_command_with_stdin_streaming("cut", ctx.raw_args);
}
