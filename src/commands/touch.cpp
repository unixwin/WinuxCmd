// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief TOUCH command options definition
 *
 * This array defines all the options supported by the touch command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -a: Change only the access time [IMPLEMENTED]
 * - @a -c, @a --no-create: Do not create any files [IMPLEMENTED]
 * - @a -d, @a --date: Parse STRING and use it instead of current time
 * [IMPLEMENTED]
 * - @a -f: (ignored) [IMPLEMENTED]
 * - @a -h, @a --no-dereference: Affect symbolic link instead of referenced file
 * [IMPLEMENTED]
 * - @a -m: Change only the modification time [IMPLEMENTED]
 * - @a -r, @a --reference: Use this file's times instead of current time
 * [IMPLEMENTED]
 * - @a -t: Use [[CC]YY]MMDDhhmm[.ss] instead of current time [IMPLEMENTED]
 * - @a --time: Change the specified time (access/atime/use/modify/mtime)
 * [IMPLEMENTED]
 */
auto constexpr TOUCH_OPTIONS = std::array{
    // [DIFFERS] -a
    OPTION("-a", "", "change only the access time"),
    // [DIFFERS] -c, --no-create
    OPTION("-c", "--no-create", "do not create any files"),
    // [DIFFERS] -d, --date
    OPTION("-d", "--date", "parse STRING and use it instead of current time",
           STRING_TYPE),
    // [DIFFERS] -f
    OPTION("-f", "", "(ignored)"),
    // [DIFFERS] -h, --no-dereference
    OPTION("-h", "--no-dereference",
           "affect symbolic link instead of referenced file"),
    // [DIFFERS] -m
    OPTION("-m", "", "change only the modification time"),
    // [DIFFERS] -r, --reference
    OPTION("-r", "--reference", "use this file's times instead of current time",
           STRING_TYPE),
    // [DIFFERS] -t
    OPTION("-t", "", "use [[CC]YY]MMDDhhmm[.ss] instead of current time",
           STRING_TYPE),
    // [DIFFERS] --time
    OPTION("", "--time",
           "change the specified time (access/atime/use/modify/mtime)",
           STRING_TYPE)};

namespace touch_pipeline {
namespace cp = core::pipeline;

struct TimePair {
  FILETIME atime{};
  FILETIME mtime{};
};

auto trim_copy(std::string s) -> std::string {
  auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

auto lower_copy(std::string s) -> std::string {
  std::ranges::transform(s, s.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return s;
}

auto is_digits(std::string_view s) -> bool {
  return !s.empty() && std::ranges::all_of(s, [](unsigned char ch) {
    return std::isdigit(ch) != 0;
  });
}

auto parse_int(std::string_view s) -> std::optional<int> {
  if (s.starts_with('+')) s.remove_prefix(1);
  int value = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc() || ptr != s.data() + s.size()) return std::nullopt;
  return value;
}

auto days_in_month(int year, int month) -> int {
  static constexpr int days[] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};
  if (month != 2) return days[month - 1];
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return leap ? 29 : 28;
}

auto valid_system_time(const SYSTEMTIME& st) -> bool {
  if (st.wYear < 1601 || st.wMonth < 1 || st.wMonth > 12) return false;
  if (st.wDay < 1 || st.wDay > days_in_month(st.wYear, st.wMonth)) {
    return false;
  }
  return st.wHour <= 23 && st.wMinute <= 59 && st.wSecond <= 59;
}

auto filetime_to_ticks(const FILETIME& ft) -> unsigned long long {
  ULARGE_INTEGER uli{};
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  return uli.QuadPart;
}

auto ticks_to_filetime(unsigned long long ticks) -> FILETIME {
  ULARGE_INTEGER uli{};
  uli.QuadPart = ticks;
  return FILETIME{uli.LowPart, uli.HighPart};
}

auto add_seconds(FILETIME ft, long long seconds) -> FILETIME {
  constexpr long long ticks_per_second = 10000000LL;
  auto ticks = static_cast<long long>(filetime_to_ticks(ft));
  ticks += seconds * ticks_per_second;
  return ticks_to_filetime(static_cast<unsigned long long>(ticks));
}

auto local_system_time_to_filetime(const SYSTEMTIME& local)
    -> std::optional<FILETIME> {
  if (!valid_system_time(local)) return std::nullopt;

  TIME_ZONE_INFORMATION tzi{};
  GetTimeZoneInformation(&tzi);

  SYSTEMTIME utc{};
  if (!TzSpecificLocalTimeToSystemTime(&tzi, &local, &utc)) {
    return std::nullopt;
  }

  FILETIME ft{};
  if (!SystemTimeToFileTime(&utc, &ft)) return std::nullopt;
  return ft;
}

auto utc_system_time_to_filetime(const SYSTEMTIME& utc)
    -> std::optional<FILETIME> {
  if (!valid_system_time(utc)) return std::nullopt;
  FILETIME ft{};
  if (!SystemTimeToFileTime(&utc, &ft)) return std::nullopt;
  return ft;
}

auto filetime_to_local_system_time(const FILETIME& ft)
    -> std::optional<SYSTEMTIME> {
  FILETIME local_ft{};
  SYSTEMTIME st{};
  if (!FileTimeToLocalFileTime(&ft, &local_ft)) return std::nullopt;
  if (!FileTimeToSystemTime(&local_ft, &st)) return std::nullopt;
  return st;
}

auto parse_epoch_time(std::string_view s) -> std::optional<FILETIME> {
  if (s.empty() || s.front() != '@') return std::nullopt;
  auto rest = s.substr(1);

  // [GNU] "@<seconds>" accepts a fractional part ("@123.5" — the fraction
  // is kept to 100ns FILETIME ticks) and a sign that applies to the whole
  // value ("@-1.5" is 1.5 seconds before the epoch). Issue #1382.
  bool negative = false;
  if (!rest.empty() && (rest.front() == '-' || rest.front() == '+')) {
    negative = rest.front() == '-';
    rest = rest.substr(1);
  }
  auto dot = rest.find('.');
  auto int_part = dot == std::string_view::npos ? rest : rest.substr(0, dot);
  auto frac_part =
      dot == std::string_view::npos ? std::string_view{} : rest.substr(dot + 1);
  if (!is_digits(int_part)) return std::nullopt;
  if (dot != std::string_view::npos && !is_digits(frac_part)) {
    return std::nullopt;
  }
  long long seconds = 0;
  auto [ptr, ec] = std::from_chars(int_part.data(),
                                   int_part.data() + int_part.size(), seconds);
  if (ec != std::errc() || ptr != int_part.data() + int_part.size()) {
    return std::nullopt;
  }
  long long frac_ticks = 0;
  for (size_t i = 0; i < 7; ++i) {
    frac_ticks = frac_ticks * 10 + (i < frac_part.size() ? frac_part[i] - '0' : 0);
  }

  SYSTEMTIME epoch{};
  epoch.wYear = 1970;
  epoch.wMonth = 1;
  epoch.wDay = 1;
  auto ft = utc_system_time_to_filetime(epoch);
  if (!ft) return std::nullopt;
  const long long delta =
      seconds * 10000000LL + frac_ticks;
  const auto total = static_cast<long long>(filetime_to_ticks(*ft)) +
                     (negative ? -delta : delta);
  if (total < 0) return std::nullopt;
  return ticks_to_filetime(static_cast<unsigned long long>(total));
}

auto parse_timezone_suffix(std::string& s) -> std::optional<int> {
  std::string lowered = lower_copy(s);
  if (lowered.ends_with(" utc") || lowered.ends_with(" gmt")) {
    s = trim_copy(s.substr(0, s.size() - 4));
    return 0;
  }
  if (lowered.ends_with("z") && s.size() > 1 &&
      std::isdigit(static_cast<unsigned char>(s[s.size() - 2]))) {
    s = trim_copy(s.substr(0, s.size() - 1));
    return 0;
  }
  if (s.size() < 5) return std::nullopt;

  size_t pos = s.size() - 5;
  if (s[pos] == '+' || s[pos] == '-') {
    std::string_view hh{s.data() + pos + 1, 2};
    std::string_view mm{s.data() + pos + 3, 2};
    if (is_digits(hh) && is_digits(mm)) {
      int hours = *parse_int(hh);
      int minutes = *parse_int(mm);
      if (hours <= 23 && minutes <= 59) {
        int sign = s[pos] == '-' ? -1 : 1;
        s = trim_copy(s.substr(0, pos));
        return sign * (hours * 60 + minutes);
      }
    }
  }

  if (s.size() >= 6) {
    pos = s.size() - 6;
    if ((s[pos] == '+' || s[pos] == '-') && s[pos + 3] == ':') {
      std::string_view hh{s.data() + pos + 1, 2};
      std::string_view mm{s.data() + pos + 4, 2};
      if (is_digits(hh) && is_digits(mm)) {
        int hours = *parse_int(hh);
        int minutes = *parse_int(mm);
        if (hours <= 23 && minutes <= 59) {
          int sign = s[pos] == '-' ? -1 : 1;
          s = trim_copy(s.substr(0, pos));
          return sign * (hours * 60 + minutes);
        }
      }
    }
  }

  return std::nullopt;
}

// [GNU] touch honors the TZ environment variable when interpreting wall
// clock times given via -t or -d (no explicit offset in the string).
// Supported POSIX forms: "UTC", "GMT" and "NAME[+|-]hh[:mm[:ss]]" where the
// offset counts WEST of the prime meridian (POSIX sign convention).
// Named IANA zones (e.g. "Asia/Shanghai") are not resolvable without a
// tz database, so they fall back to the system local zone. Returns the
// offset EAST of UTC in minutes, or nullopt to use the system local zone.
auto env_tz_offset_east() -> std::optional<int> {
  const char* tz = std::getenv("TZ");
  if (tz == nullptr || *tz == '\0') return std::nullopt;
  std::string s = tz;
  if (!s.empty() && s.front() == ':') s = s.substr(1);

  size_t pos = 0;
  if (!s.empty() && s.front() == '<') {
    auto close = s.find('>');
    if (close == std::string::npos) return std::nullopt;
    pos = close + 1;
  } else {
    while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos]))) {
      ++pos;
    }
  }
  if (pos == s.size()) {
    // Bare zone name: only UTC/GMT/UT/Z are unambiguous; anything else
    // (IANA names) falls back to the system local zone.
    std::string name = s;
    for (auto& c : name) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (name == "UTC" || name == "GMT" || name == "UT" || name == "Z") {
      return 0;
    }
    return std::nullopt;
  }

  int sign = 1;
  if (s[pos] == '+' || s[pos] == '-') {
    sign = s[pos] == '-' ? -1 : 1;
    ++pos;
  }

  auto read_num = [&](int& out, size_t& i) -> bool {
    size_t start = i;
    while (i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]))) {
      ++i;
    }
    if (i == start) return false;
    out = *parse_int(std::string_view(s).substr(start, i - start));
    return true;
  };

  int hours = 0;
  int minutes = 0;
  int seconds = 0;
  if (!read_num(hours, pos)) return std::nullopt;
  if (pos < s.size() && s[pos] == ':') {
    ++pos;
    if (!read_num(minutes, pos)) return std::nullopt;
    if (pos < s.size() && s[pos] == ':') {
      ++pos;
      if (!read_num(seconds, pos)) return std::nullopt;
    }
  }
  if (pos != s.size() || hours > 24 || minutes > 59 || seconds > 59) {
    return std::nullopt;
  }

  // POSIX: positive offset = west of UTC, so east offset negates it.
  long long east_seconds = -sign * (hours * 3600LL + minutes * 60LL + seconds);
  return static_cast<int>((east_seconds + 30) / 60);
}

// Interpret a wall clock SYSTEMTIME in the zone selected by an explicit
// UTC offset (from a "+hh:mm" suffix in the string), else the TZ
// environment variable, else the system local zone.
auto zone_time_to_filetime(const SYSTEMTIME& st,
                           std::optional<int> explicit_east_offset)
    -> std::optional<FILETIME> {
  std::optional<int> east = explicit_east_offset;
  if (!east) east = env_tz_offset_east();
  if (east) {
    auto ft = utc_system_time_to_filetime(st);
    if (!ft) return std::nullopt;
    return add_seconds(*ft, -static_cast<long long>(*east) * 60);
  }
  return local_system_time_to_filetime(st);
}

// Inverse of zone_time_to_filetime: break a FILETIME down into wall-clock
// fields in the effective zone (TZ env fixed offset, else system local).
auto filetime_to_zone_system_time(const FILETIME& ft)
    -> std::optional<SYSTEMTIME> {
  if (auto east = env_tz_offset_east()) {
    FILETIME shifted = add_seconds(ft, static_cast<long long>(*east) * 60);
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&shifted, &st)) return std::nullopt;
    return st;
  }
  return filetime_to_local_system_time(ft);
}

// [GNU] Calendar month arithmetic like parse-datetime.y's rel.month:
// shifting Mar 31 back one month yields Mar 3 (mktime day overflow rolls
// forward), not Feb 28. Verified: touch -d '2026-03-31 1 month ago' ->
// 2026-03-03 (uutils#10185 / WinuxCmd#281).
auto shift_months(const FILETIME& base, long long months)
    -> std::optional<FILETIME> {
  auto st = filetime_to_zone_system_time(base);
  if (!st) return std::nullopt;
  long long total =
      static_cast<long long>(st->wYear) * 12 + (st->wMonth - 1) + months;
  long long year = total / 12;
  long long month = total % 12;
  if (month < 0) {
    month += 12;
    --year;
  }
  ++month;
  if (year < 1601 || year > 9999) return std::nullopt;
  long long day = st->wDay;
  while (day > days_in_month(static_cast<int>(year), static_cast<int>(month))) {
    day -= days_in_month(static_cast<int>(year), static_cast<int>(month));
    if (++month > 12) {
      month = 1;
      ++year;
      if (year > 9999) return std::nullopt;
    }
  }
  st->wYear = static_cast<WORD>(year);
  st->wMonth = static_cast<WORD>(month);
  st->wDay = static_cast<WORD>(day);
  return zone_time_to_filetime(*st, std::nullopt);
}

auto parse_fixed_date_time(std::string input) -> std::optional<FILETIME> {
  input = trim_copy(input);
  if (auto epoch = parse_epoch_time(input)) return epoch;

  auto tz_offset = parse_timezone_suffix(input);
  std::ranges::replace(input, 'T', ' ');

  std::string date_part = input;
  std::string time_part;
  if (auto space = input.find(' '); space != std::string::npos) {
    date_part = input.substr(0, space);
    time_part = trim_copy(input.substr(space + 1));
  }

  int year = 0;
  int month = 0;
  int day = 0;
  int hour = 0;
  int minute = 0;
  int second = 0;
  if (date_part.size() == 10 && (date_part[4] == '-' || date_part[4] == '/') &&
      date_part[7] == date_part[4]) {
    auto y = parse_int(std::string_view(date_part).substr(0, 4));
    auto m = parse_int(std::string_view(date_part).substr(5, 2));
    auto d = parse_int(std::string_view(date_part).substr(8, 2));
    if (!y || !m || !d) return std::nullopt;
    year = *y;
    month = *m;
    day = *d;
  } else if (date_part.size() == 8 && is_digits(date_part)) {
    year = *parse_int(std::string_view(date_part).substr(0, 4));
    month = *parse_int(std::string_view(date_part).substr(4, 2));
    day = *parse_int(std::string_view(date_part).substr(6, 2));
  } else if ((date_part.size() == 12 || date_part.size() == 14) &&
             is_digits(date_part)) {
    year = *parse_int(std::string_view(date_part).substr(0, 4));
    month = *parse_int(std::string_view(date_part).substr(4, 2));
    day = *parse_int(std::string_view(date_part).substr(6, 2));
    hour = *parse_int(std::string_view(date_part).substr(8, 2));
    minute = *parse_int(std::string_view(date_part).substr(10, 2));
    if (date_part.size() == 14) {
      second = *parse_int(std::string_view(date_part).substr(12, 2));
    }
  } else {
    // [GNU] Loose ISO dates with single-digit fields are accepted
    // ("2026-6-27"), so also try splitting on '-' with variable widths
    // (uutils #13134).
    if (date_part.size() >= 6 && date_part.find('-') != std::string::npos) {
      auto first = date_part.find('-');
      auto second = date_part.find('-', first + 1);
      if (second != std::string::npos &&
          date_part.find('-', second + 1) == std::string::npos) {
        auto y = parse_int(std::string_view(date_part).substr(0, first));
        auto m = parse_int(
            std::string_view(date_part).substr(first + 1, second - first - 1));
        auto d = parse_int(std::string_view(date_part).substr(second + 1));
        if (y && m && d) {
          year = *y;
          month = *m;
          day = *d;
        } else {
          return std::nullopt;
        }
      } else {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
  }

  if (!time_part.empty()) {
    std::vector<std::string_view> pieces;
    std::string_view tv = time_part;
    while (true) {
      auto colon = tv.find(':');
      pieces.push_back(tv.substr(0, colon));
      if (colon == std::string_view::npos) break;
      tv.remove_prefix(colon + 1);
    }
    if (pieces.size() < 2 || pieces.size() > 3) return std::nullopt;
    // [GNU] the seconds field may carry a fractional part
    // ("03:04:05.123456789", stat-style timestamps; WinuxCmd#976).
    // FILETIME holds 100ns ticks, so extra digits are truncated.
    long long frac_ticks = 0;
    if (pieces.size() == 3) {
      if (auto dot = pieces[2].find('.'); dot != std::string_view::npos) {
        auto frac = pieces[2].substr(dot + 1);
        if (frac.empty() || !is_digits(frac)) return std::nullopt;
        long long nsec = 0;
        for (size_t i = 0; i < 9; ++i) {
          nsec = nsec * 10 + (i < frac.size() ? frac[i] - '0' : 0);
        }
        frac_ticks = nsec / 100;
        pieces[2] = pieces[2].substr(0, dot);
      }
    }
    auto h = parse_int(pieces[0]);
    auto m = parse_int(pieces[1]);
    auto sec =
        pieces.size() == 3 ? parse_int(pieces[2]) : std::optional<int>{0};
    if (!h || !m || !sec) return std::nullopt;
    hour = *h;
    minute = *m;
    second = *sec;

    SYSTEMTIME st{};
    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    st.wHour = static_cast<WORD>(hour);
    st.wMinute = static_cast<WORD>(minute);
    st.wSecond = static_cast<WORD>(second);

    std::optional<FILETIME> ft;
    if (tz_offset) {
      auto utc_ft = utc_system_time_to_filetime(st);
      if (!utc_ft) return std::nullopt;
      ft = add_seconds(*utc_ft, -static_cast<long long>(*tz_offset) * 60);
    } else {
      ft = zone_time_to_filetime(st, std::nullopt);
    }
    if (!ft) return std::nullopt;
    if (frac_ticks != 0) {
      ft = ticks_to_filetime(filetime_to_ticks(*ft) +
                             static_cast<unsigned long long>(frac_ticks));
    }
    return ft;
  }

  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(year);
  st.wMonth = static_cast<WORD>(month);
  st.wDay = static_cast<WORD>(day);
  st.wHour = static_cast<WORD>(hour);
  st.wMinute = static_cast<WORD>(minute);
  st.wSecond = static_cast<WORD>(second);

  if (tz_offset) {
    auto ft = utc_system_time_to_filetime(st);
    if (!ft) return std::nullopt;
    return add_seconds(*ft, -static_cast<long long>(*tz_offset) * 60);
  }
  return zone_time_to_filetime(st, std::nullopt);
}

auto parse_relative_date(std::string input, const FILETIME& base)
    -> std::optional<FILETIME> {
  input = lower_copy(trim_copy(input));
  if (input == "now") return base;
  if (input == "yesterday") return add_seconds(base, -24LL * 60 * 60);
  if (input == "tomorrow") return add_seconds(base, 24LL * 60 * 60);
  if (input == "today") {
    auto st = filetime_to_local_system_time(base);
    if (!st) return std::nullopt;
    st->wHour = 0;
    st->wMinute = 0;
    st->wSecond = 0;
    st->wMilliseconds = 0;
    return local_system_time_to_filetime(*st);
  }

  bool ago = false;
  if (input.ends_with(" ago")) {
    ago = true;
    input = trim_copy(input.substr(0, input.size() - 4));
  }

  std::istringstream iss(input);
  std::string amount_word;
  std::string unit;
  if (!(iss >> amount_word >> unit)) return std::nullopt;
  std::string trailing;
  if (iss >> trailing) return std::nullopt;

  int amount = 0;
  if (amount_word == "next") {
    amount = 1;
  } else if (amount_word == "last") {
    amount = -1;
  } else {
    auto parsed = parse_int(amount_word);
    if (!parsed) return std::nullopt;
    amount = *parsed;
  }
  if (ago) amount = -amount;

  // [GNU] month/year are calendar units, not fixed second counts:
  // "3 months ago" rolls the wall-clock month back three positions.
  if (unit == "month" || unit == "months") {
    return shift_months(base, amount);
  }
  if (unit == "year" || unit == "years") {
    return shift_months(base, static_cast<long long>(amount) * 12);
  }

  long long scale = 0;
  if (unit == "second" || unit == "seconds" || unit == "sec" ||
      unit == "secs") {
    scale = 1;
  } else if (unit == "minute" || unit == "minutes" || unit == "min" ||
             unit == "mins") {
    scale = 60;
  } else if (unit == "hour" || unit == "hours") {
    scale = 60 * 60;
  } else if (unit == "day" || unit == "days") {
    scale = 24 * 60 * 60;
  } else if (unit == "week" || unit == "weeks") {
    scale = 7 * 24 * 60 * 60;
  } else if (unit == "fortnight" || unit == "fortnights") {
    // [GNU] a fortnight is fourteen days (issue #1382).
    scale = 14 * 24 * 60 * 60;
  } else {
    return std::nullopt;
  }

  return add_seconds(base, static_cast<long long>(amount) * scale);
}

// [GNU] Weekday items (issue #1382): "Monday", "next Monday",
// "last Monday". A bare name is the nearest such day, today counting
// (+0..6); "next" pushes a same-day match a full week out (+7), "last"
// mirrors it backwards (-7..0 with same-day at -7). The result is
// 00:00:00 of the target date in the effective zone.
auto weekday_number(const std::string& name) -> std::optional<int> {
  static const std::unordered_map<std::string, int> days = {
      {"sunday", 0}, {"sun", 0},    {"monday", 1},   {"mon", 1},
      {"tuesday", 2}, {"tue", 2},   {"tues", 2},     {"wednesday", 3},
      {"wed", 3},    {"thursday", 4}, {"thu", 4},    {"thur", 4},
      {"thurs", 4},  {"friday", 5}, {"fri", 5},      {"saturday", 6},
      {"sat", 6}};
  auto it = days.find(name);
  if (it == days.end()) return std::nullopt;
  return it->second;
}

auto parse_weekday_date(std::string input, const FILETIME& base)
    -> std::optional<FILETIME> {
  input = lower_copy(trim_copy(input));
  enum class Mode { bare, next, last };
  Mode mode = Mode::bare;
  if (input.starts_with("next ")) {
    mode = Mode::next;
    input = trim_copy(input.substr(5));
  } else if (input.starts_with("last ")) {
    mode = Mode::last;
    input = trim_copy(input.substr(5));
  }
  auto target = weekday_number(input);
  if (!target) return std::nullopt;

  auto st = filetime_to_zone_system_time(base);
  if (!st) return std::nullopt;
  const int delta = (*target - static_cast<int>(st->wDayOfWeek) + 7) % 7;
  long long offset = delta;
  if (mode == Mode::next && delta == 0) offset = 7;
  if (mode == Mode::last) offset = delta == 0 ? -7 : delta - 7;

  auto shifted = add_seconds(base, offset * 24LL * 60 * 60);
  auto day = filetime_to_zone_system_time(shifted);
  if (!day) return std::nullopt;
  day->wHour = 0;
  day->wMinute = 0;
  day->wSecond = 0;
  day->wMilliseconds = 0;
  return zone_time_to_filetime(*day, std::nullopt);
}

// [GNU] A date string may end with one relative item applied to a base
// timestamp ("2026-03-31 1 month ago" -> 2026-03-03). Split off a trailing
// "<amount> <unit>[ ago|hence]" and parse the remainder as the base.
auto parse_combined_date(std::string input, const FILETIME& base, int depth)
    -> std::optional<FILETIME> {
  if (depth > 4) return std::nullopt;  // bound the recursion
  static const std::regex tail_re(
      R"(^(.*?)\s+([+-]?[0-9]+|next|last)\s+(fortnights?|seconds?|secs?|minutes?|mins?|hours?|days?|weeks?|months?|years?)\s*(ago|hence)?\s*$)",
      std::regex::icase);
  std::smatch m;
  if (!std::regex_match(input, m, tail_re)) {
    // [GNU] A trailing weekday item applies to the fixed head the same
    // way ("2020-01-02 Monday"; issue #1382).
    static const std::regex weekday_tail_re(
        R"(^(.*?)\s+((?:next|last)\s+)?(sunday|monday|tuesday|wednesday|thursday|friday|saturday|sun|mon|tue|tues|wed|thu|thur|thurs|fri|sat)\s*$)",
        std::regex::icase);
    std::smatch wm;
    if (!std::regex_match(input, wm, weekday_tail_re)) return std::nullopt;
    std::string head = trim_copy(wm[1].str());
    if (head.empty()) return std::nullopt;
    std::optional<FILETIME> head_ft = parse_fixed_date_time(head);
    if (!head_ft) head_ft = parse_combined_date(head, base, depth + 1);
    if (!head_ft) return std::nullopt;
    return parse_weekday_date(trim_copy(wm[2].str() + wm[3].str()), *head_ft);
  }
  std::string base_str = trim_copy(m[1].str());
  if (base_str.empty()) return std::nullopt;

  std::optional<FILETIME> base_ft = parse_fixed_date_time(base_str);
  if (!base_ft) {
    base_ft = parse_combined_date(base_str, base, depth + 1);
    if (!base_ft) base_ft = parse_relative_date(base_str, base);
    if (!base_ft) return std::nullopt;
  }

  std::string rel_spec = trim_copy(m[2].str() + " " + m[3].str() + " " +
                                   (m[4].matched ? m[4].str() : ""));
  return parse_relative_date(rel_spec, *base_ft);
}

// [GNU] military timezone specs (parse-datetime.y military_table):
// "<HH|HHMM><L>" or a lone "<L>" meaning today 00:00 in that zone.
// A-I = UTC+1..+9, K-M = UTC+10..+12 (J is skipped in the alphabet),
// N-Y = UTC-1..-12, Z = UTC, and J = the local zone. 'T' is not a zone.
auto parse_military_time(const std::string& input) -> std::optional<FILETIME> {
  // [GNU] also accepts "<HH>:<MM>[:<SS>]<L>" (e.g. "9:30j").
  static const std::regex military_hms_re(
      R"(^([0-9]{1,2}):([0-9]{2})(?::([0-9]{2}))?\s*([A-Za-z])$)");
  static const std::regex military_re(R"(^([0-9]{1,4})\s*([A-Za-z])$)");
  static const std::regex zone_re(R"(^([A-Za-z])$)");
  std::smatch m;
  int hour = 0;
  int minute = 0;
  int second = 0;
  char letter = '\0';
  if (std::regex_match(input, m, military_hms_re)) {
    hour = std::stoi(m[1].str());
    minute = std::stoi(m[2].str());
    if (m[3].matched) second = std::stoi(m[3].str());
    letter = static_cast<char>(
        std::tolower(static_cast<unsigned char>(m[4].str()[0])));
  } else if (std::regex_match(input, m, military_re)) {
    const std::string digits = m[1].str();
    if (digits.size() <= 2) {
      hour = std::stoi(digits);
    } else {
      hour = std::stoi(digits.substr(0, digits.size() - 2));
      minute = std::stoi(digits.substr(digits.size() - 2));
    }
    letter = static_cast<char>(
        std::tolower(static_cast<unsigned char>(m[2].str()[0])));
  } else if (std::regex_match(input, m, zone_re)) {
    letter = static_cast<char>(
        std::tolower(static_cast<unsigned char>(m[1].str()[0])));
  } else {
    return std::nullopt;
  }
  if (letter == 't' || hour > 23 || minute > 59 || second > 59) {
    return std::nullopt;
  }

  std::optional<int> east;
  if (letter == 'j') {
    east = std::nullopt;  // 'J' = the local zone
  } else if (letter >= 'a' && letter <= 'i') {
    east = (letter - 'a' + 1) * 60;
  } else if (letter >= 'k' && letter <= 'm') {
    east = (letter - 'a') * 60;
  } else if (letter == 'z') {
    east = 0;
  } else if (letter >= 'n' && letter <= 'y') {
    east = -(letter - 'n' + 1) * 60;
  } else {
    return std::nullopt;
  }

  SYSTEMTIME now{};
  GetLocalTime(&now);
  SYSTEMTIME st{};
  st.wYear = now.wYear;
  st.wMonth = now.wMonth;
  st.wDay = now.wDay;
  st.wHour = static_cast<WORD>(hour);
  st.wMinute = static_cast<WORD>(minute);
  st.wSecond = static_cast<WORD>(second);
  st.wMilliseconds = 0;
  return zone_time_to_filetime(st, east);
}

auto parse_gnu_date_string(const std::string& input, const FILETIME& base)
    -> std::optional<FILETIME> {
  // [GNU] An empty (or all-blank) -d string means today at 00:00:00 in the
  // effective zone (getdate: an empty string is the start of today).
  // Issue #1382.
  if (trim_copy(input).empty()) {
    auto st = filetime_to_zone_system_time(base);
    if (!st) return std::nullopt;
    st->wHour = 0;
    st->wMinute = 0;
    st->wSecond = 0;
    st->wMilliseconds = 0;
    return zone_time_to_filetime(*st, std::nullopt);
  }
  if (auto fixed = parse_fixed_date_time(input)) return fixed;
  if (auto mil = parse_military_time(input)) return mil;
  if (auto weekday = parse_weekday_date(input, base)) return weekday;
  if (auto rel = parse_relative_date(input, base)) return rel;
  return parse_combined_date(input, base, 0);
}

auto parse_touch_timestamp(const std::string& input)
    -> std::optional<FILETIME> {
  std::string s = trim_copy(input);
  auto dot = s.find('.');
  std::string main = dot == std::string::npos ? s : s.substr(0, dot);
  std::string seconds_part = dot == std::string::npos ? "" : s.substr(dot + 1);

  if (!(main.size() == 8 || main.size() == 10 || main.size() == 12)) {
    return std::nullopt;
  }
  if (!is_digits(main)) return std::nullopt;
  // [GNU] A present '.' REQUIRES the total suffix to be exactly
  // ".ss" (posixtm.c:100-107): a trailing dot with no digits is
  // invalid. The old guard skipped the check when the fraction was
  // empty, accepting '01010000.'.
  if (dot != std::string::npos &&
      (seconds_part.size() != 2 || !is_digits(seconds_part))) {
    return std::nullopt;
  }

  SYSTEMTIME now{};
  GetLocalTime(&now);

  size_t pos = 0;
  int year = now.wYear;
  if (main.size() == 12) {
    year = *parse_int(std::string_view(main).substr(0, 4));
    pos = 4;
  } else if (main.size() == 10) {
    int yy = *parse_int(std::string_view(main).substr(0, 2));
    year = yy >= 69 ? 1900 + yy : 2000 + yy;
    pos = 2;
  }

  int month = *parse_int(std::string_view(main).substr(pos, 2));
  int day = *parse_int(std::string_view(main).substr(pos + 2, 2));
  int hour = *parse_int(std::string_view(main).substr(pos + 4, 2));
  int minute = *parse_int(std::string_view(main).substr(pos + 6, 2));
  int second = seconds_part.empty() ? 0 : *parse_int(seconds_part);

  // [GNU] posixtm admits leap seconds (ss 60/61), which mktime
  // normalizes into the following minute (posixtm.c:195-205).
  // SYSTEMTIME cannot hold them, so convert with 0 and add the
  // seconds onto the resulting FILETIME. (Pre-1601 years stay
  // rejected: the FILETIME epoch is a hard platform floor.)
  int leap_seconds = 0;
  if (second > 59) {
    leap_seconds = second;
    second = 0;
  }

  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(year);
  st.wMonth = static_cast<WORD>(month);
  st.wDay = static_cast<WORD>(day);
  st.wHour = static_cast<WORD>(hour);
  st.wMinute = static_cast<WORD>(minute);
  st.wSecond = static_cast<WORD>(second);
  auto converted = zone_time_to_filetime(st, std::nullopt);
  if (converted && leap_seconds > 0) {
    ULARGE_INTEGER ticks{};
    ticks.LowPart = converted->dwLowDateTime;
    ticks.HighPart = converted->dwHighDateTime;
    ticks.QuadPart += static_cast<uint64_t>(leap_seconds) * 10000000ULL;
    converted->dwLowDateTime = ticks.LowPart;
    converted->dwHighDateTime = ticks.HighPart;
  }
  return converted;
}

auto file_open_flags(bool no_dereference) -> DWORD {
  DWORD flags = FILE_FLAG_BACKUP_SEMANTICS;
  if (no_dereference) flags |= FILE_FLAG_OPEN_REPARSE_POINT;
  return flags;
}

auto read_times_from_file(const std::wstring& wpath, bool no_dereference,
                          DWORD* error = nullptr) -> std::optional<TimePair> {
  auto fail = [&](DWORD e) -> std::optional<TimePair> {
    if (error != nullptr) *error = e;
    return std::nullopt;
  };
  auto operand = native_path::make_api_path_operand_w(wpath);
  if (operand.had_trailing_separator &&
      native_path::attributes_are_regular_file(
          native_path::operand_target_attributes_w(operand))) {
    return fail(ERROR_DIRECTORY);
  }

  UniqueHandle h(CreateFileW(
      operand.extended.c_str(), FILE_READ_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      OPEN_EXISTING, file_open_flags(no_dereference), nullptr));
  if (!h) return fail(GetLastError());

  FILETIME c{}, a{}, m{};
  bool ok = GetFileTime(h.get(), &c, &a, &m) != 0;
  if (!ok) return fail(GetLastError());

  return TimePair{a, m};
}

auto touch_operand_error(std::string_view path, DWORD error) -> std::string {
  return "touch: cannot touch '" + std::string(path) + "': " +
         win32_posix_error_text(error, {.invalid_name_as_missing = true});
}

// [GNU] touch.c separates the open/create failure ("cannot touch
// %s: <err>", :194) from the time-setting failure ("setting times
// of %s: <err>", :200). Both post-open failures here used to print
// 'cannot touch' with NO reason text at all.
auto touch_setting_times_error(std::string_view path, DWORD error)
    -> std::string {
  return "touch: setting times of '" + std::string(path) + "': " +
         win32_posix_error_text(error, {.invalid_name_as_missing = true});
}

auto apply_touch_one(const std::string& path,
                     const CommandContext<TOUCH_OPTIONS.size()>& ctx,
                     bool update_access, bool update_modify, bool no_create,
                     bool no_dereference,
                     const std::optional<TimePair>& ref_times,
                     const std::optional<TimePair>& date_times) -> bool {
  auto operand = native_path::make_api_path_operand(path);
  if (operand.had_trailing_separator) {
    DWORD attrs = native_path::operand_target_attributes_w(operand);
    if (native_path::attributes_are_regular_file(attrs) ||
        !native_path::valid_attributes(attrs)) {
      if (no_create && !native_path::valid_attributes(attrs)) {
        return true;
      }
      safeErrorPrintLn(touch_operand_error(path, ERROR_DIRECTORY));
      return false;
    }
  }

  DWORD create_mode =
      (no_create || no_dereference) ? OPEN_EXISTING : OPEN_ALWAYS;
  UniqueHandle h(CreateFileW(
      operand.extended.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
      create_mode, file_open_flags(no_dereference), nullptr));

  if (!h && !no_create && !no_dereference) {
    DWORD attrs = native_path::operand_target_attributes_w(operand);
    if (attrs != INVALID_FILE_ATTRIBUTES &&
        (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
      h.reset(
          CreateFileW(operand.extended.c_str(),
                      FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                      nullptr, OPEN_EXISTING, file_open_flags(false), nullptr));
    }
  }

  if (!h) {
    DWORD e = GetLastError();
    // [GNU] With -h the failure is a time-setting failure on the
    // link itself: "setting times of '<f>': No such file or
    // directory" for a missing file, not "cannot touch".
    const bool setting_times_form = no_dereference;
    // [GNU] -c/--no-create skips files that do not exist. A literal-glob
    // operand with no matches (GNU glob semantics: unmatched patterns stay
    // literal) may name a file that cannot exist on this filesystem at all
    // ('x*x' — '*' is an illegal NTFS filename character, issue #1143).
    // Such a name is as good as missing, so -c skips it silently with exit
    // status 0, matching `touch -c 'x*x'` under GNU coreutils.
    if (no_create &&
        (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ||
         e == ERROR_INVALID_NAME || e == ERROR_INVALID_PARAMETER)) {
      return true;
    }
    safeErrorPrintLn(setting_times_form ? touch_setting_times_error(path, e)
                                        : touch_operand_error(path, e));
    return false;
  }

  FILETIME c{}, cur_a{}, cur_m{};
  if (!GetFileTime(h.get(), &c, &cur_a, &cur_m)) {
    safeErrorPrintLn(touch_operand_error(path, GetLastError()));
    return false;
  }

  FILETIME set_a = cur_a;
  FILETIME set_m = cur_m;

  if (date_times.has_value()) {
    // Use specified date/time from -d or -t option
    if (update_access) set_a = date_times->atime;
    if (update_modify) set_m = date_times->mtime;
  } else if (ref_times.has_value()) {
    // Use times from reference file
    if (update_access) set_a = ref_times->atime;
    if (update_modify) set_m = ref_times->mtime;
  } else {
    // Use current time
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    if (update_access) set_a = now;
    if (update_modify) set_m = now;
  }

  FILETIME* pa = update_access ? &set_a : nullptr;
  FILETIME* pm = update_modify ? &set_m : nullptr;

  bool ok = SetFileTime(h.get(), nullptr, pa, pm) != 0;

  if (!ok) {
    safeErrorPrintLn(touch_setting_times_error(path, GetLastError()));
    return false;
  }

  return true;
}

auto process_command(const CommandContext<TOUCH_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  bool flag_a = ctx.get<bool>("-a", false);
  bool flag_m = ctx.get<bool>("-m", false);

  // [GNU] --time is ADDITIVE: each occurrence ORs its mask into the
  // selection (touch.c:319-322, change_times |= XARGMATCH(...)), on
  // top of -a/-m. It used to REPLACE the selection (discarding a
  // sibling -a/-m) and only the last occurrence was ever read.
  // Values are argmatch-matched: any unambiguous prefix of the GNU
  // word set works, synonyms share a mask (T2/T3).
  for (const auto& occurrence : ctx.string_occurrences({"--time"})) {
    const std::string_view word = occurrence.value;
    struct TimeWord {
      std::string_view text;
      int mask;  // 1 = atime, 2 = mtime
    };
    static constexpr TimeWord time_words[] = {
        {"atime", 1}, {"access", 1}, {"use", 1},
        {"mtime", 2}, {"modify", 2}, {"change", 3},
    };
    int matched_mask = 0;
    size_t matches = 0;
    bool ambiguous = false;
    for (const TimeWord& candidate : time_words) {
      if (candidate.text == word) {
        matched_mask = candidate.mask;
        matches = 1;
        ambiguous = false;
        break;
      }
      if (candidate.text.starts_with(word)) {
        if (matched_mask != 0 && matched_mask != candidate.mask) {
          ambiguous = true;
        }
        matched_mask = candidate.mask;
        ++matches;
      }
    }
    if (matches == 0 || ambiguous) {
      return std::unexpected(
          "invalid argument '" + std::string(word) +
          "' for '--time'\nValid arguments are:\n"
          "  - 'atime', 'access', 'use'\n"
          "  - 'mtime', 'modify'\n"
          "  - 'change'");
    }
    if (matched_mask & 1) flag_a = true;
    if (matched_mask & 2) flag_m = true;
  }

  bool update_access = flag_a;
  bool update_modify = flag_m;
  if (!flag_a && !flag_m) {
    update_access = true;
    update_modify = true;
  }

  bool no_create =
      ctx.get<bool>("--no-create", false) || ctx.get<bool>("-c", false);

  bool no_dereference =
      ctx.get<bool>("--no-dereference", false) || ctx.get<bool>("-h", false);

  // [GNU] Time sources (touch.c:336-340): -t conflicts with -r and
  // with -d ("cannot specify times from more than one source");
  // -r and -d COMBINE, the -d string being evaluated against the
  // reference times (T5 below). Repeated -d/-t keep only the LAST
  // occurrence, evaluated once — occurrences used to be CHAINED,
  // each result seeding the next (T4).
  std::string last_date_value;
  std::string last_stamp_value;
  bool has_date = false;
  bool has_stamp = false;
  for (const auto& occurrence :
       ctx.string_occurrences({"--date", "-d", "-t"})) {
    if (occurrence.short_name == "-t") {
      has_stamp = true;
      last_stamp_value = occurrence.value;
    } else {
      has_date = true;
      last_date_value = occurrence.value;
    }
  }

  std::string ref_path = ctx.get<std::string>("--reference", "");
  if (ref_path.empty()) ref_path = ctx.get<std::string>("-r", "");

  if (has_stamp && (has_date || !ref_path.empty())) {
    return std::unexpected("cannot specify times from more than one source");
  }

  std::optional<TimePair> ref_times = std::nullopt;
  if (!ref_path.empty()) {
    // [GNU] single-line diagnostic with the errno text:
    // "touch: failed to get attributes of 'dang': No such file or directory"
    DWORD ref_error = 0;
    ref_times = read_times_from_file(utf8_to_wstring(ref_path), no_dereference,
                                     &ref_error);
    if (!ref_times.has_value()) {
      return std::unexpected(
          "failed to get attributes of '" + ref_path + "': " +
          win32_posix_error_text(ref_error, {.invalid_name_as_missing = true}));
    }
  }

  std::optional<TimePair> date_times = std::nullopt;
  if (has_stamp) {
    auto ft = parse_touch_timestamp(last_stamp_value);
    if (!ft.has_value()) {
      return std::unexpected("invalid date format '" + last_stamp_value + "'");
    }
    date_times = TimePair{*ft, *ft};
  } else if (has_date) {
    if (ref_times.has_value()) {
      // [GNU] -r + -d: the string is evaluated against the
      // reference ATIME for the atime field and the reference MTIME
      // for the mtime field (touch.c:354-360). A single mtime base
      // used to serve both (T5).
      auto atime_result =
          parse_gnu_date_string(last_date_value, ref_times->atime);
      auto mtime_result =
          parse_gnu_date_string(last_date_value, ref_times->mtime);
      if (!atime_result.has_value() || !mtime_result.has_value()) {
        return std::unexpected("invalid date format '" + last_date_value +
                               "'");
      }
      date_times = TimePair{*atime_result, *mtime_result};
    } else {
      FILETIME now{};
      GetSystemTimeAsFileTime(&now);
      auto ft = parse_gnu_date_string(last_date_value, now);
      if (!ft.has_value()) {
        return std::unexpected("invalid date format '" + last_date_value +
                               "'");
      }
      date_times = TimePair{*ft, *ft};
    }
  }

  (void)ctx.get<bool>("-f", false);

  // [GNU] All validation above precedes the missing-operand check
  // (touch.c: option errors abort during/right after getopt;
  // missing operand is diagnosed last, with the try-help line —
  // T6). This check used to be the FIRST statement here.
  if (ctx.positionals.empty()) {
    return std::unexpected("missing file operand");
  }

  bool all_ok = true;
  for (auto p : ctx.positionals) {
    std::string file_arg(p);
    std::vector<std::string> expanded;
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          expanded.push_back(wstring_to_utf8(file));
        }
      } else {
        expanded.push_back(file_arg);
      }
    } else {
      expanded.push_back(file_arg);
    }
    for (const auto& f : expanded) {
      if (f == "-") {
        // [GNU] Operand '-' retimes standard output (touch.c:
        // 127-128) instead of naming a literal file (T1).
        HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
        FILETIME set_a{}, set_m{};
        if (date_times.has_value()) {
          set_a = date_times->atime;
          set_m = date_times->mtime;
        } else if (ref_times.has_value()) {
          set_a = ref_times->atime;
          set_m = ref_times->mtime;
        } else {
          GetSystemTimeAsFileTime(&set_a);
          set_m = set_a;
        }
        FILETIME* pa = update_access ? &set_a : nullptr;
        FILETIME* pm = update_modify ? &set_m : nullptr;
        if (out == nullptr || out == INVALID_HANDLE_VALUE ||
            SetFileTime(out, nullptr, pa, pm) == 0) {
          safeErrorPrintLn(touch_setting_times_error(
              "-", out == nullptr || out == INVALID_HANDLE_VALUE
                       ? ERROR_INVALID_HANDLE
                       : GetLastError()));
          all_ok = false;
        }
        continue;
      }
      if (!apply_touch_one(f, ctx, update_access, update_modify, no_create,
                           no_dereference, ref_times, date_times)) {
        all_ok = false;
      }
    }
  }

  return all_ok;
}
}  // namespace touch_pipeline

REGISTER_COMMAND(touch, "touch", "touch [OPTION]... FILE...",
                 "Update the access and modification times of each FILE to the "
                 "current time.\n"
                 "A FILE argument that does not exist is created empty, unless "
                 "-c or -h is supplied.",
                 "  touch file.txt\n"
                 "  touch -a file.txt\n"
                 "  touch -m file.txt\n"
                 "  touch -c missing.txt\n"
                 "  touch -r ref.txt target.txt\n"
                 "  touch -h link.txt",
                 "stat(1), date(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 TOUCH_OPTIONS) {
  using namespace touch_pipeline;
  auto result = process_command(ctx);
  if (!result) {
    // [GNU] Usage-class failures carry the try-help line
    // (usage(EXIT_FAILURE) in touch.c).
    if (result.error() == "missing file operand" ||
        result.error() == "cannot specify times from more than one source") {
      safeErrorPrintLn("touch: " + result.error());
      safeErrorPrintLn("Try 'touch --help' for more information.");
      return 1;
    }
    cp::report_error(result, L"touch");
    return 1;
  }
  return *result ? 0 : 1;
}
