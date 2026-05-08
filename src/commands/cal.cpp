/*
 *  Copyright © 2026 WinuxCmd
 */

#include <ctime>

#include "core/command_macros.h"
import std;
import core;
import utils;
import container;

auto constexpr CAL_OPTIONS =
    std::array{OPTION("", "", "display calendar", STRING_TYPE)};

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
                 "cal\ncal 3 2024",

                 /* see_also */
                 "date(1)",

                 /* author */
                 "WinuxCmd",

                 /* copyright */
                 "Copyright © 2026 WinuxCmd",

                 /* options */
                 CAL_OPTIONS) {
  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);

  int year = timeinfo->tm_year + 1900;
  int month = timeinfo->tm_mon + 1;

  if (!ctx.positionals.empty()) {
    try {
      if (ctx.positionals.size() == 1) {
        year = std::stoi(std::string(ctx.positionals[0]));
      } else if (ctx.positionals.size() == 2) {
        month = std::stoi(std::string(ctx.positionals[0]));
        year = std::stoi(std::string(ctx.positionals[1]));
      }
    } catch (...) {
    }
  }

  const char* month_names[] = {
      "",     "January", "February",  "March",   "April",    "May",     "June",
      "July", "August",  "September", "October", "November", "December"};

  char buf[128];
  sprintf_s(buf, sizeof(buf), "%s %d", month_names[month], year);
  safePrintLn(buf);
  safePrintLn("Su Mo Tu We Th Fr Sa");

  struct tm first_day = {0};
  first_day.tm_year = year - 1900;
  first_day.tm_mon = month - 1;
  first_day.tm_mday = 1;
  mktime(&first_day);

  int start_day = first_day.tm_wday;
  int days_in_month;

  if (month == 2) {
    bool is_leap = (year % 400 == 0) || (year % 100 != 0 && year % 4 == 0);
    days_in_month = is_leap ? 29 : 28;
  } else if (month == 4 || month == 6 || month == 9 || month == 11) {
    days_in_month = 30;
  } else {
    days_in_month = 31;
  }

  for (int i = 0; i < start_day; ++i) {
    safePrint("   ");
  }

  int day = 1;
  while (day <= days_in_month) {
    sprintf_s(buf, sizeof(buf), "%2d ", day);
    safePrint(buf);

    if ((start_day + day - 1) % 7 == 6) {
      safePrintLn("\n");
    }

    day++;
  }

  safePrintLn("\n");

  return 0;
}
