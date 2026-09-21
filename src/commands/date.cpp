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

#include <cstdlib>  // std::getenv
#include <ctime>    // _tzset, localtime_s, mktime (TZ env support)

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
    OPTION("-u", "--utc", "Coordinated Universal Time (UTC)"),
    // [DIFFERS]
    OPTION("-R", "--rfc-email", "output RFC 5322 compliant date string"),
    // [DIFFERS]
    OPTION("", "--rfc-2822", "output RFC 2822 compliant date string"),
    // [DIFFERS]
    OPTION("-I", "--iso-8601", "output ISO 8601 date/time",
           OPTIONAL_STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--rfc-3339", "output RFC 3339 date/time", STRING_TYPE),
    // [DIFFERS]
    OPTION("-r", "--reference", "display the last modification time of FILE",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("-f", "--file", "display date strings from DATEFILE, one per line",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("", "--universal", "alias for --utc"),
    // [GNU] --uct: deprecated alias for --utc; hidden like GNU
    OPTION("", "--uct", "", BOOL_TYPE),
    // [GNU] --rfc-822: deprecated alias for -R/--rfc-email; hidden like GNU
    OPTION("", "--rfc-822", "", BOOL_TYPE),
    // [GNU] --debug: annotate the parsed date, and warn about dubious usage
    OPTION("", "--debug",
           "annotate the parsed date, and warn about dubious usage to stderr"),
    // [GNU] --resolution: output the available resolution of timestamps
    OPTION("", "--resolution", "output the available resolution of timestamps"),
    // [GNU] --set: set time described by STRING
    OPTION("", "--set", "set time described by STRING", STRING_TYPE),
    // [DIFFERS] GNU: -s, --set=STRING; WinuxCmd: -s and --set are separate
    // entries
    OPTION("-s", "", "set time described by STRING", STRING_TYPE)};

namespace date_pipeline {
namespace cp = core::pipeline;

struct TimeValue {
  FILETIME utc{};
  SYSTEMTIME display{};
  bool utc_display = false;
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

// [GNU] Relative magnitudes are parsed without exceptions: a value that does
// not fit must yield "invalid date" (gnulib parse-datetime.y reports an
// overflow through ckd_* and fails the parse), never std::out_of_range from
// the throwing std::stoll family, which would abort the process.
auto parse_amount(std::string_view s) -> std::optional<long long> {
  if (s.starts_with('+')) s.remove_prefix(1);
  long long value = 0;
  auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), value);
  if (ec != std::errc() || ptr != s.data() + s.size()) return std::nullopt;
  return value;
}

// Checked accumulation mirroring gnulib's ckd_add / ckd_mul usage.
auto add_checked(long long &total, long long amount) -> bool {
  constexpr auto lo = std::numeric_limits<long long>::min();
  constexpr auto hi = std::numeric_limits<long long>::max();
  if (amount > 0 && total > hi - amount) return false;
  if (amount < 0 && total < lo - amount) return false;
  total += amount;
  return true;
}

auto mul_checked(long long a, long long b, long long &out) -> bool {
  constexpr auto lo = std::numeric_limits<long long>::min();
  if (a == 0 || b == 0) {
    out = 0;
    return true;
  }
  if ((a == -1 && b == lo) || (b == -1 && a == lo)) return false;
  out = a * b;
  return out / b == a;
}

auto days_in_month(int year, int month) -> int {
  static constexpr int days[] = {31, 28, 31, 30, 31, 30,
                                 31, 31, 30, 31, 30, 31};
  if (month != 2) return days[month - 1];
  bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
  return leap ? 29 : 28;
}

auto valid_system_time(const SYSTEMTIME &st) -> bool {
  if (st.wYear < 1601 || st.wMonth < 1 || st.wMonth > 12) return false;
  if (st.wDay < 1 || st.wDay > days_in_month(st.wYear, st.wMonth)) {
    return false;
  }
  return st.wHour <= 23 && st.wMinute <= 59 && st.wSecond <= 59;
}

auto filetime_to_ticks(const FILETIME &ft) -> unsigned long long {
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

auto utc_system_time_to_filetime(const SYSTEMTIME &utc)
    -> std::optional<FILETIME> {
  if (!valid_system_time(utc)) return std::nullopt;
  FILETIME ft{};
  if (!SystemTimeToFileTime(&utc, &ft)) return std::nullopt;
  return ft;
}

// [GNU] Honor the TZ environment variable like GNU date does. The CRT
// implements POSIX TZ parsing ("PST8PDT" and friends), so when TZ is set
// we route conversions through localtime/mktime; otherwise fall back to
// the process/system timezone. (Savannah #9089)
auto tz_env_active() -> bool {
  const char *tz = std::getenv("TZ");
  return tz != nullptr && *tz != '\0';
}

// [GNU] Parse the TZ environment variable as a POSIX proleptic fixed-offset
// zone — "NAME[±]hh[:mm[:ss]]" or "<NAME>[±]hh[:mm[:ss]]" — with NO daylight
// saving part. Returns the offset EAST of UTC in seconds (POSIX offsets count
// west, so the sign is inverted), or nullopt when the zone has DST rules or
// is otherwise not a plain fixed offset; callers then keep the CRT
// localtime/mktime path. Unlike the CRT (which rejects time_t before 1970
// and after ~3000), pure arithmetic covers the whole FILETIME range, so
// e.g. TZ=UTC0 date -d @253402300739 -> year 9999 (WinuxCmd#352).
auto env_tz_fixed_offset_seconds() -> std::optional<long long> {
  const char *tz_env = std::getenv("TZ");
  if (tz_env == nullptr || *tz_env == '\0') return std::nullopt;
  std::string s = tz_env;
  if (s.front() == ':') s.erase(0, 1);

  size_t pos = 0;
  std::string name;
  if (pos < s.size() && s[pos] == '<') {
    auto close = s.find('>', pos);
    if (close == std::string::npos) return std::nullopt;
    name = s.substr(pos + 1, close - pos - 1);
    pos = close + 1;
  } else {
    size_t start = pos;
    while (pos < s.size() && std::isalpha(static_cast<unsigned char>(s[pos]))) {
      ++pos;
    }
    name = s.substr(start, pos - start);
  }
  if (name.empty()) return std::nullopt;

  if (pos == s.size()) {
    // Bare zone name: only the unambiguous UTC spellings are fixed-offset.
    std::string upper = name;
    for (auto &c : upper) {
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    if (upper == "UTC" || upper == "GMT" || upper == "UT" || upper == "Z") {
      return 0;
    }
    return std::nullopt;
  }

  int sign = 1;
  if (s[pos] == '+' || s[pos] == '-') {
    sign = s[pos] == '-' ? -1 : 1;
    ++pos;
  }
  auto read_num = [&](int &out) -> bool {
    size_t start = pos;
    while (pos < s.size() && std::isdigit(static_cast<unsigned char>(s[pos]))) {
      ++pos;
    }
    if (pos == start) return false;
    out = *parse_int(std::string_view(s).substr(start, pos - start));
    return true;
  };
  int hours = 0, minutes = 0, seconds = 0;
  if (!read_num(hours)) return std::nullopt;
  if (pos < s.size() && s[pos] == ':') {
    ++pos;
    if (!read_num(minutes)) return std::nullopt;
    if (pos < s.size() && s[pos] == ':') {
      ++pos;
      if (!read_num(seconds)) return std::nullopt;
    }
  }
  // Trailing DST name or rule set ("PST8PDT", "EST5EDT,M3.2.0,...") -> not a
  // fixed zone; defer to the CRT implementation.
  if (pos != s.size() || hours > 24 || minutes > 59 || seconds > 59) {
    return std::nullopt;
  }
  return -sign * (hours * 3600LL + minutes * 60LL + seconds);
}

auto filetime_to_local_st(const FILETIME &ft) -> std::optional<SYSTEMTIME> {
  if (!tz_env_active()) {
    FILETIME local_ft{};
    if (!FileTimeToLocalFileTime(&ft, &local_ft)) return std::nullopt;
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&local_ft, &st)) return std::nullopt;
    return st;
  }
  // Fixed-offset POSIX TZ ("UTC0", "EST5", ...): shift arithmetically so the
  // whole FILETIME range works (CRT localtime_s stops at ~year 3000 and
  // refuses negative time_t).
  if (auto east = env_tz_fixed_offset_seconds()) {
    FILETIME shifted = add_seconds(ft, *east);
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&shifted, &st)) return std::nullopt;
    return st;
  }
  _tzset();
  const unsigned long long ticks = filetime_to_ticks(ft);
  const long long secs =
      static_cast<long long>(ticks / 10000000ULL) - 11644473600LL;
  const time_t t = static_cast<time_t>(secs);
  struct tm tmv{};
  if (localtime_s(&tmv, &t) != 0) return std::nullopt;
  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(tmv.tm_year + 1900);
  st.wMonth = static_cast<WORD>(tmv.tm_mon + 1);
  st.wDay = static_cast<WORD>(tmv.tm_mday);
  st.wHour = static_cast<WORD>(tmv.tm_hour);
  st.wMinute = static_cast<WORD>(tmv.tm_min);
  st.wSecond = static_cast<WORD>(tmv.tm_sec);
  st.wDayOfWeek = static_cast<WORD>(tmv.tm_wday);
  return st;
}

auto local_st_to_filetime(const SYSTEMTIME &local) -> std::optional<FILETIME> {
  if (!tz_env_active()) {
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
  if (auto east = env_tz_fixed_offset_seconds()) {
    if (!valid_system_time(local)) return std::nullopt;
    FILETIME as_utc{};
    if (!SystemTimeToFileTime(&local, &as_utc)) return std::nullopt;
    return add_seconds(as_utc, -*east);
  }
  if (!valid_system_time(local)) return std::nullopt;
  struct tm tmv{};
  tmv.tm_year = static_cast<int>(local.wYear) - 1900;
  tmv.tm_mon = static_cast<int>(local.wMonth) - 1;
  tmv.tm_mday = static_cast<int>(local.wDay);
  tmv.tm_hour = static_cast<int>(local.wHour);
  tmv.tm_min = static_cast<int>(local.wMinute);
  tmv.tm_sec = static_cast<int>(local.wSecond);
  tmv.tm_isdst = -1;
  _tzset();
  const time_t t = mktime(&tmv);
  if (t == static_cast<time_t>(-1)) return std::nullopt;
  const unsigned long long ticks =
      (static_cast<unsigned long long>(t) + 11644473600ULL) * 10000000ULL;
  return ticks_to_filetime(ticks);
}

auto local_system_time_to_filetime(const SYSTEMTIME &local)
    -> std::optional<FILETIME> {
  return local_st_to_filetime(local);
}

auto filetime_to_system_time(const FILETIME &ft, bool use_utc)
    -> std::optional<SYSTEMTIME> {
  if (use_utc) {
    SYSTEMTIME st{};
    if (!FileTimeToSystemTime(&ft, &st)) return std::nullopt;
    return st;
  }
  return filetime_to_local_st(ft);
}

auto parse_epoch_time(std::string_view s) -> std::optional<FILETIME> {
  if (s.empty() || s.front() != '@') return std::nullopt;
  long long seconds = 0;
  auto rest = s.substr(1);
  auto [ptr, ec] =
      std::from_chars(rest.data(), rest.data() + rest.size(), seconds);
  if (ec != std::errc() || ptr != rest.data() + rest.size())
    return std::nullopt;

  SYSTEMTIME epoch{};
  epoch.wYear = 1970;
  epoch.wMonth = 1;
  epoch.wDay = 1;
  auto ft = utc_system_time_to_filetime(epoch);
  if (!ft) return std::nullopt;
  return add_seconds(*ft, seconds);
}

// [GNU] parse-datetime.y time_zone_table: named fixed-offset zones. Values
// are minutes EAST of UTC; the *DT/*ST summer entries already include the
// DST hour (tDAYZONE = standard offset + 60). J is intentionally absent
// (military table handles it).
auto named_zone_offset_minutes(std::string_view name) -> std::optional<int> {
  static const std::pair<std::string_view, int> table[] = {
      {"GMT", 0},     {"UT", 0},     {"UTC", 0},     {"WET", 0},
      {"WEST", 60},   {"BST", 60},   {"ART", -180},  {"BRT", -180},
      {"BRST", -120}, {"NST", -210}, {"NDT", -150},  {"AST", -240},
      {"ADT", -180},  {"CLT", -240}, {"CLST", -180}, {"EST", -300},
      {"EDT", -240},  {"CST", -360}, {"CDT", -300},  {"MST", -420},
      {"MDT", -360},  {"PST", -480}, {"PDT", -420},  {"AKST", -540},
      {"AKDT", -480}, {"HST", -600}, {"HAST", -600}, {"HADT", -540},
      {"SST", -720},  {"WAT", 60},   {"CET", 60},    {"CEST", 120},
      {"MET", 60},    {"MEZ", 60},   {"MEST", 120},  {"MESZ", 120},
      {"EET", 120},   {"EEST", 180}, {"CAT", 120},   {"SAST", 120},
      {"EAT", 180},   {"MSK", 180},  {"MSD", 240},   {"IST", 330},
      {"SGT", 480},   {"KST", 540},  {"JST", 540},   {"GST", 600},
      {"NZST", 720},  {"NZDT", 780},
  };
  std::string upper(name);
  for (auto &c : upper) {
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  for (const auto &[zone, off] : table) {
    if (zone == upper) return off;
  }
  return std::nullopt;
}

auto parse_timezone_suffix(std::string &s) -> std::optional<int> {
  std::string lowered = lower_copy(s);
  if (lowered.ends_with(" utc") || lowered.ends_with(" gmt")) {
    s = trim_copy(s.substr(0, s.size() - 4));
    return 0;
  }
  // [GNU] Other named zones ("... PST", "... MEST"): a trailing alphabetic
  // word found in the gnulib zone table is a fixed offset (uutils#12954 /
  // WinuxCmd#358).
  {
    size_t end = s.find_last_not_of(' ');
    if (end != std::string::npos) {
      size_t begin = s.find_last_of(' ', end);
      std::string_view word = std::string_view(s).substr(
          begin == std::string::npos ? 0 : begin + 1,
          end - (begin == std::string::npos ? 0 : begin + 1) + 1);
      bool alpha = !word.empty() && std::ranges::all_of(word, [](char c) {
        return std::isalpha(static_cast<unsigned char>(c)) != 0;
      });
      if (alpha && begin != std::string::npos) {
        if (auto off = named_zone_offset_minutes(word)) {
          s = trim_copy(s.substr(0, begin));
          return *off;
        }
      }
    }
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

// [GNU] --debug annotation support: the parser records which grammar items
// it recognized (in input order) plus the aggregate relative offsets, so the
// caller can replay gnulib parse-datetime.y's debug trace (uutils#7342 /
// WinuxCmd#239). All fields are optional outputs; callers pass nullptr when
// --debug is not in effect.
struct DateDebugInfo {
  // (input position, text) pairs like "parsed date part: (Y-M-D) 2026-01-02".
  std::vector<std::pair<size_t, std::string>> items;
  bool epoch = false;
  bool zone_seen = false;  // explicit zone in the string (tZONE / military)
  int zone_seconds = 0;    // parsed zone offset, seconds east of UTC
  bool have_date = false;
  bool have_time = false;
  bool have_day = false;      // named weekday item ("next monday")
  std::string day_label;      // str_days() text, e.g. "next/first Mon"
  bool start_is_now = false;  // no explicit date/time: base is 'now'
  bool meridian_pm = false;   // trailing PM suffix was parsed
  int raw_hour = 0, raw_minute = 0, raw_second = 0;  // before meridian fix
  SYSTEMTIME start{};  // wall-clock start in the effective zone
  int nsec = 0;
  // Aggregate relative offsets (ago/hence already folded into the sign).
  long long rel_year = 0, rel_month = 0, rel_day = 0;
  long long rel_hour = 0, rel_minute = 0, rel_second = 0;
};

// gnulib time_zone_str(): "+HH[:MM[:SS]]" for a seconds-east offset.
auto debug_zone_str(int seconds_east) -> std::string {
  char buf[16]{};
  char sign = seconds_east < 0 ? '-' : '+';
  int total = std::abs(seconds_east);
  int hour = total / 3600;
  int rem = total % 3600;
  if (rem == 0) {
    snprintf(buf, sizeof(buf), "%c%02d", sign, hour);
  } else if (rem % 60 == 0) {
    snprintf(buf, sizeof(buf), "%c%02d:%02d", sign, hour, rem / 60);
  } else {
    snprintf(buf, sizeof(buf), "%c%02d:%02d:%02d", sign, hour, rem / 60,
             rem % 60);
  }
  return buf;
}

auto debug_date_str(const SYSTEMTIME &st) -> std::string {
  char buf[32]{};
  snprintf(buf, sizeof(buf), "(Y-M-D) %04u-%02u-%02u", st.wYear, st.wMonth,
           st.wDay);
  return buf;
}

auto debug_time_str(const SYSTEMTIME &st) -> std::string {
  char buf[16]{};
  snprintf(buf, sizeof(buf), "%02u:%02u:%02u", st.wHour, st.wMinute,
           st.wSecond);
  return buf;
}

auto debug_datetime_str(const SYSTEMTIME &st, bool zone_seen, int zone_seconds)
    -> std::string {
  std::string s = debug_date_str(st) + " " + debug_time_str(st);
  if (zone_seen) s += " TZ=" + debug_zone_str(zone_seconds);
  return s;
}

auto parse_fixed_date_time(std::string input, bool use_utc,
                           DateDebugInfo *dbg = nullptr)
    -> std::optional<FILETIME> {
  input = trim_copy(input);
  if (auto epoch = parse_epoch_time(input)) {
    if (dbg != nullptr) {
      dbg->epoch = true;
      // Re-derive the seconds for the annotation (the regex-free fast path
      // already validated them).
      long long secs = 0;
      auto rest = std::string_view(input).substr(1);
      std::from_chars(rest.data(), rest.data() + rest.size(), secs);
      dbg->items.emplace_back(
          0, "parsed number of seconds part: number of seconds: " +
                 std::to_string(secs));
    }
    return epoch;
  }

  // Named zones (" UTC"/" GMT"/trailing 'Z') are reported as a separate
  // "parsed zone part" item; a numeric ±hhmm/±hh:mm offset is folded into
  // the time item like gnulib does ("parsed time part: 03:04:05 UTC+08").
  const std::string before_tz = input;
  auto tz_offset = parse_timezone_suffix(input);
  bool named_zone = false;
  if (tz_offset && before_tz.size() != input.size()) {
    // A named zone (" PST", " UTC") was stripped when the removed suffix is
    // all alphabetic.
    std::string removed = trim_copy(before_tz.substr(input.size()));
    named_zone = !removed.empty() && std::ranges::all_of(removed, [](char c) {
      return std::isalpha(static_cast<unsigned char>(c)) != 0;
    });
  }
  std::ranges::replace(input, 'T', ' ');

  // Tokenize: locate the Y-M-D date token (loose single-digit fields and
  // '/' separators are accepted, uutils#6392 / WinuxCmd#979); the remaining
  // tokens in original order form the wall-clock time, so both
  // "2026-01-02 10:00 PM" and GNU's reversed "10:00 PM 2026-01-01" parse
  // (uutils#9253 / WinuxCmd#264, Savannah#16214 / WinuxCmd#384).
  static const std::regex date_token_re(
      R"(^[0-9]{4}[-/][0-9]{1,2}[-/][0-9]{1,2}$|^[0-9]{8}$)");
  std::string date_part;
  std::string time_part;
  size_t date_pos = std::string::npos;
  size_t time_pos = std::string::npos;
  {
    std::vector<std::string> time_tokens;
    std::istringstream iss(input);
    std::string tok;
    bool date_found = false;
    while (iss >> tok) {
      if (!date_found && std::regex_match(tok, date_token_re)) {
        date_part = tok;
        date_pos = input.find(tok);
        date_found = true;
      } else {
        time_tokens.push_back(tok);
      }
    }
    for (size_t i = 0; i < time_tokens.size(); ++i) {
      if (i) time_part += ' ';
      time_part += time_tokens[i];
    }
    if (!time_tokens.empty()) {
      time_pos = input.find(time_tokens.front());
    }
    if (!date_found) {
      // [GNU] A bare wall-clock time uses the current date: "-d '15:04'"
      // is today at 15:04. Anything else still fails the date parse below
      // unless it forms a valid time on its own.
      time_part = trim_copy(input);
      if (time_pos == std::string::npos && !time_tokens.empty()) {
        time_pos = 0;
      }
    }
  }

  int year = 0;
  int month = 0;
  int day = 0;
  if (date_part.empty()) {
    // No date token: GNU keeps the current date in the effective zone.
    FILETIME now_ft{};
    GetSystemTimeAsFileTime(&now_ft);
    std::optional<SYSTEMTIME> today;
    if (tz_offset || use_utc) {
      // For an explicit parsed zone the current date is taken in that zone.
      FILETIME shifted =
          add_seconds(now_ft, tz_offset ? *tz_offset * 60LL : 0LL);
      SYSTEMTIME utc_st{};
      if (!FileTimeToSystemTime(&shifted, &utc_st)) return std::nullopt;
      today = utc_st;
    } else {
      today = filetime_to_local_st(now_ft);
    }
    if (!today) return std::nullopt;
    year = today->wYear;
    month = today->wMonth;
    day = today->wDay;
    if (dbg != nullptr) dbg->start_is_now = true;
  } else {
    static const std::regex ymd_re(
        R"(^([0-9]{4})([-/])([0-9]{1,2})\2([0-9]{1,2})$)");
    std::smatch ymd;
    if (std::regex_match(date_part, ymd, ymd_re)) {
      year = std::stoi(ymd[1].str());
      month = std::stoi(ymd[3].str());
      day = std::stoi(ymd[4].str());
    } else if (date_part.size() == 8 && is_digits(date_part)) {
      year = *parse_int(std::string_view(date_part).substr(0, 4));
      month = *parse_int(std::string_view(date_part).substr(4, 2));
      day = *parse_int(std::string_view(date_part).substr(6, 2));
    } else {
      return std::nullopt;
    }
  }

  int hour = 0;
  int minute = 0;
  int second = 0;
  int frac_nsec = 0;
  if (!time_part.empty()) {
    // [GNU] Optional trailing AM/PM (case-insensitive, with or without a
    // space: "03:04:05 PM" and "03:04:05PM"). 12 AM = 00:xx, 12 PM = 12:xx,
    // and hour values above 12 combined with AM/PM are rejected
    // (uutils#9253 / WinuxCmd#264).
    // gnulib's meridian_table also spells these "A.M." and "P.M.", which are
    // valid for date(1) ("2026-01-02 03:04:05 P.M." -> 15:04). Drop a trailing
    // period and the period between the letters so all four spellings reach
    // one suffix test.
    std::string tp = time_part;
    if (auto last = tp.find_last_not_of(' ');
        last != std::string::npos && tp[last] == '.') {
      tp.erase(last);
      auto space = tp.find_last_of(' ');
      auto begin = space == std::string::npos ? size_t{0} : space + 1;
      tp.erase(std::remove(tp.begin() + static_cast<std::ptrdiff_t>(begin),
                           tp.end(), '.'),
               tp.end());
      tp = trim_copy(tp);
    }
    std::string tp_lower = lower_copy(tp);
    bool has_pm = false;
    bool has_am = false;
    auto strip_ampm = [&](std::string_view suffix) {
      if (tp_lower.size() >= suffix.size() &&
          tp_lower.compare(tp_lower.size() - suffix.size(), suffix.size(),
                           suffix) == 0) {
        tp = trim_copy(tp.substr(0, tp.size() - suffix.size()));
        tp_lower = lower_copy(tp);
        return true;
      }
      return false;
    };
    if (strip_ampm("pm"))
      has_pm = true;
    else if (strip_ampm("am"))
      has_am = true;

    std::vector<std::string_view> pieces;
    std::string_view tv = tp;
    while (true) {
      auto colon = tv.find(':');
      pieces.push_back(tv.substr(0, colon));
      if (colon == std::string_view::npos) break;
      tv.remove_prefix(colon + 1);
    }
    if (pieces.size() < 2 || pieces.size() > 3) return std::nullopt;
    // [GNU] The seconds field may carry a fractional part
    // ("03:04:05.123456789", e.g. ls -l / stat output; WinuxCmd#976).
    // GNU keeps full nanosecond resolution; FILETIME only stores 100ns
    // ticks, so digits past the 7th are truncated.
    if (pieces.size() == 3) {
      if (auto dot = pieces[2].find('.'); dot != std::string_view::npos) {
        auto frac = pieces[2].substr(dot + 1);
        if (frac.empty() || !is_digits(frac)) return std::nullopt;
        for (size_t i = 0; i < 9; ++i) {
          frac_nsec = frac_nsec * 10 + (i < frac.size() ? frac[i] - '0' : 0);
        }
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
    if (dbg != nullptr) {
      dbg->nsec = frac_nsec;
      dbg->raw_hour = hour;
      dbg->raw_minute = minute;
      dbg->raw_second = second;
    }
    if (has_am || has_pm) {
      if (hour < 1 || hour > 12) return std::nullopt;
      if (has_pm && hour < 12)
        hour += 12;
      else if (has_am && hour == 12)
        hour = 0;
    }
    if (dbg != nullptr && has_pm) dbg->meridian_pm = true;
  }

  if (dbg != nullptr && !time_part.empty()) dbg->have_time = true;

  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(year);
  st.wMonth = static_cast<WORD>(month);
  st.wDay = static_cast<WORD>(day);
  st.wHour = static_cast<WORD>(hour);
  st.wMinute = static_cast<WORD>(minute);
  st.wSecond = static_cast<WORD>(second);

  if (dbg != nullptr) {
    if (!date_part.empty()) {
      char item[64]{};
      snprintf(item, sizeof(item), "parsed date part: (Y-M-D) %04d-%02d-%02d",
               year, month, day);
      dbg->items.emplace_back(date_pos, item);
      dbg->have_date = true;
    }
    if (dbg->have_time) {
      // gnulib prints the raw parsed time with a "pm" suffix, before the
      // meridian is folded in ("03:04:00pm").
      char raw[16]{};
      snprintf(raw, sizeof(raw), "%02d:%02d:%02d", dbg->raw_hour,
               dbg->raw_minute, dbg->raw_second);
      std::string item = std::string("parsed time part: ") + raw;
      if (dbg->nsec != 0) {
        char ns[16]{};
        snprintf(ns, sizeof(ns), ".%09d", dbg->nsec);
        item += ns;
      }
      if (dbg->meridian_pm) item += "pm";
      if (tz_offset && !named_zone) {
        item += " UTC" + debug_zone_str(*tz_offset * 60);
      }
      dbg->items.emplace_back(time_pos == std::string::npos ? 0 : time_pos,
                              item);
    }
    if (named_zone) {
      // " UTC"/" PST" suffix: separate "parsed zone part" item.
      dbg->items.emplace_back(
          input.size(),
          "parsed zone part: UTC" + debug_zone_str(*tz_offset * 60));
      dbg->zone_seen = true;
      dbg->zone_seconds = *tz_offset * 60;
    } else if (tz_offset) {
      dbg->zone_seen = true;
      dbg->zone_seconds = *tz_offset * 60;
      if (!dbg->have_time) {
        dbg->items.emplace_back(
            input.size(),
            "parsed zone part: UTC" + debug_zone_str(*tz_offset * 60));
      }
    }
    dbg->start = st;
  }

  // Fold the parsed fractional seconds into the FILETIME (100ns ticks).
  const long long frac_ticks = frac_nsec / 100;

  if (tz_offset) {
    auto ft = utc_system_time_to_filetime(st);
    if (!ft) return std::nullopt;
    return ticks_to_filetime(
        filetime_to_ticks(
            add_seconds(*ft, -static_cast<long long>(*tz_offset) * 60)) +
        frac_ticks);
  }
  // [GNU] -u/--utc makes zone-less date strings parse as UTC instead of
  // local time.
  std::optional<FILETIME> result = use_utc ? utc_system_time_to_filetime(st)
                                           : local_system_time_to_filetime(st);
  if (result && frac_ticks != 0) {
    result = ticks_to_filetime(filetime_to_ticks(*result) + frac_ticks);
  }
  return result;
}

auto timezone_offset_minutes(const FILETIME &utc, bool use_utc) -> int {
  if (use_utc) return 0;
  auto local = filetime_to_local_st(utc);
  if (!local) return 0;
  // Truncate both sides to whole seconds before differencing: the incoming
  // FILETIME carries sub-second ticks, and integer division by 60s would
  // otherwise drop a whole minute (480 -> 479) whenever the sub-second part
  // leaks into the difference. This broke military timezone conversion by
  // one minute (WinuxCmd#354 follow-up).
  local->wMilliseconds = 0;
  SYSTEMTIME utc_st{};
  if (!FileTimeToSystemTime(&utc, &utc_st)) return 0;
  utc_st.wMilliseconds = 0;
  FILETIME utc_trunc{};
  if (!SystemTimeToFileTime(&utc_st, &utc_trunc)) return 0;
  FILETIME as_utc{};
  if (!SystemTimeToFileTime(&*local, &as_utc)) return 0;
  auto diff = static_cast<long long>(filetime_to_ticks(as_utc)) -
              static_cast<long long>(filetime_to_ticks(utc_trunc));
  return static_cast<int>(diff / (10000000LL * 60));
}

auto format_zone_offset(int minutes, bool colon) -> std::string {
  char buf[8]{};
  char sign = minutes < 0 ? '-' : '+';
  int total = std::abs(minutes);
  int hours = total / 60;
  int mins = total % 60;
  if (colon) {
    snprintf(buf, sizeof(buf), "%c%02d:%02d", sign, hours, mins);
  } else {
    snprintf(buf, sizeof(buf), "%c%02d%02d", sign, hours, mins);
  }
  return buf;
}

auto timezone_name(const FILETIME &utc, bool use_utc) -> std::string {
  if (use_utc) return "UTC";
  int offset = timezone_offset_minutes(utc, false);
  if (offset == 0) return "UTC";
  if (offset == 8 * 60) return "CST";
  if (offset == -5 * 60) return "EST";
  if (offset == -4 * 60) return "EDT";
  if (offset == -6 * 60) return "CST";
  if (offset == -7 * 60) return "MST";
  if (offset == -8 * 60) return "PST";
  return format_zone_offset(offset, false);
}

auto day_of_year(const SYSTEMTIME &st) -> int {
  int day = st.wDay;
  for (int month = 1; month < st.wMonth; ++month) {
    day += days_in_month(st.wYear, month);
  }
  return day;
}

auto epoch_seconds(const FILETIME &ft) -> long long {
  SYSTEMTIME epoch{};
  epoch.wYear = 1970;
  epoch.wMonth = 1;
  epoch.wDay = 1;
  auto epoch_ft = utc_system_time_to_filetime(epoch).value();
  auto diff = static_cast<long long>(filetime_to_ticks(ft)) -
              static_cast<long long>(filetime_to_ticks(epoch_ft));
  return diff / 10000000LL;
}

auto append_number(std::string &out, int value, int width, char fill = '0') {
  char buf[32]{};
  snprintf(buf, sizeof(buf), fill == ' ' ? "%*d" : "%0*d", width, value);
  out += buf;
}

// Left-pad s with fill until it is at least width chars long.
auto pad_left(std::string s, size_t width, char fill) -> std::string {
  if (s.size() >= width) return s;
  s.insert(0, width - s.size(), fill);
  return s;
}

// [GNU] %^ forces upper case, %# swaps the natural case of textual output
// (gnulib strftime semantics: to_lowcase wins over to_uppcase).
auto apply_case(std::string text, bool to_uppcase, bool to_lowcase)
    -> std::string {
  if (to_lowcase) {
    std::ranges::transform(text, text.begin(), [](unsigned char ch) {
      return static_cast<char>(std::tolower(ch));
    });
  } else if (to_uppcase) {
    std::ranges::transform(text, text.begin(), [](unsigned char ch) {
      return static_cast<char>(std::toupper(ch));
    });
  }
  return text;
}

auto format_time(const TimeValue &tv, const std::string &format)
    -> std::string {
  const SYSTEMTIME &st = tv.display;
  int zone_minutes = timezone_offset_minutes(tv.utc, tv.utc_display);
  std::string result;
  result.reserve(format.size() * 2);

  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] == '%') {
      // [GNU] case modifiers parsed before the specifier; per-spec state
      // is reset for every '%' (to_uppcase inherits nothing here because
      // composite expansions re-enter format_time recursively).
      bool to_uppcase = false;
      bool to_lowcase = false;
      bool change_case = false;
      bool colon_zone = false;
      // [GNU] numeric width/padding flags: '-' (no pad), '_' (space pad),
      // '0' (zero pad, default) and a decimal width that overrides the
      // per-specifier default precision (%02j -> "01", %3N -> milliseconds).
      char pad_char = '0';
      bool no_pad = false;
      int width = 0;
      bool has_width = false;
      const size_t spec_start = i;
      ++i;
      while (i < format.size()) {
        char flag = format[i];
        if (flag == '^') {
          to_uppcase = true;
        } else if (flag == '#') {
          change_case = true;
        } else if (flag == ':' && !colon_zone) {
          colon_zone = true;
        } else if (flag == '-') {
          no_pad = true;
        } else if (flag == '_') {
          pad_char = ' ';
        } else if (flag == 'E' || flag == 'O') {
          // [GNU] %E/%O select the locale's alternative representation;
          // in the C/POSIX locale (and on Windows, which has no alternate
          // digits/eras) they fall back to the base conversion (#1088).
        } else if (flag >= '0' && flag <= '9') {
          has_width = true;
          width = width * 10 + (flag - '0');
        } else {
          break;
        }
        ++i;
      }
      if (i >= format.size()) {
        // Dangling '%' (optionally with flags): emit verbatim.
        result.append(format, spec_start, format.size() - spec_start);
        break;
      }
      char spec = format[i];

      switch (spec) {
        case '%':
          result += '%';
          break;
        case 'n':
          result += '\n';
          break;
        case 't':
          result += '\t';
          break;
        case 'Y':
          append_number(result, st.wYear, 4);
          break;
        case 'y':
          append_number(result, st.wYear % 100, 2);
          break;
        case 'C':
          append_number(result, st.wYear / 100, 2);
          break;
        case 'm':
          append_number(result, st.wMonth, 2);
          break;
        case 'd':
          append_number(result, st.wDay, 2);
          break;
        case 'e':
          append_number(result, st.wDay, 2, ' ');
          break;
        case 'H':
          append_number(result, st.wHour, 2);
          break;
        case 'I':
          append_number(result, (st.wHour % 12 == 0) ? 12 : st.wHour % 12, 2);
          break;
        case 'M':
          append_number(result, st.wMinute, 2);
          break;
        case 'S':
          append_number(result, st.wSecond, 2);
          break;
        case 'N': {
          // [GNU] %N prints 9-digit nanoseconds. Derived from the FILETIME
          // 100ns ticks (rather than wMilliseconds) so parsed fractional
          // seconds like "...05.123456789" keep their precision
          // (WinuxCmd#976); the last two digits are always 0 on this
          // platform.
          std::string nanos = pad_left(
              std::to_string(static_cast<long long>(filetime_to_ticks(tv.utc) %
                                                    10000000ULL) *
                             100),
              9, '0');
          if (no_pad) {
            result += nanos.substr(0, 1);
          } else if (has_width) {
            if (width < static_cast<int>(nanos.size())) {
              result += nanos.substr(0, width);
            } else {
              result += pad_left(nanos, width, '0');
            }
          } else {
            result += nanos;
          }
          break;
        }
        case 'p': {
          std::string text = st.wHour < 12 ? "AM" : "PM";
          // [GNU] %#p prints the meridiem in lower case.
          if (change_case) {
            to_uppcase = false;
            to_lowcase = true;
          }
          result += apply_case(std::move(text), to_uppcase, to_lowcase);
          break;
        }
        case 'P': {
          // [GNU] %P always prints lower case; to_lowcase wins over '^'.
          std::string text = st.wHour < 12 ? "AM" : "PM";
          to_lowcase = true;
          result += apply_case(std::move(text), to_uppcase, to_lowcase);
          break;
        }
        case 'a':
        case 'A': {
          static const char *weekday_names[] = {
              "Sunday",   "Monday", "Tuesday", "Wednesday",
              "Thursday", "Friday", "Saturday"};
          static const char *weekday_abbr[] = {"Sun", "Mon", "Tue", "Wed",
                                               "Thu", "Fri", "Sat"};
          int day_of_week = st.wDayOfWeek;
          // [GNU] %^a/%#a both force upper case.
          if (change_case) {
            to_uppcase = true;
            to_lowcase = false;
          }
          std::string text = (spec == 'a') ? weekday_abbr[day_of_week]
                                           : weekday_names[day_of_week];
          result += apply_case(std::move(text), to_uppcase, to_lowcase);
          break;
        }
        case 'b':
        case 'h':
        case 'B': {
          static const char *month_names[] = {
              "January",   "February", "March",    "April",
              "May",       "June",     "July",     "August",
              "September", "October",  "November", "December"};
          static const char *month_abbr[] = {"Jan", "Feb", "Mar", "Apr",
                                             "May", "Jun", "Jul", "Aug",
                                             "Sep", "Oct", "Nov", "Dec"};
          // [GNU] %^b/%#b both force upper case.
          if (change_case) {
            to_uppcase = true;
            to_lowcase = false;
          }
          std::string text = (spec == 'b') ? month_abbr[st.wMonth - 1]
                                           : month_names[st.wMonth - 1];
          result += apply_case(std::move(text), to_uppcase, to_lowcase);
          break;
        }
        case 'F':
          result +=
              apply_case(format_time(tv, "%Y-%m-%d"), to_uppcase, to_lowcase);
          break;
        case 'D':
          result +=
              apply_case(format_time(tv, "%m/%d/%y"), to_uppcase, to_lowcase);
          break;
        case 'T':
          result +=
              apply_case(format_time(tv, "%H:%M:%S"), to_uppcase, to_lowcase);
          break;
        case 'R':
          result +=
              apply_case(format_time(tv, "%H:%M"), to_uppcase, to_lowcase);
          break;
        case 'r':
          result += apply_case(format_time(tv, "%I:%M:%S %p"), to_uppcase,
                               to_lowcase);
          break;
        case 'c':
          result += apply_case(format_time(tv, "%a %b %e %H:%M:%S %Y"),
                               to_uppcase, to_lowcase);
          break;
        case 'x':
          result +=
              apply_case(format_time(tv, "%m/%d/%y"), to_uppcase, to_lowcase);
          break;
        case 'X':
          result +=
              apply_case(format_time(tv, "%H:%M:%S"), to_uppcase, to_lowcase);
          break;
        case 'j': {
          const int doy = day_of_year(st);
          if (has_width) {
            std::string digits = std::to_string(doy);
            if (no_pad) {
              result += digits;
            } else if (static_cast<int>(digits.size()) < width) {
              result.append(width - digits.size(), pad_char);
            }
            result += digits;
          } else {
            append_number(result, doy, 3);
          }
          break;
        }
        case 'q':
          // [GNU] %q: quarter of year (1-4).
          append_number(result, (st.wMonth - 1) / 3 + 1, 1);
          break;
        case 'u':
          result +=
              static_cast<char>('0' + (st.wDayOfWeek == 0 ? 7 : st.wDayOfWeek));
          break;
        case 'w':
          result += static_cast<char>('0' + st.wDayOfWeek);
          break;
        case 's':
          result += std::to_string(epoch_seconds(tv.utc));
          break;
        case 'z':
          result += format_zone_offset(zone_minutes, colon_zone);
          break;
        case 'Z': {
          // [GNU] %#Z prints the zone name in lower case.
          if (change_case) {
            to_uppcase = false;
            to_lowcase = true;
          }
          result += apply_case(timezone_name(tv.utc, tv.utc_display),
                               to_uppcase, to_lowcase);
          break;
        }
        default:
          result.append(format, spec_start, i - spec_start + 1);
          break;
      }
    } else {
      result += format[i];
    }
  }

  return result;
}

auto make_time_value(FILETIME utc, bool use_utc) -> std::optional<TimeValue> {
  auto display = filetime_to_system_time(utc, use_utc);
  if (!display) return std::nullopt;
  return TimeValue{utc, *display, use_utc};
}

auto current_time_value(bool use_utc) -> TimeValue {
  FILETIME utc{};
  GetSystemTimeAsFileTime(&utc);
  return make_time_value(utc, use_utc).value();
}

auto read_reference_time(std::string_view path) -> std::optional<FILETIME> {
  WIN32_FILE_ATTRIBUTE_DATA attributes{};
  if (!GetFileAttributesExW(utf8_to_wstring(std::string(path)).c_str(),
                            GetFileExInfoStandard, &attributes)) {
    return std::nullopt;
  }
  return attributes.ftLastWriteTime;
}

// [GNU] Relative date items: "[+|-]N unit" chains appended after a base
// date (or standing alone, relative to now), e.g. "2026-01-01 +1 month",
// "-2 days", "+1 month +2 days" (WinuxCmd#975 #383 #386).
// Month/year items are summed and applied as calendar arithmetic with
// month-end rollover ("2026-01-31 +1 month +1 month" = 2026-03-31,
// "2024-02-29 +1 year" = 2025-03-01); the rest accumulate as seconds.
struct RelativeItem {
  long long amount = 0;
  bool calendar = false;          // month/year items
  long long months_per_unit = 1;  // year items count as 12 calendar months
  long long unit_seconds = 1;     // scale for non-calendar units
};

// Strip trailing relative items from the end of the string, returning them
// in encounter order. Leaves the remainder (base date) in place. Returns
// nullopt when an amount does not fit in the relative accumulator, which the
// caller turns into GNU's "invalid date" (see parse_amount).
//
// [GNU] Each item may carry a trailing "ago"/"hence" (parse-datetime.y
// `rel: relunit tAGO`): "ago" negates just that item, matching GNU's
// per-relunit sign. Weeks/fortnights count toward the day aggregate, like
// gnulib's tDAY_UNIT multipliers (week=7, fortnight=14).
auto strip_relative_items(std::string &s, DateDebugInfo *dbg = nullptr)
    -> std::optional<std::vector<RelativeItem>> {
  static const std::regex item_re(
      R"(([+-]?[0-9]+)\s*(fortnights|fortnight|seconds|second|secs|sec|minutes|minute|mins|min|hours|hour|days|day|weeks|week|months|month|years|year)\s*(ago|hence)?\s*$)",
      std::regex::icase);
  std::vector<RelativeItem> items;
  std::vector<std::pair<size_t, std::string>> dbg_items;
  std::string work = trim_copy(s);
  std::smatch m;
  while (std::regex_search(work, m, item_re) &&
         m.position(0) + m.length(0) == work.size()) {
    const auto amount = parse_amount(m[1].str());
    if (!amount) return std::nullopt;
    std::string unit = lower_copy(m[2].str());
    bool is_year = unit.starts_with("year");
    bool is_month = unit.starts_with("month");
    long long effective = *amount;
    if (m[3].matched && lower_copy(m[3].str()) == "ago") {
      effective = -effective;
    }
    long long unit_seconds = 1;
    long long day_scale = 0;
    if (unit.starts_with("fortnight")) {
      unit_seconds = 1209600;
      day_scale = 14;
    } else if (unit.starts_with("week")) {
      unit_seconds = 604800;
      day_scale = 7;
    } else if (unit.starts_with("day")) {
      unit_seconds = 86400;
      day_scale = 1;
    } else if (unit.starts_with("hour")) {
      unit_seconds = 3600;
    } else if (unit.starts_with("min")) {
      unit_seconds = 60;
    }
    items.push_back(
        {effective, is_month || is_year, is_year ? 12 : 1, unit_seconds});
    if (dbg != nullptr) {
      if (is_year)
        dbg->rel_year += effective;
      else if (is_month)
        dbg->rel_month += effective;
      else if (day_scale != 0)
        dbg->rel_day += effective * day_scale;
      else if (unit_seconds == 3600)
        dbg->rel_hour += effective;
      else if (unit_seconds == 60)
        dbg->rel_minute += effective;
      else
        dbg->rel_second += effective;
      // Items are stripped right-to-left; positions are absolute in the
      // original string, so sort order is recovered by reversing below.
      dbg_items.emplace_back(m.position(0), "");
    }
    work = trim_copy(work.substr(0, m.position(0)));
  }
  if (dbg != nullptr && !dbg_items.empty()) {
    // Replay in input order with running totals to match gnulib's
    // cumulative "parsed relative part:" annotations.
    long long ry = 0, rm = 0, rd = 0, rh = 0, rmin = 0, rs = 0;
    for (size_t i = items.size(); i-- > 0;) {
      const auto &it = items[i];
      long long scaled =
          it.amount * (it.calendar ? it.months_per_unit : it.unit_seconds);
      if (it.calendar && it.months_per_unit == 12)
        ry += it.amount;
      else if (it.calendar)
        rm += it.amount;
      else if (it.unit_seconds % 86400 == 0)
        rd += scaled / 86400;
      else if (it.unit_seconds == 3600)
        rh += it.amount;
      else if (it.unit_seconds == 60)
        rmin += it.amount;
      else
        rs += scaled;
      std::string line = "parsed relative part:";
      auto rel_part = [&](long long v, const char *name) {
        if (v != 0) {
          char buf[40]{};
          snprintf(buf, sizeof(buf), " %+lld %s", v, name);
          line += buf;
        }
      };
      rel_part(ry, "year(s)");
      rel_part(rm, "month(s)");
      rel_part(rd, "day(s)");
      rel_part(rh, "hour(s)");
      rel_part(rmin, "minutes");
      rel_part(rs, "seconds");
      dbg->items.emplace_back(dbg_items[i].first, line);
    }
  }
  s = work;
  return items;
}

auto apply_relative_items(const FILETIME &base,
                          const std::vector<RelativeItem> &items, bool use_utc)
    -> std::optional<FILETIME> {
  long long delta_seconds = 0;
  long long delta_months = 0;
  for (const auto &item : items) {
    long long scaled = 0;
    if (!mul_checked(item.amount,
                     item.calendar ? item.months_per_unit : item.unit_seconds,
                     scaled))
      return std::nullopt;
    if (!add_checked(item.calendar ? delta_months : delta_seconds, scaled))
      return std::nullopt;
  }

  FILETIME result = base;
  if (delta_months != 0) {
    // Calendar arithmetic in the display frame (local or UTC)
    SYSTEMTIME st{};
    if (use_utc) {
      if (!FileTimeToSystemTime(&result, &st)) return std::nullopt;
    } else {
      auto local = filetime_to_local_st(result);
      if (!local) return std::nullopt;
      st = *local;
    }
    long long total =
        static_cast<long long>(st.wYear) * 12 + (st.wMonth - 1) + delta_months;
    int year = static_cast<int>(total / 12);
    int month = static_cast<int>(total % 12) + 1;
    if (year < 1 || year > 9999) return std::nullopt;
    int day = st.wDay;
    while (day > days_in_month(year, month)) {
      day -= days_in_month(year, month);
      if (++month > 12) {
        month = 1;
        ++year;
        if (year > 9999) return std::nullopt;
      }
    }
    st.wYear = static_cast<WORD>(year);
    st.wMonth = static_cast<WORD>(month);
    st.wDay = static_cast<WORD>(day);
    auto converted = use_utc ? utc_system_time_to_filetime(st)
                             : local_system_time_to_filetime(st);
    if (!converted) return std::nullopt;
    result = *converted;
  }
  if (delta_seconds != 0) result = add_seconds(result, delta_seconds);
  return result;
}

auto parse_date_argument(const std::string &arg, bool use_utc,
                         DateDebugInfo *dbg = nullptr)
    -> std::optional<FILETIME> {
  std::string value = trim_copy(arg);
  std::string lower = lower_copy(value);
  FILETIME now{};
  GetSystemTimeAsFileTime(&now);
  auto relative = [&](long long amount, long long unit) {
    return add_seconds(now, amount * unit);
  };

  // [GNU] Trailing relative items ("2026-01-01 +1 month", "+2 days",
  // "1 hour", "3 days ago"). The remainder is parsed as the base date;
  // with no remainder the base is now.
  {
    std::string work = value;
    auto rel_items = strip_relative_items(work, dbg);
    // An unrepresentable amount is an invalid date, exactly like GNU's
    // overflow check in parse-datetime.y — never a thrown exception.
    if (!rel_items) return std::nullopt;
    if (!rel_items->empty()) {
      std::string rest = trim_copy(std::move(work));
      std::optional<FILETIME> base;
      if (rest.empty()) {
        base = now;
        if (dbg != nullptr) dbg->start_is_now = true;
      } else {
        base = parse_date_argument(rest, use_utc, dbg);
      }
      if (!base) return std::nullopt;
      auto applied = apply_relative_items(*base, *rel_items, use_utc);
      if (!applied) return std::nullopt;
      return *applied;
    }
  }

  // [GNU] Natural language date support. "today"/"now" are a zero-valued
  // relative item ("today/this/now"); "tomorrow"/"yesterday" are day shifts
  // that keep the current wall-clock time.
  if (lower == "now" || lower == "today") {
    if (dbg != nullptr) {
      dbg->items.emplace_back(0, "parsed relative part: today/this/now");
      dbg->start_is_now = true;
    }
    return now;
  }
  if (lower == "tomorrow" || lower == "yesterday") {
    const long long shift = lower == "tomorrow" ? 1 : -1;
    if (dbg != nullptr) {
      char buf[64]{};
      snprintf(buf, sizeof(buf), "parsed relative part: %+lld day(s)", shift);
      dbg->items.emplace_back(0, buf);
      dbg->rel_day += shift;
      dbg->start_is_now = true;
    }
    return relative(shift, 86400);
  }

  // [GNU] "yesterday 10:00 GMT" / "tomorrow 09:30" / "today 08:00" — a
  // relative day word followed by a wall-clock time and an optional
  // UTC-ish zone name (uutils #10788). Parsed manually to keep the
  // hot path free of regex machinery.
  {
    const std::string_view day_words[] = {"yesterday", "today", "tomorrow"};
    for (const auto &word : day_words) {
      if (!lower.starts_with(word)) continue;
      std::string rest = trim_copy(lower.substr(word.size()));
      if (rest.empty()) continue;  // bare day word handled above

      // rest: "HH:MM[:SS]" optionally followed by a UTC-ish zone name.
      std::string time_part = rest;
      bool utc_zone = false;
      if (auto space = rest.find(' '); space != std::string::npos) {
        time_part = trim_copy(rest.substr(0, space));
        std::string zone = trim_copy(rest.substr(space + 1));
        utc_zone =
            zone == "gmt" || zone == "utc" || zone == "ut" || zone == "z";
        if (!utc_zone) return std::nullopt;
      }
      int hour = -1;
      int minute = -1;
      int second = 0;
      {
        const auto colon1 = time_part.find(':');
        if (colon1 == std::string::npos) return std::nullopt;
        const auto colon2 = time_part.find(':', colon1 + 1);
        auto parse_field = [](const std::string &text) -> int {
          if (text.empty() || text.size() > 2) return -1;
          for (const char ch : text) {
            if (!std::isdigit(static_cast<unsigned char>(ch))) return -1;
          }
          return (text[0] - '0') * 10 + (text.size() > 1 ? text[1] - '0' : 0);
        };
        hour = parse_field(time_part.substr(0, colon1));
        if (colon2 == std::string::npos) {
          minute = parse_field(time_part.substr(colon1 + 1));
        } else {
          minute =
              parse_field(time_part.substr(colon1 + 1, colon2 - colon1 - 1));
          second = parse_field(time_part.substr(colon2 + 1));
        }
      }
      if (hour < 0 || minute < 0 || second < 0 || hour > 23 || minute > 59 ||
          second > 59) {
        return std::nullopt;
      }
      long long day_shift = 0;
      if (word == "yesterday")
        day_shift = -86400;
      else if (word == "tomorrow")
        day_shift = 86400;
      const FILETIME shifted = add_seconds(now, day_shift);
      SYSTEMTIME base{};
      if (dbg != nullptr) {
        // The day word reduces first (position 0), then the time token.
        if (word == "today") {
          dbg->items.emplace_back(0, "parsed relative part: today/this/now");
        } else {
          char buf[64]{};
          snprintf(buf, sizeof(buf), "parsed relative part: %+lld day(s)",
                   day_shift / 86400);
          dbg->items.emplace_back(0, buf);
        }
        dbg->rel_day += day_shift / 86400;
        dbg->have_time = true;
        const size_t tpos = lower.find(time_part);
        dbg->items.emplace_back(
            tpos == std::string::npos ? word.size() + 1 : tpos,
            "parsed time part: " + ([&] {
              char t[16]{};
              snprintf(t, sizeof(t), "%02d:%02d:%02d", hour, minute, second);
              return std::string(t);
            })());
        if (utc_zone) {
          dbg->items.emplace_back(value.size(), "parsed zone part: UTC+00");
          dbg->zone_seen = true;
          dbg->zone_seconds = 0;
        }
      }
      if (utc_zone) {
        if (!FileTimeToSystemTime(&shifted, &base)) return std::nullopt;
        base.wHour = static_cast<WORD>(hour);
        base.wMinute = static_cast<WORD>(minute);
        base.wSecond = static_cast<WORD>(second);
        base.wMilliseconds = 0;
        if (dbg != nullptr) dbg->start = base;
        FILETIME out{};
        if (!SystemTimeToFileTime(&base, &out)) return std::nullopt;
        return out;
      }
      auto base_opt = filetime_to_local_st(shifted);
      if (!base_opt) {
        return std::nullopt;
      }
      base = *base_opt;
      base.wHour = static_cast<WORD>(hour);
      base.wMinute = static_cast<WORD>(minute);
      base.wSecond = static_cast<WORD>(second);
      base.wMilliseconds = 0;
      if (dbg != nullptr) dbg->start = base;
      return local_system_time_to_filetime(base);
    }
  }

  // "next monday", "next week", etc. GNU treats next/last week as a day
  // shift, month/year as calendar items, and weekday names as day items
  // that reset the time to midnight (gnulib days_seen path).
  static const std::regex next_last_re(
      R"(^(next|last)\s+(monday|tuesday|wednesday|thursday|friday|saturday|sunday|week|month|year)$)");
  std::smatch nl_match;
  if (std::regex_match(lower, nl_match, next_last_re)) {
    const std::string dir = nl_match[1].str();
    const std::string unit = nl_match[2].str();
    const long long sign = dir == "next" ? 1 : -1;

    if (unit == "week") {
      if (dbg != nullptr) {
        dbg->items.emplace_back(
            0, "parsed relative part: " + std::to_string(sign * 7) + " day(s)");
        dbg->rel_day += sign * 7;
        dbg->start_is_now = true;
      }
      return relative(sign, 604800);
    }
    if (unit == "month" || unit == "year") {
      // Calendar arithmetic like parse-datetime.y's rel.month/rel.year.
      const bool is_year = unit == "year";
      const RelativeItem item{sign, true, is_year ? 12 : 1, 1};
      if (dbg != nullptr) {
        dbg->items.emplace_back(
            0, "parsed relative part: " + std::to_string(sign) +
                   (is_year ? " year(s)" : " month(s)"));
        if (is_year)
          dbg->rel_year += sign;
        else
          dbg->rel_month += sign;
        dbg->start_is_now = true;
      }
      return apply_relative_items(now, {item}, use_utc);
    }

    // Weekday names: GNU shifts to the named day and resets the wall
    // clock to midnight ("warning: using midnight as starting time").
    static const char *day_names[] = {"sunday",    "monday",   "tuesday",
                                      "wednesday", "thursday", "friday",
                                      "saturday"};
    static const char *day_abbr[] = {"Sun", "Mon", "Tue", "Wed",
                                     "Thu", "Fri", "Sat"};
    int target_dow = -1;
    for (int i = 0; i < 7; ++i) {
      if (unit == day_names[i]) target_dow = i;
    }
    auto local_now = use_utc ? filetime_to_system_time(now, true)
                             : filetime_to_local_st(now);
    if (!local_now) return std::nullopt;
    int current_dow = local_now->wDayOfWeek;  // 0=Sunday, 1=Monday, ...
    int days_delta = sign > 0 ? (target_dow - current_dow + 7) % 7
                              : (current_dow - target_dow + 7) % 7;
    if (days_delta == 0) days_delta = 7;
    const FILETIME shifted = add_seconds(now, sign * days_delta * 86400LL);
    auto target = use_utc ? filetime_to_system_time(shifted, true)
                          : filetime_to_local_st(shifted);
    if (!target) return std::nullopt;
    target->wHour = 0;
    target->wMinute = 0;
    target->wSecond = 0;
    target->wMilliseconds = 0;
    if (dbg != nullptr) {
      // gnulib str_days(): "next/first Mon", "last Fri".
      const char *ord = sign > 0 ? "next/first" : "last";
      dbg->items.emplace_back(0, std::string("parsed day part: ") + ord + " " +
                                     day_abbr[target_dow] +
                                     " (day ordinal=" + std::to_string(sign) +
                                     " number=" + std::to_string(target_dow) +
                                     ")");
      dbg->have_day = true;
      dbg->day_label = std::string(ord) + " " + day_abbr[target_dow];
      dbg->start = *target;
    }
    return use_utc ? utc_system_time_to_filetime(*target)
                   : local_system_time_to_filetime(*target);
  }

  // [GNU] military timezone specs (parse-datetime.y military_table):
  // "<HH|HHMM><L>", "<HH>:<MM>[:<SS>]<L>", or a lone "<L>" meaning today at
  // 00:00 in that zone. A-I = UTC+1..+9, K-M = UTC+10..+12 (J is not in the
  // sequence), N-Y = UTC-1..-12, Z = UTC, and J is the LOCAL zone. 'T' also
  // serves as the ISO 8601 separator, but remains UTC-7 in a zone position.
  //
  // J was added upstream in gnulib commit 9cde39f8 (2022-05-17,
  // "parse-datetime: support 'J' military time zone", released in
  // coreutils 9.2), which added
  // `{ "J", 'J', 0 }` to military_table and a matching `item: 'J'` grammar
  // rule. Coreutils 8.32 predates it and rejects J outright, so J is the one
  // military letter whose expected result depends on the oracle version: the
  // differential cases carry an `oracle_version:` precondition for exactly that
  // reason. (uutils #12893, #12895)
  {
    static const std::regex military_hms_re(
        R"(^([0-9]{1,2}):([0-9]{2})(?::([0-9]{2}))?\s*([A-Za-z])$)");
    static const std::regex military_re(R"(^([0-9]{1,4})\s*([A-Za-z])$)");
    static const std::regex military_zone_re(R"(^([A-Za-z])$)");
    std::smatch mil;
    int hour = 0;
    int minute = 0;
    int second = 0;
    char letter = '\0';
    size_t digits_len = 0;
    bool matched = false;
    if (std::regex_match(value, mil, military_hms_re)) {
      hour = std::stoi(mil[1].str());
      minute = std::stoi(mil[2].str());
      if (mil[3].matched) second = std::stoi(mil[3].str());
      letter = static_cast<char>(
          std::tolower(static_cast<unsigned char>(mil[4].str()[0])));
      digits_len = mil[1].str().size() + mil[2].str().size() +
                   (mil[3].matched ? mil[3].str().size() : 0);
      matched = true;
    } else if (std::regex_match(value, mil, military_re)) {
      const std::string digits = mil[1].str();
      digits_len = digits.size();
      if (digits.size() <= 2) {
        hour = std::stoi(digits);
      } else {
        hour = std::stoi(digits.substr(0, digits.size() - 2));
        minute = std::stoi(digits.substr(digits.size() - 2));
      }
      letter = static_cast<char>(
          std::tolower(static_cast<unsigned char>(mil[2].str()[0])));
      matched = true;
    } else if (std::regex_match(value, mil, military_zone_re)) {
      letter = static_cast<char>(
          std::tolower(static_cast<unsigned char>(mil[1].str()[0])));
      matched = true;
    }
    if (matched) {
      bool ok = hour <= 23 && minute <= 59 && second <= 59;
      int zone_offset_minutes = 0;
      bool local_zone = false;
      if (letter == 'j') {
        // [GNU] 'J' is the local zone (parse-datetime.y: {"J", 'J', 0}).
        // Under -u the universal context makes "local" UTC itself, so
        // `date -u -d 1024j` renders 10:24, not the shifted UTC time.
        if (use_utc) {
          local_zone = false;
          zone_offset_minutes = 0;
        } else {
          local_zone = true;
        }
      } else if (letter >= 'a' && letter <= 'i') {
        zone_offset_minutes = (letter - 'a' + 1) * 60;
      } else if (letter >= 'k' && letter <= 'm') {
        // K=+10, L=+11, M=+12 (J is skipped in the military alphabet)
        zone_offset_minutes = (letter - 'a') * 60;
      } else if (letter == 'z') {
        zone_offset_minutes = 0;
      } else if (letter >= 'n' && letter <= 'y') {
        zone_offset_minutes = -(letter - 'n' + 1) * 60;
      } else {
        ok = false;
      }
      if (!ok) return std::nullopt;

      FILETIME now_ft{};
      GetSystemTimeAsFileTime(&now_ft);
      auto local_now_opt = filetime_to_local_st(now_ft);
      if (!local_now_opt) return std::nullopt;
      SYSTEMTIME local_now = *local_now_opt;

      SYSTEMTIME target{};
      target.wYear = local_now.wYear;
      target.wMonth = local_now.wMonth;
      target.wDay = local_now.wDay;
      target.wHour = static_cast<WORD>(hour);
      target.wMinute = static_cast<WORD>(minute);
      target.wSecond = static_cast<WORD>(second);

      auto as_local = local_system_time_to_filetime(target);
      if (!as_local) return std::nullopt;
      // Shift from "wall time as local" to "wall time in the target zone"
      const int local_offset = timezone_offset_minutes(now_ft, false);
      if (local_zone) zone_offset_minutes = local_offset;
      if (dbg != nullptr) {
        char t[16]{};
        snprintf(t, sizeof(t), "%02d:%02d:%02d", hour, minute, second);
        dbg->items.emplace_back(0, std::string("parsed number part: ") + t);
        dbg->items.emplace_back(
            digits_len,
            "parsed zone part: UTC" + debug_zone_str(zone_offset_minutes * 60));
        dbg->zone_seen = true;
        dbg->zone_seconds = zone_offset_minutes * 60;
        dbg->have_time = true;
        dbg->start = target;
      }
      return add_seconds(
          *as_local,
          static_cast<long long>(local_offset - zone_offset_minutes) * 60);
    }
  }

  return parse_fixed_date_time(arg, use_utc, dbg);
}

// gnulib mktime-style normalization for the --debug "new date/time" line:
// add year/month/day aggregates to a wall-clock SYSTEMTIME, rolling day
// overflow/underflow across month boundaries.
auto debug_shift_date(SYSTEMTIME st, long long rel_year, long long rel_month,
                      long long rel_day) -> SYSTEMTIME {
  long long total_months = static_cast<long long>(st.wYear) * 12 +
                           (st.wMonth - 1) + rel_year * 12 + rel_month;
  long long year = total_months / 12;
  long long month = total_months % 12;
  if (month < 0) {
    month += 12;
    --year;
  }
  ++month;
  if (year < 1) year = 1;  // clamp: the real conversion fails anyway
  long long day = static_cast<long long>(st.wDay) + rel_day;
  while (day > days_in_month(static_cast<int>(year), static_cast<int>(month))) {
    day -= days_in_month(static_cast<int>(year), static_cast<int>(month));
    if (++month > 12) {
      month = 1;
      ++year;
    }
  }
  while (day < 1) {
    if (--month < 1) {
      month = 12;
      --year;
    }
    day += days_in_month(static_cast<int>(year), static_cast<int>(month));
  }
  st.wYear = static_cast<WORD>(year);
  st.wMonth = static_cast<WORD>(month);
  st.wDay = static_cast<WORD>(day);
  return st;
}

// Convert a wall-clock SYSTEMTIME in the effective parse zone to FILETIME.
auto debug_st_to_filetime(const SYSTEMTIME &st, const DateDebugInfo &dbg,
                          bool use_utc) -> std::optional<FILETIME> {
  if (dbg.zone_seen) {
    auto ft = utc_system_time_to_filetime(st);
    if (!ft) return std::nullopt;
    return add_seconds(*ft, -static_cast<long long>(dbg.zone_seconds));
  }
  if (use_utc) return utc_system_time_to_filetime(st);
  return local_system_time_to_filetime(st);
}

// [GNU] Replay the parse-datetime.y debug trace for a successfully parsed
// -d/-s string (and the partial "parsed ... part" lines for a failed one).
// Message shapes follow lib/parse-datetime.y's debugging block
// (uutils#7342 / WinuxCmd#239).
auto emit_date_debug(const DateDebugInfo &dbg, const FILETIME &result,
                     bool use_utc, const std::string &format) -> void {
  auto items = dbg.items;
  std::ranges::sort(
      items, [](const auto &a, const auto &b) { return a.first < b.first; });
  for (const auto &[pos, line] : items) {
    safeErrorPrintLn("date: " + line);
  }

  // Effective TZ string like GNU date.c: -u maps to "UTC0".
  const char *tz_env = std::getenv("TZ");
  const std::string tzstring =
      use_utc ? "UTC0" : (tz_env != nullptr ? tz_env : "");

  // input timezone
  std::string input_tz;
  if (dbg.epoch) {
    input_tz = "'@timespec' - always UTC";
  } else if (dbg.zone_seen) {
    input_tz =
        "parsed date/time string (" + debug_zone_str(dbg.zone_seconds) + ")";
  } else if (!tzstring.empty()) {
    input_tz = tzstring == "UTC0" ? "TZ=\"UTC0\" environment value or -u"
                                  : "TZ=\"" + tzstring + "\" environment value";
  } else {
    input_tz = "system default";
  }
  safeErrorPrintLn("date: input timezone: " + input_tz);

  const bool rels_seen = dbg.rel_year != 0 || dbg.rel_month != 0 ||
                         dbg.rel_day != 0 || dbg.rel_hour != 0 ||
                         dbg.rel_minute != 0 || dbg.rel_second != 0;

  if (!dbg.epoch) {
    SYSTEMTIME start = dbg.start;
    if (dbg.start_is_now) {
      FILETIME now_ft{};
      GetSystemTimeAsFileTime(&now_ft);
      auto cur = use_utc ? filetime_to_system_time(now_ft, true)
                         : filetime_to_local_st(now_ft);
      if (cur) start = *cur;
    }

    if (dbg.have_time) {
      safeErrorPrintLn("date: using specified time as starting value: '" +
                       debug_time_str(start) + "'");
    } else if (rels_seen && !dbg.have_date && !dbg.have_day) {
      safeErrorPrintLn("date: using current time as starting value: '" +
                       debug_time_str(start) + "'");
    } else {
      safeErrorPrintLn(
          "date: warning: using midnight as starting time: 00:00:00");
    }
    if (dbg.have_day) {
      safeErrorPrintLn(
          "date: new start date: '" + dbg.day_label + "' is '" +
          debug_datetime_str(start, dbg.zone_seen, dbg.zone_seconds) + "'");
    }
    if (!dbg.have_date && !dbg.have_day) {
      safeErrorPrintLn("date: using current date as starting value: '" +
                       debug_date_str(start) + "'");
    }
    safeErrorPrintLn(
        "date: starting date/time: '" +
        debug_datetime_str(start, dbg.zone_seen, dbg.zone_seconds) + "'");

    SYSTEMTIME adjusted = start;
    if (dbg.rel_year != 0 || dbg.rel_month != 0 || dbg.rel_day != 0) {
      if ((dbg.rel_year != 0 || dbg.rel_month != 0) && start.wDay != 15) {
        safeErrorPrintLn(
            "date: warning: when adding relative months/years, it is "
            "recommended to specify the 15th of the months");
      }
      if (dbg.rel_day != 0 && start.wHour != 12) {
        safeErrorPrintLn(
            "date: warning: when adding relative days, it is recommended "
            "to specify noon");
      }
      adjusted =
          debug_shift_date(start, dbg.rel_year, dbg.rel_month, dbg.rel_day);
      char buf[128]{};
      snprintf(buf, sizeof(buf),
               "after date adjustment (%+lld years, %+lld months, %+lld "
               "days),",
               dbg.rel_year, dbg.rel_month, dbg.rel_day);
      safeErrorPrintLn(std::string("date: ") + buf);
      safeErrorPrintLn(
          "date:     new date/time = '" +
          debug_datetime_str(adjusted, dbg.zone_seen, dbg.zone_seconds) + "'");
    }

    // "'(Y-M-D) ...' = N epoch-seconds": convert the adjusted wall clock
    // through the effective zone.
    if (auto base_ft = debug_st_to_filetime(adjusted, dbg, use_utc)) {
      safeErrorPrintLn(
          "date: '" +
          debug_datetime_str(adjusted, dbg.zone_seen, dbg.zone_seconds) +
          "' = " + std::to_string(epoch_seconds(*base_ft)) + " epoch-seconds");
    }

    if (dbg.rel_hour != 0 || dbg.rel_minute != 0 || dbg.rel_second != 0) {
      char buf[128]{};
      snprintf(buf, sizeof(buf),
               "after time adjustment (%+lld hours, %+lld minutes, %+lld "
               "seconds, +0 ns),",
               dbg.rel_hour, dbg.rel_minute, dbg.rel_second);
      safeErrorPrintLn(std::string("date: ") + buf);
      safeErrorPrintLn(
          "date:     new time = " + std::to_string(epoch_seconds(result)) +
          " epoch-seconds");
    }
  }

  // timezone / final lines
  if (tzstring.empty()) {
    safeErrorPrintLn("date: timezone: system default");
  } else if (tzstring == "UTC0") {
    safeErrorPrintLn("date: timezone: Universal Time");
  } else {
    safeErrorPrintLn("date: timezone: TZ=\"" + tzstring +
                     "\" environment value");
  }
  const auto ticks = static_cast<long long>(filetime_to_ticks(result));
  const long long secs = epoch_seconds(result);
  char nbuf[32]{};
  snprintf(nbuf, sizeof(nbuf), "%09lld", (ticks % 10000000LL) * 100);
  safeErrorPrintLn("date: final: " + std::to_string(secs) + "." + nbuf +
                   " (epoch-seconds)");
  if (auto utc_st = filetime_to_system_time(result, true)) {
    safeErrorPrintLn("date: final: " + debug_datetime_str(*utc_st, false, 0) +
                     " (UTC)");
  }
  std::optional<SYSTEMTIME> local_st;
  int local_off = 0;
  if (use_utc) {
    local_st = filetime_to_system_time(result, true);
  } else {
    local_st = filetime_to_local_st(result);
    local_off = timezone_offset_minutes(result, false);
  }
  if (local_st) {
    safeErrorPrintLn("date: final: " + debug_datetime_str(*local_st, false, 0) +
                     " (UTC" + debug_zone_str(local_off * 60) + ")");
  }

  // [GNU] The debug dump ends with the effective output format string.
  safeErrorPrintLn("date: output format: '" + format + "'");
}

auto normalize_timespec(std::string spec, bool default_date) -> std::string {
  spec = lower_copy(trim_copy(spec));
  if (spec.empty()) return default_date ? "date" : "seconds";
  return spec;
}

auto iso_format_for(std::string spec) -> std::optional<std::string> {
  spec = normalize_timespec(std::move(spec), true);
  if (spec == "date") return "%Y-%m-%d";
  if (spec == "hours") return "%Y-%m-%dT%H%:z";
  if (spec == "minutes") return "%Y-%m-%dT%H:%M%:z";
  if (spec == "seconds") return "%Y-%m-%dT%H:%M:%S%:z";
  // [GNU] -Ins uses the ISO 8601 decimal comma before nanoseconds
  // ("2026-09-14T08:27:11,355532700+00:00"), unlike --rfc-3339=ns which
  // keeps a dot (uutils#6387 / WinuxCmd#219).
  if (spec == "ns" || spec == "nanoseconds") return "%Y-%m-%dT%H:%M:%S,%N%:z";
  return std::nullopt;
}

auto rfc3339_format_for(std::string spec) -> std::optional<std::string> {
  spec = normalize_timespec(std::move(spec), false);
  if (spec == "date") return "%Y-%m-%d";
  if (spec == "seconds") return "%Y-%m-%d %H:%M:%S%:z";
  if (spec == "ns" || spec == "nanoseconds") return "%Y-%m-%d %H:%M:%S.%N%:z";
  return std::nullopt;
}

// [GNU] posixtime: parse the POSIX MMDDhhmm[[CC]YY][.ss] set-date syntax
// (coreutils posixtm.c). Only 8, 10, or 12 digits are accepted; a trailing
// ".ss" appends seconds. With 8 digits the current year is used, with 10 a
// 2-digit year maps into the 1969-2068 window, and with 12 a 4-digit year
// must lie in the 20th or 21st century.
auto parse_posix_clock(std::string_view arg) -> std::optional<SYSTEMTIME> {
  std::string digits(arg);
  int sec = 0;
  if (auto dot = digits.find('.'); dot != std::string::npos) {
    std::string ss = digits.substr(dot + 1);
    if (ss.size() != 2 || !std::ranges::all_of(ss, [](unsigned char ch) {
          return std::isdigit(ch);
        }))
      return std::nullopt;
    sec = (ss[0] - '0') * 10 + (ss[1] - '0');
    if (sec > 60) return std::nullopt;
    digits.resize(dot);
  }
  if (digits.size() != 8 && digits.size() != 10 && digits.size() != 12)
    return std::nullopt;
  if (!std::ranges::all_of(digits,
                           [](unsigned char ch) { return std::isdigit(ch); }))
    return std::nullopt;

  int mon = (digits[0] - '0') * 10 + (digits[1] - '0');
  int day = (digits[2] - '0') * 10 + (digits[3] - '0');
  int hour = (digits[4] - '0') * 10 + (digits[5] - '0');
  int minute = (digits[6] - '0') * 10 + (digits[7] - '0');
  int year;
  if (digits.size() == 8) {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    year = now.wYear;
  } else if (digits.size() == 10) {
    int two = (digits[8] - '0') * 10 + (digits[9] - '0');
    year = two < 69 ? 2000 + two : 1900 + two;
  } else {
    year = (digits[8] - '0') * 1000 + (digits[9] - '0') * 100 +
           (digits[10] - '0') * 10 + (digits[11] - '0');
    int century = year / 100;
    if (century != 19 && century != 20) return std::nullopt;
  }
  if (mon < 1 || mon > 12 || day < 1 || day > 31 || hour > 23 || minute > 59)
    return std::nullopt;
  SYSTEMTIME st{static_cast<WORD>(year),
                static_cast<WORD>(mon),
                0,
                static_cast<WORD>(day),
                static_cast<WORD>(hour),
                static_cast<WORD>(minute),
                static_cast<WORD>(sec),
                0};
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
    "  %F   Full date; same as %Y-%m-%d\n"
    "  %T   Time; same as %H:%M:%S\n"
    "  %z   Numeric time zone offset\n"
    "  %p   AM/PM\n"
    "  %a   Abbreviated weekday name\n"
    "  %A   Full weekday name\n"
    "  %b   Abbreviated month name\n"
    "  %B   Full month name",
    "  date                    Display current date and time\n"
    "  date +'%Y-%m-%d'        Display date in YYYY-MM-DD format\n"
    "  date +'%H:%M:%S'        Display time in HH:MM:SS format\n"
    "  date -u                 Display UTC time\n"
    "  date -R                 Display RFC email format\n"
    "  date -r FILE            Display FILE modification time\n"
    "  date -Iseconds          Display ISO 8601 format",
    "cal(1)", "caomengxuan666", "Copyright © 2026 WinuxCmd", DATE_OPTIONS) {
  using namespace date_pipeline;

  bool is_set = ctx.has("--set") || ctx.has("-s");
  std::string set_arg;
  if (is_set) {
    set_arg = ctx.get<std::string>("--set", "");
    if (set_arg.empty()) set_arg = ctx.get<std::string>("-s", "");
  }

  // [GNU] -d/-f/-r/--resolution are mutually exclusive date sources
  std::string date_arg = ctx.get<std::string>("--date", "");
  if (date_arg.empty()) date_arg = ctx.get<std::string>("-d", "");
  std::string date_file = ctx.get<std::string>("--file", "");
  if (date_file.empty()) date_file = ctx.get<std::string>("-f", "");
  std::string reference_path = ctx.get<std::string>("--reference", "");
  if (reference_path.empty()) reference_path = ctx.get<std::string>("-r", "");
  bool has_resolution = ctx.has("--resolution");

  int specified_date =
      (!date_arg.empty() ? 1 : 0) + (!date_file.empty() ? 1 : 0) +
      (!reference_path.empty() ? 1 : 0) + (has_resolution ? 1 : 0);
  if (specified_date > 1) {
    safeErrorPrintLn(
        "date: the options to specify dates for printing are mutually "
        "exclusive");
    return 1;
  }
  // [GNU] --set may not be combined with any printing date source
  if (is_set && specified_date) {
    safeErrorPrintLn(
        "date: the options to print and set the time may not be used together");
    return 1;
  }

  // [GNU] --universal/--uct: aliases for --utc
  bool use_utc = ctx.get<bool>("-u", false) || ctx.get<bool>("--utc", false) ||
                 ctx.get<bool>("--universal", false) ||
                 ctx.get<bool>("--uct", false);
  bool rfc2822 =
      ctx.get<bool>("-R", false) || ctx.get<bool>("--rfc-email", false) ||
      ctx.get<bool>("--rfc-2822", false) || ctx.get<bool>("--rfc-822", false);

  // [GNU] -R/-I/--rfc-3339 all set the output format; a '+' operand may not
  // override them ("multiple output formats specified").
  std::string format;
  std::string iso_arg = ctx.get<std::string>("--iso-8601", "");
  if (!ctx.has("--iso-8601")) iso_arg = ctx.get<std::string>("-I", "");
  if (ctx.has("--iso-8601") || ctx.has("-I")) {
    auto iso_format = iso_format_for(iso_arg);
    if (!iso_format) {
      safeErrorPrint("date: invalid argument '");
      safeErrorPrint(iso_arg);
      safeErrorPrint("' for '--iso-8601'\n");
      // [GNU] list the accepted timespecs plus the help hint.
      safeErrorPrintLn("Valid arguments are:");
      safeErrorPrintLn("  - 'hours'");
      safeErrorPrintLn("  - 'minutes'");
      safeErrorPrintLn("  - 'date'");
      safeErrorPrintLn("  - 'seconds'");
      safeErrorPrintLn("  - 'ns'");
      safeErrorPrintLn("Try 'date --help' for more information.");
      return 1;
    }
    format = *iso_format;
  }
  std::string rfc3339_arg = ctx.get<std::string>("--rfc-3339", "");
  if (!rfc3339_arg.empty()) {
    auto rfc3339_format = rfc3339_format_for(rfc3339_arg);
    if (!rfc3339_format) {
      safeErrorPrint("date: invalid argument '");
      safeErrorPrint(rfc3339_arg);
      safeErrorPrint("' for '--rfc-3339'\n");
      safeErrorPrintLn("Valid arguments are:");
      safeErrorPrintLn("  - 'date'");
      safeErrorPrintLn("  - 'seconds'");
      safeErrorPrintLn("  - 'ns'");
      safeErrorPrintLn("Try 'date --help' for more information.");
      return 1;
    }
    format = *rfc3339_format;
  }
  if (rfc2822) format = "%a, %d %b %Y %H:%M:%S %z";

  // [GNU] positional operand handling
  std::string posix_date_operand;
  if (!ctx.positionals.empty()) {
    if (ctx.positionals.size() > 1) {
      safeErrorPrint("date: extra operand '");
      safeErrorPrint(std::string(ctx.positionals[1]));
      safeErrorPrintLn("'");
      return 1;
    }
    std::string operand{ctx.positionals[0]};
    if (!operand.empty() && operand[0] == '+') {
      if (!format.empty()) {
        safeErrorPrintLn("date: multiple output formats specified");
        return 1;
      }
      format = operand.substr(1);
    } else if (specified_date || is_set) {
      safeErrorPrint("date: the argument '");
      safeErrorPrint(operand);
      safeErrorPrintLn("' lacks a leading '+';");
      safeErrorPrintLn(
          "when using an option to specify date(s), any non-option");
      safeErrorPrintLn("argument must be a format string beginning with '+'");
      return 1;
    } else {
      posix_date_operand = operand;
    }
  }

  if (format.empty()) {
    // [GNU] --resolution defaults to "%s.%N"
    format = has_resolution ? "%s.%N" : "%a %b %e %H:%M:%S %Z %Y";
  }

  const bool debug = ctx.has("--debug");

  // [GNU] On a failed parse, --debug still prints the "parsed ... part"
  // lines for whatever was recognized before the error.
  auto emit_partial_debug = [](const DateDebugInfo &dbg) {
    auto items = dbg.items;
    std::ranges::sort(
        items, [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &[pos, line] : items) {
      safeErrorPrintLn("date: " + line);
    }
  };

  if (is_set) {
    DateDebugInfo dbg;
    auto parsed = parse_date_argument(set_arg, use_utc, debug ? &dbg : nullptr);
    if (!parsed) {
      if (debug) emit_partial_debug(dbg);
      safeErrorPrint("date: invalid date '");
      safeErrorPrint(set_arg);
      safeErrorPrintLn("'");
      return 1;
    }
    if (debug) emit_date_debug(dbg, *parsed, use_utc, format);

    // SetSystemTime expects a UTC SYSTEMTIME; the parsed value is already
    // an absolute UTC FILETIME, so no local-time conversion is needed here.
    SYSTEMTIME utc_st{};
    if (!FileTimeToSystemTime(&*parsed, &utc_st)) {
      safeErrorPrintLn("date: cannot convert time");
      return 1;
    }
    // [GNU] regardless of the outcome, the (attempted) new date is printed
    int exit_code = 0;
    if (!SetSystemTime(&utc_st)) {
      safeErrorPrintLn("date: cannot set date");
      exit_code = 1;
    }
    auto tv = make_time_value(*parsed, use_utc);
    if (tv) {
      safePrintLn(format_time(*tv, format));
    }
    return exit_code;
  }

  FILETIME selected_time{};
  if (!reference_path.empty()) {
    auto reference_time = read_reference_time(reference_path);
    if (!reference_time) {
      safeErrorPrint("date: failed to get modification time of '");
      safeErrorPrint(reference_path);
      safeErrorPrint("'\n");
      return 1;
    }
    selected_time = *reference_time;
  } else if (!date_arg.empty()) {
    DateDebugInfo dbg;
    auto parsed =
        parse_date_argument(date_arg, use_utc, debug ? &dbg : nullptr);
    if (!parsed) {
      if (debug) emit_partial_debug(dbg);
      safeErrorPrint("date: invalid date '");
      safeErrorPrint(date_arg);
      safeErrorPrint("'\n");
      return 1;
    }
    selected_time = *parsed;
    if (debug) emit_date_debug(dbg, *parsed, use_utc, format);
  } else if (has_resolution) {
    // [GNU] date --resolution prints the available timestamp resolution;
    // FILETIME ticks are 100 ns wide.
    safePrintLn("0.000000100");
    return 0;
  } else if (posix_date_operand.empty()) {
    GetSystemTimeAsFileTime(&selected_time);
  }

  // [GNU] a bare positional date argument means: set the system clock to the
  // POSIX-format date/time MMDDhhmm[[CC]YY][.ss]
  if (!posix_date_operand.empty()) {
    auto parsed = parse_posix_clock(posix_date_operand);
    if (!parsed) {
      safeErrorPrint("date: invalid date '");
      safeErrorPrint(posix_date_operand);
      safeErrorPrintLn("'");
      return 1;
    }
    // POSIX set-clock operands are local wall-clock time; GNU interprets
    // them as UTC when -u is active.
    int exit_code = 0;
    FILETIME utc_ft{};
    if (use_utc) {
      if (!SystemTimeToFileTime(&*parsed, &utc_ft)) {
        safeErrorPrintLn("date: cannot set date");
        return 1;
      }
      SYSTEMTIME utc_st{};
      if (!FileTimeToSystemTime(&utc_ft, &utc_st)) {
        safeErrorPrintLn("date: cannot set date");
        return 1;
      }
      if (!SetSystemTime(&utc_st)) {
        safeErrorPrintLn("date: cannot set date");
        exit_code = 1;
      }
    } else {
      // SetLocalTime takes local wall-clock time (SetSystemTime would take
      // UTC); rebuild the UTC FILETIME for display afterwards.
      if (!SetLocalTime(&*parsed)) {
        safeErrorPrintLn("date: cannot set date");
        exit_code = 1;
      }
      SYSTEMTIME utc_st{};
      auto back_ft = local_st_to_filetime(*parsed);
      if (back_ft) {
        utc_ft = *back_ft;
      }
    }
    // Print the (attempted) new date like GNU does.
    if (debug) {
      safeErrorPrintLn("date: output format: '" + format + "'");
    }
    auto tv = make_time_value(utc_ft, use_utc);
    if (tv) safePrintLn(format_time(*tv, format));
    return exit_code;
  }

  auto tv = make_time_value(selected_time, use_utc);
  if (!tv) {
    safeErrorPrint("date: failed to convert time\n");
    return 1;
  }

  // [GNU] -f/--file: display date strings from DATEFILE, one per line
  if (!date_file.empty()) {
    // [GNU] "-" reads date strings from standard input.
    const bool from_stdin = date_file == "-";
    std::ifstream file_stream;
    if (!from_stdin) {
      // [GNU/Linux] fopen on a directory succeeds but the first read fails
      // with EISDIR, so GNU reports "<file>: read error: Is a directory";
      // every other open failure reports strerror(errno) after the file.
      std::error_code ec;
      const bool is_dir = std::filesystem::is_directory(date_file, ec);
      file_stream.open(date_file, std::ios::binary);
      if (!file_stream) {
        if (is_dir) {
          safeErrorPrintLn(winux::i18n::format(
              "command.date.error.read_error",
              "date: {}: read error: Is a directory", date_file));
        } else {
          const int open_errno = errno;
          const char *reason = open_errno != 0 ? std::strerror(open_errno)
                                               : "No such file or directory";
          // The common ENOENT case gets a dedicated key so translators can
          // render the whole message; anything else keeps the raw strerror.
          if (std::strcmp(reason, "No such file or directory") == 0) {
            safeErrorPrintLn(winux::i18n::format(
                "command.date.error.cannot_open",
                "date: {}: No such file or directory", date_file));
          } else {
            safeErrorPrintLn(
                winux::i18n::format("command.date.error.cannot_open_generic",
                                    "date: {}: {}", date_file, reason));
          }
        }
        return 1;
      }
    }
    std::istream &in = from_stdin ? static_cast<std::istream &>(std::cin)
                                  : static_cast<std::istream &>(file_stream);
    std::string line;
    bool ok = true;
    while (std::getline(in, line)) {
      DateDebugInfo line_dbg;
      auto parsed =
          parse_date_argument(line, use_utc, debug ? &line_dbg : nullptr);
      if (!parsed) {
        // [GNU] batch mode reports each invalid line and keeps going; the
        // exit status is 1 after every line has been processed.
        if (debug) emit_partial_debug(line_dbg);
        safeErrorPrint("date: invalid date '");
        safeErrorPrint(line);
        safeErrorPrintLn("'");
        ok = false;
        continue;
      }
      auto file_tv = make_time_value(*parsed, use_utc);
      if (!file_tv) {
        safeErrorPrintLn("date: failed to convert time");
        ok = false;
        continue;
      }
      if (debug) emit_date_debug(line_dbg, *parsed, use_utc, format);
      if (debug) {
        safeErrorPrintLn("date: output format: '" + format + "'");
      }
      safePrint(format_time(*file_tv, format));
      safePrint("\n");
    }
    return ok ? 0 : 1;
  }

  // [GNU] --debug annotates the output format even for non-parsed sources
  // (-r FILE, "now", --resolution).
  if (debug) {
    safeErrorPrintLn("date: output format: '" + format + "'");
  }
  std::string output = format_time(*tv, format);
  safePrintLn(output);

  return 0;
}
