/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: tzset.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for tzset command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <time.h>

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr TZSET_OPTIONS =
    std::array{OPTION("", "", "timezone settings", STRING_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Get current timezone
std::string get_timezone() {
  TIME_ZONE_INFORMATION tzi;
  DWORD result = GetTimeZoneInformation(&tzi);

  if (result == TIME_ZONE_ID_INVALID) {
    return "UTC";
  }

  // Get timezone name based on DST
  wchar_t* tz_name =
      (result == TIME_ZONE_ID_DAYLIGHT) ? tzi.DaylightName : tzi.StandardName;
  return wstring_to_utf8(tz_name);
}

// Get timezone offset
std::string get_timezone_offset() {
  TIME_ZONE_INFORMATION tzi;
  DWORD result = GetTimeZoneInformation(&tzi);

  if (result == TIME_ZONE_ID_INVALID) {
    return "+0000";
  }

  LONG bias = tzi.Bias;
  if (result == TIME_ZONE_ID_DAYLIGHT) {
    bias += tzi.DaylightBias;
  } else {
    bias += tzi.StandardBias;
  }

  // Convert to UTC offset format
  int hours = abs(bias) / 60;
  int minutes = abs(bias) % 60;
  char sign = (bias <= 0) ? '+' : '-';

  char offset[8];
  sprintf_s(offset, sizeof(offset), "%c%02d%02d", sign, hours, minutes);

  return std::string(offset);
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(tzset,
                 /* cmd_name */ "tzset",
                 /* cmd_synopsis */ "tzset [TIMEZONE]",
                 /* cmd_desc */
                 "Set timezone for the current shell session.\n"
                 "Set or display the timezone for the current shell session.\n"
                 "If no argument is provided, displays the current timezone.",
                 /* examples */
                 "  tzset\n"
                 "  tzset UTC\n"
                 "  tzset Asia/Shanghai",
                 /* see_also */ "date, timedatectl",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ TZSET_OPTIONS) {
  if (ctx.positionals.empty()) {
    // Display current timezone
    std::string tz = get_timezone();
    std::string offset = get_timezone_offset();
    safePrintLn("TZ=" + tz + offset);
    return 0;
  }

  // Set timezone
  std::string new_tz = std::string(ctx.positionals[0]);

  // Set TZ environment variable
  SetEnvironmentVariableA("TZ", new_tz.c_str());

  // Update C runtime timezone
  _tzset();

  return 0;
}
