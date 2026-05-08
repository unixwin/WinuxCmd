/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: date.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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

#pragma comment(lib, "advapi32.lib")
import std;
import core;
import utils;

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
 * - @a +FORMAT: Output formatted date string [IMPLEMENTED]
 */
auto constexpr DATE_OPTIONS = std::array{
    OPTION("-d", "--date", "display time described by STRING, not 'now'",
           STRING_TYPE),
    OPTION("-u", "--utc", "Coordinated Universal Time (UTC)"),
    OPTION("-R", "--rfc-2822", "output RFC 2822 compliant date string")};

namespace date_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Format date/time according to format string
 * @param st System time
 * @param format Format string (supports common format specifiers)
 * @return Formatted date/time string
 */
auto format_time(const SYSTEMTIME &st, const std::string &format)
    -> std::string {
  std::string result;
  result.reserve(format.size() * 2);

  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] == '%' && i + 1 < format.size()) {
      char spec = format[++i];
      char buf[64];

      switch (spec) {
        case 'Y':  // Year (4 digits)
          snprintf(buf, sizeof(buf), "%04d", st.wYear);
          result += buf;
          break;
        case 'm':  // Month (01-12)
          snprintf(buf, sizeof(buf), "%02d", st.wMonth);
          result += buf;
          break;
        case 'd':  // Day (01-31)
          snprintf(buf, sizeof(buf), "%02d", st.wDay);
          result += buf;
          break;
        case 'H':  // Hour (00-23)
          snprintf(buf, sizeof(buf), "%02d", st.wHour);
          result += buf;
          break;
        case 'I':  // Hour (01-12)
          snprintf(buf, sizeof(buf), "%02d",
                   (st.wHour % 12 == 0) ? 12 : st.wHour % 12);
          result += buf;
          break;
        case 'M':  // Minute (00-59)
          snprintf(buf, sizeof(buf), "%02d", st.wMinute);
          result += buf;
          break;
        case 'S':  // Second (00-60)
          snprintf(buf, sizeof(buf), "%02d", st.wSecond);
          result += buf;
          break;
        case 'p':  // AM/PM
          result += (st.wHour < 12) ? "AM" : "PM";
          break;
        case 'a':  // Abbreviated weekday name
        case 'A':  // Full weekday name
        {
          static const char *weekday_names[] = {
              "Sunday",   "Monday", "Tuesday", "Wednesday",
              "Thursday", "Friday", "Saturday"};
          static const char *weekday_abbr[] = {"Sun", "Mon", "Tue", "Wed",
                                               "Thu", "Fri", "Sat"};
          int day_of_week = st.wDayOfWeek;
          result += (spec == 'a') ? weekday_abbr[day_of_week]
                                  : weekday_names[day_of_week];
          break;
        }
        case 'b':  // Abbreviated month name
        case 'B':  // Full month name
        {
          static const char *month_names[] = {
              "January",   "February", "March",    "April",
              "May",       "June",     "July",     "August",
              "September", "October",  "November", "December"};
          static const char *month_abbr[] = {"Jan", "Feb", "Mar", "Apr",
                                             "May", "Jun", "Jul", "Aug",
                                             "Sep", "Oct", "Nov", "Dec"};
          result += (spec == 'b') ? month_abbr[st.wMonth - 1]
                                  : month_names[st.wMonth - 1];
          break;
        }
        case 'y':  // Year (2 digits)
          snprintf(buf, sizeof(buf), "%02d", st.wYear % 100);
          result += buf;
          break;
        default:
          result += '%';
          result += spec;
          break;
      }
    } else {
      result += format[i];
    }
  }

  return result;
}

/**
 * @brief Get current system time
 * @param use_utc If true, use UTC time; otherwise use local time
 * @return SYSTEMTIME structure
 */
auto get_current_time(bool use_utc) -> SYSTEMTIME {
  SYSTEMTIME st;
  if (use_utc) {
    GetSystemTime(&st);
  } else {
    GetLocalTime(&st);
  }
  return st;
}

}  // namespace date_pipeline

REGISTER_COMMAND(
    date, "date", "print or set the system date and time",
    "Display the current time in the given FORMAT, or set the system date.\n"
    "\n"
    "FORMAT controls the output. Interpreted sequences are:\n"
    "  %Y   Year (4 digits)\n"
    "  %y   Year (2 digits)\n"
    "  %m   Month (01-12)\n"
    "  %d   Day (01-31)\n"
    "  %H   Hour (00-23)\n"
    "  %I   Hour (01-12)\n"
    "  %M   Minute (00-59)\n"
    "  %S   Second (00-60)\n"
    "  %p   AM/PM\n"
    "  %a   Abbreviated weekday name\n"
    "  %A   Full weekday name\n"
    "  %b   Abbreviated month name\n"
    "  %B   Full month name",
    "  date                    Display current date and time\n"
    "  date +'%Y-%m-%d'        Display date in YYYY-MM-DD format\n"
    "  date +'%H:%M:%S'        Display time in HH:MM:SS format\n"
    "  date -u                 Display UTC time\n"
    "  date -R                 Display RFC 2822 format",
    "cal(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd", DATE_OPTIONS) {
  using namespace date_pipeline;

  bool use_utc = ctx.get<bool>("-u", false) || ctx.get<bool>("--utc", false);
  bool rfc2822 =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--rfc-2822", false);

  SYSTEMTIME st = get_current_time(use_utc);

  // Default format: YYYY-MM-DD HH:MM:SS
  std::string format = "%Y-%m-%d %H:%M:%S";

  // Check for custom format (+FORMAT)
  for (auto arg : ctx.positionals) {
    std::string_view arg_sv = arg;
    if (arg_sv.starts_with("+")) {
      format = std::string(arg_sv.substr(1));
      break;
    }
  }

  // RFC 2822 format
  if (rfc2822) {
    format = "%a, %d %b %Y %H:%M:%S %z";
  }

  std::string output = format_time(st, format);
  safePrintLn(output);

  return 0;
}
