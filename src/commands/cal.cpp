/*
 *  Copyright © 2026 WinuxCmd
 */

#include <ctime>

#include "core/command_macros.h"
import std;
import core;
import utils;
import container;

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

namespace {
constexpr std::array<std::string_view, 13> kMonthNames = {
    "",     "January", "February", "March",   "April",    "May",     "June",
    "July", "August",  "September", "October", "November", "December"};
constexpr std::array<std::string_view, 7> kDayNamesShort = {
    "Su", "Mo", "Tu", "We", "Th", "Fr", "Sa"};
constexpr std::array<std::string_view, 7> kDayNamesLong = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};

bool parseInt(std::string_view text, int& value) {
  if (text.empty()) return false;
  int parsed = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), parsed);
  if (ec != std::errc() || ptr != text.data() + text.size()) return false;
  value = parsed;
  return true;
}

bool isLeapYear(int year) {
  return (year % 400 == 0) || (year % 100 != 0 && year % 4 == 0);
}

int daysInMonth(int year, int month) {
  static constexpr std::array<int, 13> kDays = {0,  31, 28, 31, 30, 31, 30,
                                                31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  return kDays[static_cast<size_t>(month)];
}

int dayOfYear(int year, int month, int day) {
  int total = day;
  for (int m = 1; m < month; ++m) total += daysInMonth(year, m);
  return total;
}

int weekdaySundayZero(int year, int month, int day) {
  static constexpr std::array<int, 12> kOffsets = {0, 3, 2, 5, 0, 3,
                                                   5, 1, 4, 6, 2, 4};
  if (month < 3) --year;
  return (year + year / 4 - year / 100 + year / 400 +
          kOffsets[static_cast<size_t>(month - 1)] + day) %
         7;
}

std::string blanks(size_t count) { return std::string(count, ' '); }

std::string center(std::string_view text, size_t width) {
  std::string owned(text.substr(0, width));
  if (owned.size() >= width) return owned;
  size_t padding = width - owned.size();
  size_t left = (padding + 1) / 2;
  size_t right = padding - left;
  return blanks(left) + owned + blanks(right);
}

std::string rightAligned(int value, int width) {
  if (value <= 0) return blanks(static_cast<size_t>(width));
  std::string text = std::to_string(value);
  if (text.size() >= static_cast<size_t>(width)) return text;
  return blanks(static_cast<size_t>(width) - text.size()) + text;
}

struct RenderStyle {
  int weekstart = 0;  // 0 = Sunday
  bool julian = false;
  bool name_only_titles = false;
};

int monthWidth(const RenderStyle& style) { return style.julian ? 27 : 20; }

// One month block: title line, weekday header, six week rows
// (cal.c cal_output_months always emits all six week lines).
std::array<std::string, 8> monthLines(int year, int month,
                                      const RenderStyle& style) {
  const int width = monthWidth(style);
  const int first_cell = style.julian ? 3 : 2;
  const int cell = style.julian ? 4 : 3;
  std::array<std::string, 8> lines{};
  if (style.name_only_titles) {
    lines[0] = center(kMonthNames[static_cast<size_t>(month)], width);
  } else {
    lines[0] = center(
        std::format("{} {:04d}", kMonthNames[static_cast<size_t>(month)],
                    year),
        width);
  }
  {
    std::string header;
    for (int i = 0; i < 7; ++i) {
      int dow = (style.weekstart + i) % 7;
      std::string_view name = style.julian
                                  ? kDayNamesLong[static_cast<size_t>(dow)]
                                  : kDayNamesShort[static_cast<size_t>(dow)];
      if (i > 0) header.push_back(' ');
      if (static_cast<int>(name.size()) < first_cell) {
        header += blanks(static_cast<size_t>(first_cell) - name.size());
      }
      header += name;
    }
    lines[1] = header;
  }
  int start_col =
      (weekdaySundayZero(year, month, 1) - style.weekstart + 7) % 7;
  int last = daysInMonth(year, month);
  int day = 1;
  for (int week = 0; week < 6; ++week) {
    std::string row;
    row.reserve(static_cast<size_t>(width));
    for (int col = 0; col < 7; ++col) {
      bool in_month = !(week == 0 && col < start_col) && day <= last;
      int shown = 0;
      if (in_month) {
        shown = style.julian ? dayOfYear(year, month, day) : day;
        ++day;
      }
      row += rightAligned(shown, col == 0 ? first_cell : cell);
    }
    lines[static_cast<size_t>(week + 2)] = row;
  }
  return lines;
}

void printLine(std::string_view line) {
  safePrint(line);
  safePrint("\n");
}

// Print a row of month blocks side by side (cal.c:295 gutter_width=2
// for monthly output, 3 for the year view at cal.c:567).
void printBlockRow(std::vector<std::pair<int, int>> months,
                   const RenderStyle& style, int gutter) {
  std::vector<std::array<std::string, 8>> blocks;
  for (auto [y, m] : months) blocks.push_back(monthLines(y, m, style));
  for (size_t line = 0; line < 8; ++line) {
    for (size_t col = 0; col < blocks.size(); ++col) {
      safePrint(blocks[col][line]);
      if (col + 1 < blocks.size()) safePrint(blanks(static_cast<size_t>(gutter)));
    }
    safePrint("\n");
  }
}

void advanceMonth(int& year, int& month) {
  if (++month > 12) {
    month = 1;
    ++year;
  }
}

int monthFromName(std::string_view text) {
  std::string lower;
  for (char c : text) {
    lower.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  for (int m = 1; m <= 12; ++m) {
    std::string full;
    for (char c : kMonthNames[static_cast<size_t>(m)]) {
      full.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == full) return m;
    if (lower == full.substr(0, 3)) return m;
  }
  return -1;
}

bool isDigitString(std::string_view text) {
  return !text.empty() &&
         std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; });
}
}  // namespace

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
  if (ctx.get<bool>("-V", false)) {
    safePrintLn("cal (WinuxCmd) 1.0");
    return 0;
  }
  if (ctx.get<bool>("-h", false)) {
    safePrintLn("Usage: cal [OPTION] [[MONTH] YEAR]");
    safePrintLn("Display a calendar.");
    safePrintLn("");
    safePrintLn("  -1, --one       display only the requested month");
    safePrintLn("  -3, --three     display previous, current and next month");
    safePrintLn("  -s, --sunday    start week on Sunday");
    safePrintLn("  -m, --monday    start week on Monday");
    safePrintLn("  -j, --julian    display Julian days");
    safePrintLn("  -n, --months=NUM  display NUM months");
    safePrintLn("  -S, --span      span the months around the request");
    safePrintLn("  -y, --year      display the whole year");
    safePrintLn("  -Y, --twelve    display the next twelve months");
    return 0;
  }

  time_t now = time(nullptr);
  struct tm* timeinfo = localtime(&now);
  int cur_year = timeinfo ? timeinfo->tm_year + 1900 : 1970;
  int cur_month = timeinfo ? timeinfo->tm_mon + 1 : 1;

  RenderStyle style;
  style.julian = ctx.get<bool>("-j", false) || ctx.get<bool>("--julian", false);
  // -s/-m: the last one on the command line wins (cal.c switch order).
  for (auto arg : ctx.raw_args) {
    if (arg == "-s" || arg == "--sunday") style.weekstart = 0;
    if (arg == "-m" || arg == "--monday") style.weekstart = 1;
  }

  bool three = ctx.get<bool>("-3", false) || ctx.get<bool>("--three", false);
  bool one = ctx.get<bool>("-1", false) || ctx.get<bool>("--one", false);
  bool year_flag = ctx.get<bool>("-y", false) || ctx.get<bool>("--year", false);
  bool twelve = ctx.get<bool>("-Y", false) || ctx.get<bool>("--twelve", false);
  bool span = ctx.get<bool>("-S", false) || ctx.get<bool>("--span", false);
  int num_months = 0;
  {
    std::string n_text =
        ctx.get<std::string>("-n", ctx.get<std::string>("--months", ""));
    if (!n_text.empty()) {
      int parsed = 0;
      if (!parseInt(n_text, parsed) || parsed < 0) {
        // cal.c:401-403: strtou32_or_err quotes the bad argument.
        safeErrorPrintLn(
            std::format("cal: invalid month argument: '{}'", n_text));
        return 1;
      }
      num_months = parsed;
    }
  }
  if (three) {
    num_months = 3;
    span = true;
  }
  if (twelve) num_months = 12;

  // Operand parsing (cal.c:467-530).
  int year = cur_year;
  int month = cur_month;
  bool year_only = false;
  const auto& pos = ctx.positionals;
  if (pos.size() > 3) {
    safeErrorPrintLn("cal: bad usage");
    safeErrorPrintLn(winux::i18n::format(
        "common.try_help", "Try '{} --help' for more information.", "cal"));
    return 1;
  }
  size_t idx = 0;
  int day = 0;
  if (pos.size() == 1 && !isDigitString(pos[0])) {
    int named = monthFromName(pos[0]);
    if (named < 0) {
      safeErrorPrintLn(
          "cal: failed to parse timestamp or unknown month name: " +
          std::string(pos[0]));
      return 1;
    }
    month = named;
    year = cur_year;
    idx = 1;  // consumed; not a year
  } else if (!pos.empty()) {
    if (pos.size() == 3) {
      // cal.c:481-483: a non-numeric day is a parse error naming the
      // argument; a numeric one out of range gets the 1-31 message.
      if (!parseInt(pos[0], day)) {
        safeErrorPrintLn(
            std::format("cal: illegal day value: '{}'", pos[0]));
        return 1;
      }
      if (day < 1 || day > 31) {
        safeErrorPrintLn("cal: illegal day value: use 1-31");
        return 1;
      }
      idx = 1;
    }
    if (pos.size() >= 2) {
      std::string_view month_text = pos[idx];
      // cal.c:486-495: the numeric path is chosen by the FIRST digit,
      // so "3x" fails as an illegal month value, not an unknown name.
      if (!month_text.empty() && month_text.front() >= '0' &&
          month_text.front() <= '9') {
        if (!parseInt(month_text, month)) month = 0;
      } else {
        month = monthFromName(month_text);
        if (month < 0) {
          safeErrorPrintLn("cal: unknown month name: " +
                           std::string(month_text));
          return 1;
        }
      }
      if (month < 1 || month > 12) {
        safeErrorPrintLn("cal: illegal month value: use 1-12");
        return 1;
      }
      ++idx;
    }
    // Final operand is the year (cal.c:498: strtos32_or_err quotes a
    // non-numeric year in the message).
    if (!parseInt(pos[idx], year)) {
      safeErrorPrintLn(
          std::format("cal: illegal year value: '{}'", pos[idx]));
      return 1;
    }
    if (year < 1) {
      safeErrorPrintLn("cal: illegal year value: use positive integer");
      return 1;
    }
    if (pos.size() == 1) year_only = true;
    if (day > 0 && day > daysInMonth(year, month)) {
      safeErrorPrintLn(std::format("cal: illegal day value: use 1-{}",
                                   daysInMonth(year, month)));
      return 1;
    }
  }

  // Year view: -y, or a lone year operand with no month count
  // (cal.c: yflag when !req.month && !num_months).
  if (year_flag || (year_only && num_months == 0 && !one)) {
    RenderStyle year_style = style;
    year_style.name_only_titles = year_flag || year_only;
    int width = 3 * monthWidth(style) + 2 * 3;
    printLine(center(std::format("{:04d}", year), static_cast<size_t>(width)));
    safePrint("\n");
    for (int first = 1; first <= 12; first += 3) {
      printBlockRow({{year, first}, {year, first + 1}, {year, first + 2}},
                    year_style, 3);
    }
    return 0;
  }

  if (num_months == 0) num_months = 1;
  int start_year = year;
  int start_month = month;
  if (span) {
    // cal.c monthly(): the span starts num_months/2 before the
    // requested month (-3 therefore shows the previous month first).
    int back = num_months / 2;
    for (int i = 0; i < back; ++i) {
      if (--start_month < 1) {
        start_month = 12;
        --start_year;
      }
    }
  }
  // A lone year operand with -n/-3/-1 anchors at the CURRENT month
  // of that year (cal.c: req_month defaults to the local month when
  // only a year is given); `month` already holds it above.

  std::vector<std::pair<int, int>> sequence;
  {
    int y = start_year;
    int m = start_month;
    for (int i = 0; i < num_months; ++i) {
      sequence.emplace_back(y, m);
      advanceMonth(y, m);
    }
  }
  // -Y goes through the yearly layout family (gutter 3, cal.c:567)
  // but keeps full "Month Year" titles and no year banner.
  const int gutter = twelve ? 3 : 2;
  for (size_t base = 0; base < sequence.size(); base += 3) {
    std::vector<std::pair<int, int>> row;
    for (size_t k = base; k < sequence.size() && k < base + 3; ++k) {
      row.push_back(sequence[k]);
    }
    printBlockRow(row, style, gutter);
  }
  return 0;
}
