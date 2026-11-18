// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for date.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

// Standard library symbols (std::regex, std::istringstream, ...) come from the
// `import std;` below. Do NOT also #include standard C++ headers in a
// translation unit that imports std: the unity build merges this file with the
// other commands, and the MSVC STL headers then define std:: symbols a second
// time, failing with C2995/C2011/C2953 redefinitions raised from
// __msvc_heap_algorithms.hpp, __msvc_bit_utils.hpp and <limits>.
// pgrep.cpp/pkill.cpp use std::regex, and a dozen other commands use
// std::stringstream, all without including <regex>/<sstream> - which is the
// established convention here.

import std;
import core;
import utils;
import rust2_bridge;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief DATE command options definition
 *
 * This array defines all the options supported by the date command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -d, @a --date: Display time described by STRING, not 'now' [IMPLEMENTED]
 * - @a -u, @a --utc: Coordinated Universal Time (UTC) [IMPLEMENTED]
 * - @a -R, @a --rfc-2822: Output RFC 2822 compliant date string [IMPLEMENTED]
 * - @a -I, @a --iso-8601: Output ISO 8601 date/time [IMPLEMENTED]
 * - @a --rfc-3339: Output RFC 3339 date/time [IMPLEMENTED]
 * - @a -r, @a --reference: Display the last modification time of FILE
 * [IMPLEMENTED]
 * - @a +FORMAT: Output formatted date string [IMPLEMENTED]
 */
auto constexpr DATE_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-d", "--date", "display time described by STRING, not 'now'",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("-u", "--utc", "print or set Coordinated Universal Time (UTC)"),
    // [DIFFERS]
    OPTION("-R", "--rfc-email",
           "output date and time in RFC 5322 format. Example: Mon, 14 Aug "
           "2006 02:34:56 -0600"),
    // [DIFFERS]
    OPTION("", "--rfc-2822", "output RFC 2822 compliant date string"),
    // [DIFFERS]
    OPTION("-I", "--iso-8601",
           "output date/time in ISO 8601 format. FMT='date' for date only "
           "(the default), 'hours', 'minutes', 'seconds', or 'ns' for date "
           "and time to the indicated precision. Example: "
           "2006-08-14T02:34:56-06:00",
           OPTIONAL_STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--rfc-3339",
           "output date/time in RFC 3339 format. FMT='date', 'seconds', or "
           "'ns' for date and time to the indicated precision. Example: "
           "2006-08-14 02:34:56-06:00",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("-r", "--reference", "display the last modification time of FILE",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("-f", "--file", "like --date; once for each line of DATEFILE",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--universal", "alias for --utc"),
    // [GNU] --uct: deprecated alias for --utc; hidden like GNU
    OPTION("", "--uct", "", BOOL_TYPE),
    // [GNU] --rfc-822: deprecated alias for -R/--rfc-email; hidden like GNU
    OPTION("", "--rfc-822", "", BOOL_TYPE),
    // [GNU] --debug: annotate the parsed date, and warn about dubious usage
    OPTION("", "--debug",
           "annotate the parsed date, and warn about questionable usage to "
           "stderr"),
    // [GNU] --resolution: output the available resolution of timestamps
    OPTION("", "--resolution", "output the available resolution of timestamps"),
    // [GNU] --set: set time described by STRING
    OPTION("", "--set", "set time described by STRING", STRING_TYPE),
    // [DIFFERS] GNU: -s, --set=STRING; WinuxCmd: -s and --set are separate
    // entries
    OPTION("-s", "", "set time described by STRING", STRING_TYPE)};


REGISTER_COMMAND(
    date, "date", "print or set the system date and time",
    "Display date and time in the given FORMAT.\n"
    "With -s, or with [MMDDhhmm[[CC]YY][.ss]], set the date and time.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options "
    "too.\n"
    "\n"
    "All options that specify the date to display are mutually exclusive.\n"
    "I.e.: --date, --file, --reference, --resolution.\n"
    "\n"
    "FORMAT controls the output.  Interpreted sequences are:\n"
    "\n"
    "  %%   a literal %\n"
    "  %a   locale's abbreviated weekday name (e.g., Sun)\n"
    "  %A   locale's full weekday name (e.g., Sunday)\n"
    "  %b   locale's abbreviated month name (e.g., Jan)\n"
    "  %B   locale's full month name (e.g., January)\n"
    "  %c   locale's date and time (e.g., Thu Mar  3 23:05:25 2005)\n"
    "  %C   century; like %Y, except omit last two digits (e.g., 20)\n"
    "  %d   day of month (e.g., 01)\n"
    "  %D   date; same as %m/%d/%y\n"
    "  %e   day of month, space padded; same as %_d\n"
    "  %F   full date; like %Y-%m-%d\n"
    "  %G   year of ISO week number (see %V); normally useful only with %V\n"
    "  %g   year of ISO week number (see %V); normally useful only with %V\n"
    "  %h   same as %b\n"
    "  %H   hour (00..23)\n"
    "  %I   hour (01..12)\n"
    "  %j   day of year (001..366)\n"
    "  %k   hour, space padded ( 0..23); same as %_H\n"
    "  %l   hour, space padded ( 1..12); same as %_I\n"
    "  %m   month (01..12)\n"
    "  %M   minute (00..59)\n"
    "  %n   a newline\n"
    "  %N   nanoseconds (000000000..999999999)\n"
    "  %p   locale's equivalent of either AM or PM; blank if not known\n"
    "  %P   like %p, but lower case\n"
    "  %q   quarter of year (1..4)\n"
    "  %r   locale's 12-hour clock time (e.g., 11:11:04 PM)\n"
    "  %R   24-hour hour and minute; same as %H:%M\n"
    "  %s   seconds since the Epoch (1970-01-01 00:00 UTC)\n"
    "  %S   second (00..60)\n"
    "  %t   a tab\n"
    "  %T   time; same as %H:%M:%S\n"
    "  %U   week number of year, with Sunday as first day of week (00..53)\n"
    "  %V   ISO week number, with Monday as first day of week (01..53)\n"
    "  %W   week number of year, with Monday as first day of week (00..53)\n"
    "  %u   day of week (1..7); 1 is Monday\n"
    "  %w   day of week (0..6); 0 is Sunday\n"
    "  %x   locale's date representation (e.g., 12/31/99)\n"
    "  %X   locale's time representation (e.g., 23:13:48)\n"
    "  %y   last two digits of year (00..99)\n"
    "  %Y   year\n"
    "  %z   +hhmm numeric time zone (e.g., -0400)\n"
    "  %:z  +hh:mm numeric time zone (e.g., -04:00)\n"
    "  %::z  +hh:mm:ss numeric time zone (e.g., -04:00:00)\n"
    "  %:::z  numeric time zone with : to precision (e.g., -04, +05:30)\n"
    "  %Z   alphabetic time zone abbreviation (e.g., EDT)\n"
    "\n"
    "By default, date pads numeric fields with zeroes.\n"
    "The following optional flags may follow '%':\n"
    "\n"
    "  -  (hyphen) do not pad the field\n"
    "  _  (underscore) pad with spaces\n"
    "  0  (zero) pad with zeros\n"
    "  ^  use upper case if possible\n"
    "  #  use opposite case if possible\n"
    "\n"
    "After any flags comes an optional field width, as a decimal number;\n"
    "then an optional modifier, which is either\n"
    "E to use the locale's alternate representations if available, or\n"
    "O to use the locale's alternate numeric symbols if available.\n"
    "\n"
    "Examples:\n"
    "Convert seconds since the Epoch (1970-01-01 UTC) to a date\n"
    "  $ date --date='@2147483647'",
    "  date                    Display current date and time\n"
    "  date +'%Y-%m-%d'        Display date in YYYY-MM-DD format\n"
    "  date +'%H:%M:%S'        Display time in HH:MM:SS format\n"
    "  date -u                 Display UTC time\n"
    "  date -R                 Display RFC email format\n"
    "  date -r FILE            Display FILE modification time\n"
    "  date -Iseconds          Display ISO 8601 format",
    "cal(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd", DATE_OPTIONS) {
  return winux::rust2::run_command_with_stdin_streaming("date", ctx.raw_args);
}
