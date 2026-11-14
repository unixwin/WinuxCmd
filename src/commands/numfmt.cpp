// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for numfmt command.
/// @Version: 0.2.0
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

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr NUMFMT_OPTIONS = std::array{
    // [GNU]
    OPTION("-d", "--delimiter",
           "use X instead of whitespace for field delimiter", STRING_TYPE),
    // [GNU]
    OPTION("", "--from",
           "auto-scale input numbers to UNITs; default is 'none'; see UNIT "
           "below",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--to", "auto-scale output numbers to UNITs; see UNIT below",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--round",
           "use METHOD for rounding when scaling; METHOD can be: up, down, "
           "from-zero (default), towards-zero, nearest",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--padding",
           "pad the output to N characters; positive N will right-align; "
           "negative N will left-align; padding is ignored if the output "
           "is wider than N; the default is to automatically pad if a "
           "whitespace is found",
           INT_TYPE),
    // [EXT]
    OPTION("", "--pad", "pad the output to N characters (same as --padding)",
           INT_TYPE),
    // [GNU] --suffix: the GNU text also promises accepting the suffix in
    // input numbers; that half is not implemented here, so it is not
    // documented.
    OPTION("", "--suffix", "add SUFFIX to output numbers", STRING_TYPE),
    // [GNU] --field accepts cut(1)-style ranges (N, N-M, N-, -M, -, and
    // comma separated lists); default is field 1 only.
    OPTION("", "--field",
           "replace the numbers in these input fields (default=1); see "
           "FIELDS below",
           STRING_TYPE),
    // [GNU]
    OPTION("-f", "--format",
           "use printf style floating-point FORMAT; see FORMAT below for "
           "details",
           STRING_TYPE),
    // [GNU] --header[=N]: optional value; a bare --header means N=1 and the
    // next argument stays an input operand (uutils #13272). #1519: the
    // value is xstrtoumax in GNU (numfmt.c:1558), so the option uses
    // the parser's unsigned optional type.
    OPTION("", "--header",
           "print (without converting) the first N header lines; N "
           "defaults to 1 if not specified",
           OPTIONAL_UINT_TYPE),
    // [GNU]
    OPTION("", "--grouping",
           "use locale-defined grouping of digits, e.g. 1,000,000", BOOL_TYPE),
    // [GNU]
    OPTION("", "--invalid",
           "failure mode for invalid numbers: MODE can be: abort "
           "(default), fail, warn, ignore",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--debug", "print conversion diagnostics", BOOL_TYPE),
    // [GNU] hidden developer option, spelled "---debug" on the command
    // line (numfmt.c {"-debug"}); aliases --debug. Empty description keeps
    // it out of --help like GNU.
    OPTION("", "---debug", "", BOOL_TYPE),
    // [GNU] --from-unit: specify the input unit size (default 1)
    OPTION("", "--from-unit",
           "specify the input unit size (instead of the default 1)",
           STRING_TYPE),
    // [GNU] --to-unit: the output unit size (default 1)
    OPTION("", "--to-unit", "the output unit size (instead of the default 1)",
           STRING_TYPE),
    // [GNU] --unit-separator: use STRING between number and unit
    OPTION("", "--unit-separator", "use STRING between number and unit",
           STRING_TYPE),
    // [GNU] --zero-terminated: line delimiter is NUL, not newline
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline",
           BOOL_TYPE),
    // [GNU] -M: use IEC units (powers of 1024)
    OPTION("-M", "", "use IEC units (powers of 1024)", BOOL_TYPE),
    // [GNU] -l: use locale grouping
    OPTION("-l", "", "use locale grouping", BOOL_TYPE)};
REGISTER_COMMAND(
    numfmt,
    /* cmd_name */ "numfmt",
    /* cmd_synopsis */ "numfmt [OPTION]... [NUMBER]...",
    /* cmd_desc */
    "Reformat NUMBER(s), or the numbers from standard input if none are\n"
    "specified.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options "
    "too.\n"
    "\n"
    "UNIT options:\n"
    "  none       no auto-scaling is done; suffixes will trigger an error\n"
    "  auto       accept optional single/two letter suffix:\n"
    "               1K = 1000,\n"
    "               1Ki = 1024,\n"
    "               1M = 1000000,\n"
    "               1Mi = 1048576,\n"
    "  si         accept optional single letter suffix:\n"
    "               1K = 1000,\n"
    "               1M = 1000000,\n"
    "               ...\n"
    "  iec        accept optional single letter suffix:\n"
    "               1K = 1024,\n"
    "               1M = 1048576,\n"
    "               ...\n"
    "  iec-i      accept optional two-letter suffix:\n"
    "               1Ki = 1024,\n"
    "               1Mi = 1048576,\n"
    "               ...\n"
    "\n"
    "FIELDS supports cut(1) style field ranges:\n"
    "  N    N'th field, counted from 1\n"
    "  N-   from N'th field, to end of line\n"
    "  N-M  from N'th to M'th field (inclusive)\n"
    "  -M   from first to M'th field (inclusive)\n"
    "  -    all fields\n"
    "Multiple fields/ranges can be separated with commas\n"
    "\n"
    "FORMAT must be suitable for printing one floating-point argument "
    "'%f'.\n"
    "Optional quote (%'f) will enable grouping of digits.\n"
    "Optional width value (%10f) will pad output. Optional zero (%010f) "
    "width\n"
    "will zero pad the number. Optional negative values (%-10f) will left "
    "align.\n"
    "Optional precision (%.1f) will override the input determined "
    "precision.\n"
    "\n"
    "Exit status is 0 if all input numbers were successfully converted.\n"
    "By default, numfmt will stop at the first conversion error with exit "
    "status 2.\n"
    "With --invalid='fail' a warning is printed for each conversion error\n"
    "and the exit status is 2.  With --invalid='warn' each conversion error "
    "is\n"
    "diagnosed, but the exit status is 0.  With --invalid='ignore' "
    "conversion\n"
    "errors are not diagnosed and the exit status is 0.\n"
    "\n"
    "Examples:\n"
    "  $ numfmt --to=si 1000\n"
    "            -> \"1.0K\"\n"
    "  $ numfmt --to=iec 2048\n"
    "           -> \"2.0K\"\n"
    "  $ numfmt --to=iec-i 4096\n"
    "           -> \"4.0Ki\"\n"
    "  $ echo 1K | numfmt --from=si\n"
    "           -> \"1000\"\n"
    "  $ echo 1K | numfmt --from=iec\n"
    "           -> \"1024\"\n"
    "  $ df -B1 | numfmt --header --field 2-4 --to=si\n"
    "  $ ls -l  | numfmt --header --field 5 --to=iec\n"
    "  $ ls -lh | numfmt --header --field 5 --from=iec --padding=10\n"
    "  $ ls -lh | numfmt --header --field 5 --from=iec --format %10f",
    /* examples */
    "  numfmt --to=si 1024\n"
    "  echo 1M | numfmt --from=si\n"
    "  numfmt --to=iec --padding=10 1024",
    /* see_also */ "human-readable, bytesize",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ NUMFMT_OPTIONS) {
  // #1488 mechanical replacement: the semantics live in the rust2 library
  // (rust2/crates/winux-cmds/src/numfmt.rs, numfmt_cmd); the C++ side
  // keeps only this registration -- the unit parser, --from/--to
  // converters, --format engine and the padding model are deleted.
  // stdin streams through the bridge's fd 0 pull callback
  // (run_command_with_stdin_streaming): only the no-operand form reads it,
  // and fd 0 is untouched otherwise. The 74-case GNU matrix
  // (--from/--to/--format/--padding family) gates the library side.
  // The 1.x acceptances GNU 9.4 rejects (--unit-separator, -M, -l,
  // ZYRQ unit suffixes) are not inherited.
  return winux::rust2::run_command_with_stdin_streaming("numfmt", ctx.raw_args);
}
