/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: touch.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

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
 * [NOT SUPPORT]
 * - @a -m: Change only the modification time [IMPLEMENTED]
 * - @a -r, @a --reference: Use this file's times instead of current time
 * [IMPLEMENTED]
 * - @a -t: Use [[CC]YY]MMDDhhmm[.ss] instead of current time [IMPLEMENTED]
 * - @a --time: Change the specified time (access/atime/use/modify/mtime)
 * [IMPLEMENTED]
 */
auto constexpr TOUCH_OPTIONS = std::array{
    OPTION("-a", "", "change only the access time"),
    OPTION("-c", "--no-create", "do not create any files"),
    OPTION("-d", "--date", "parse STRING and use it instead of current time",
           STRING_TYPE),
    OPTION("-f", "", "(ignored)"),
    OPTION("-h", "--no-dereference",
           "affect symbolic link instead of referenced file [NOT SUPPORT]"),
    OPTION("-m", "", "change only the modification time"),
    OPTION("-r", "--reference", "use this file's times instead of current time",
           STRING_TYPE),
    OPTION("-t", "", "use [[CC]YY]MMDDhhmm[.ss] instead of current time",
           STRING_TYPE),
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
  } else {
    return std::nullopt;
  }

  int hour = 0;
  int minute = 0;
  int second = 0;
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
    auto h = parse_int(pieces[0]);
    auto m = parse_int(pieces[1]);
    auto sec =
        pieces.size() == 3 ? parse_int(pieces[2]) : std::optional<int>{0};
    if (!h || !m || !sec) return std::nullopt;
    hour = *h;
    minute = *m;
    second = *sec;
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
  return local_system_time_to_filetime(st);
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
  } else {
    return std::nullopt;
  }

  return add_seconds(base, static_cast<long long>(amount) * scale);
}

auto parse_gnu_date_string(const std::string& input, const FILETIME& base)
    -> std::optional<FILETIME> {
  if (auto fixed = parse_fixed_date_time(input)) return fixed;
  return parse_relative_date(input, base);
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
  if (!seconds_part.empty() &&
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

  SYSTEMTIME st{};
  st.wYear = static_cast<WORD>(year);
  st.wMonth = static_cast<WORD>(month);
  st.wDay = static_cast<WORD>(day);
  st.wHour = static_cast<WORD>(hour);
  st.wMinute = static_cast<WORD>(minute);
  st.wSecond = static_cast<WORD>(second);
  return local_system_time_to_filetime(st);
}

auto read_times_from_file(const std::wstring& wpath)
    -> std::optional<TimePair> {
  HANDLE h =
      CreateFileW(wpath.c_str(), FILE_READ_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (h == INVALID_HANDLE_VALUE) return std::nullopt;

  FILETIME c{}, a{}, m{};
  bool ok = GetFileTime(h, &c, &a, &m) != 0;
  CloseHandle(h);
  if (!ok) return std::nullopt;

  return TimePair{a, m};
}

auto apply_touch_one(const std::string& path,
                     const CommandContext<TOUCH_OPTIONS.size()>& ctx,
                     bool update_access, bool update_modify, bool no_create,
                     const std::optional<TimePair>& ref_times,
                     const std::optional<TimePair>& date_times) -> bool {
  std::wstring wpath = utf8_to_wstring(path);

  DWORD create_mode = no_create ? OPEN_EXISTING : OPEN_ALWAYS;
  HANDLE h =
      CreateFileW(wpath.c_str(), FILE_READ_ATTRIBUTES | FILE_WRITE_ATTRIBUTES,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, create_mode, FILE_ATTRIBUTE_NORMAL, nullptr);

  if (h == INVALID_HANDLE_VALUE) {
    DWORD e = GetLastError();
    if (no_create && (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND)) {
      return true;
    }
    safeErrorPrint("touch: cannot touch '");
    safeErrorPrint(path);
    safeErrorPrint("': No such file or directory\n");
    return false;
  }

  FILETIME c{}, cur_a{}, cur_m{};
  if (!GetFileTime(h, &c, &cur_a, &cur_m)) {
    CloseHandle(h);
    safeErrorPrint("touch: cannot touch '");
    safeErrorPrint(path);
    safeErrorPrint("'\n");
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

  bool ok = SetFileTime(h, nullptr, pa, pm) != 0;
  CloseHandle(h);

  if (!ok) {
    safeErrorPrint("touch: cannot touch '");
    safeErrorPrint(path);
    safeErrorPrint("'\n");
    return false;
  }

  return true;
}

auto process_command(const CommandContext<TOUCH_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  if (ctx.positionals.empty()) return std::unexpected("missing file operand");

  bool flag_a = ctx.get<bool>("-a", false);
  bool flag_m = ctx.get<bool>("-m", false);

  std::string time_word = ctx.get<std::string>("--time", "");
  if (!time_word.empty()) {
    if (time_word == "access" || time_word == "atime" || time_word == "use") {
      flag_a = true;
      flag_m = false;
    } else if (time_word == "modify" || time_word == "mtime") {
      flag_a = false;
      flag_m = true;
    } else {
      safeErrorPrint("touch: invalid argument '");
      safeErrorPrint(time_word);
      safeErrorPrint("' for '--time'\n");
      return std::unexpected("invalid time argument");
    }
  }

  bool update_access = flag_a;
  bool update_modify = flag_m;
  if (!flag_a && !flag_m) {
    update_access = true;
    update_modify = true;
  }

  bool no_create =
      ctx.get<bool>("--no-create", false) || ctx.get<bool>("-c", false);

  std::optional<TimePair> ref_times = std::nullopt;
  std::string ref_path = ctx.get<std::string>("--reference", "");
  if (ref_path.empty()) ref_path = ctx.get<std::string>("-r", "");
  if (!ref_path.empty()) {
    ref_times = read_times_from_file(utf8_to_wstring(ref_path));
    if (!ref_times.has_value()) {
      safeErrorPrint("touch: failed to get attributes of '");
      safeErrorPrint(ref_path);
      safeErrorPrint("'\n");
      return std::unexpected("reference file error");
    }
  }

  std::optional<TimePair> date_times = std::nullopt;
  FILETIME base_time{};
  if (ref_times.has_value()) {
    base_time = ref_times->mtime;
  } else {
    GetSystemTimeAsFileTime(&base_time);
  }

  for (const auto& occurrence :
       ctx.string_occurrences({"--date", "-d", "-t"})) {
    std::optional<FILETIME> ft;
    if (occurrence.short_name == "-t") {
      ft = parse_touch_timestamp(occurrence.value);
    } else {
      ft = parse_gnu_date_string(occurrence.value, base_time);
    }
    if (!ft.has_value()) {
      safeErrorPrint("touch: invalid date format '");
      safeErrorPrint(occurrence.value);
      safeErrorPrint("'\n");
      return std::unexpected("invalid date format");
    }
    date_times = TimePair{*ft, *ft};
    base_time = *ft;
  }

  (void)ctx.get<bool>("--no-dereference", false);
  (void)ctx.get<bool>("-h", false);
  (void)ctx.get<bool>("-f", false);

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
      if (!apply_touch_one(f, ctx, update_access, update_modify, no_create,
                           ref_times, date_times)) {
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
                 "-c is supplied.",
                 "  touch file.txt\n"
                 "  touch -a file.txt\n"
                 "  touch -m file.txt\n"
                 "  touch -c missing.txt\n"
                 "  touch -r ref.txt target.txt",
                 "stat(1), date(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 TOUCH_OPTIONS) {
  using namespace touch_pipeline;
  auto result = process_command(ctx);
  if (!result) {
    cp::report_error(result, L"touch");
    return 1;
  }
  return *result ? 0 : 1;
}
