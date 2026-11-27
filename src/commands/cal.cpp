/*
 *  Copyright © 2026 WinuxCmd
 */

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import rust2_bridge;

auto constexpr CAL_OPTIONS = std::array{
    // [GNU] util-linux cal.c optstring "13mjn:sSywYvc:Vh" and the
    // longopts table (cal.c:306-325).  -w (week numbers), -v
    // (vertical), -c (columns), --color, --reform and --iso are not
    // implemented; see the audit report.
    OPTION("-1", "--one", "display only the requested month", BOOL_TYPE),
    OPTION("-3", "--three", "display previous, current and next month",
           BOOL_TYPE),
    OPTION("-s", "--sunday", "start week on Sunday", BOOL_TYPE),
    OPTION("-m", "--monday", "start week on Monday", BOOL_TYPE),
    OPTION("-j", "--julian", "display Julian days (day-of-year numbers)",
           BOOL_TYPE),
    OPTION("-n", "--months", "display NUM months starting at the request",
           STRING_TYPE),
    OPTION("-S", "--span", "span NUM months around the requested month",
           BOOL_TYPE),
    OPTION("-y", "--year", "display the whole year", BOOL_TYPE),
    OPTION("-Y", "--twelve", "display the next twelve months", BOOL_TYPE),
    OPTION("-V", "", "", BOOL_TYPE),
    OPTION("-h", "", "", BOOL_TYPE)};

REGISTER_COMMAND(cal,
                 /* name */
                 "cal",

                 /* synopsis */
                 "cal [OPTION] [[MONTH] YEAR]",

                 /* description */
                 "Display a calendar.\n"
                 "Display a calendar for the specified month or year. If no "
                 "arguments are given, display the current month.",

                 /* examples */
                 "cal\ncal 3 2024\ncal -3\ncal -y 2026",

                 /* see_also */
                 "date(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 CAL_OPTIONS) {
  // #1488 mechanical replacement: semantics live in rust2
  // (rust2/crates/winux-cmds/src/cal.rs, cal_cmd).
  return winux::rust2::run_command("cal", ctx.raw_args);
}
