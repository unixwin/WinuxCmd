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
 * - @a -d, @a --date: Parse STRING and use it instead of current time [NOT
 * SUPPORT]
 * - @a -f: (ignored) [IMPLEMENTED]
 * - @a -h, @a --no-dereference: Affect symbolic link instead of referenced file
 * [NOT SUPPORT]
 * - @a -m: Change only the modification time [IMPLEMENTED]
 * - @a -r, @a --reference: Use this file's times instead of current time
 * [IMPLEMENTED]
 * - @a -t: Use [[CC]YY]MMDDhhmm[.ss] instead of current time [NOT SUPPORT]
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

/**
 * @brief Parse date string in format [[CC]YY]MMDDhhmm[.ss]
 * @param date_str Date string to parse
 * @return FILETIME or nullopt if parsing fails
 */
auto parse_date_string(const std::string& date_str) -> std::optional<FILETIME> {
  std::string s = date_str;

  // Remove any leading/trailing whitespace
  size_t start = s.find_first_not_of(" \t");
  size_t end = s.find_last_not_of(" \t");
  if (start != std::string::npos && end != std::string::npos) {
    s = s.substr(start, end - start + 1);
  } else {
    return std::nullopt;
  }

  // Parse format: [[CC]YY]MMDDhhmm[.ss]
  // Minimum length: MMDDhhmm = 8 characters
  // Maximum length: CCYYMMDDhhmm.ss = 15 characters
  if (s.length() < 8 || s.length() > 15) {
    return std::nullopt;
  }

  // Handle optional century (CC) and year (YY)
  size_t pos = 0;
  int year = 0;

  if (s.length() >= 12) {
    // Format includes century and year: CCYYMMDDhhmm
    if (s.length() >= 12) {
      std::string ccyy = s.substr(0, 4);
      if (!std::all_of(ccyy.begin(), ccyy.end(), ::isdigit)) {
        return std::nullopt;
      }
      year = std::stoi(ccyy);
      pos = 4;
    }
  } else {
    // Format includes only year: YYMMDDhhmm
    std::string yy = s.substr(0, 2);
    if (!std::all_of(yy.begin(), yy.end(), ::isdigit)) {
      return std::nullopt;
    }
    int yy_val = std::stoi(yy);
    // Assume 1970-2069 range
    year = (yy_val >= 70) ? 1900 + yy_val : 2000 + yy_val;
    pos = 2;
  }

  // Parse month (MM)
  if (pos + 2 > s.length()) return std::nullopt;
  std::string mm = s.substr(pos, 2);
  if (!std::all_of(mm.begin(), mm.end(), ::isdigit)) return std::nullopt;
  int month = std::stoi(mm);
  pos += 2;

  // Parse day (DD)
  if (pos + 2 > s.length()) return std::nullopt;
  std::string dd = s.substr(pos, 2);
  if (!std::all_of(dd.begin(), dd.end(), ::isdigit)) return std::nullopt;
  int day = std::stoi(dd);
  pos += 2;

  // Parse hour (hh)
  if (pos + 2 > s.length()) return std::nullopt;
  std::string hh = s.substr(pos, 2);
  if (!std::all_of(hh.begin(), hh.end(), ::isdigit)) return std::nullopt;
  int hour = std::stoi(hh);
  pos += 2;

  // Parse minute (mm)
  if (pos + 2 > s.length()) return std::nullopt;
  std::string min = s.substr(pos, 2);
  if (!std::all_of(min.begin(), min.end(), ::isdigit)) return std::nullopt;
  int minute = std::stoi(min);
  pos += 2;

  // Parse optional second (.ss)
  int second = 0;
  if (pos < s.length() && s[pos] == '.') {
    pos++;
    if (pos + 2 > s.length()) return std::nullopt;
    std::string ss = s.substr(pos, 2);
    if (!std::all_of(ss.begin(), ss.end(), ::isdigit)) return std::nullopt;
    second = std::stoi(ss);
    pos += 2;
  }

  // Validate ranges
  if (month < 1 || month > 12) return std::nullopt;
  if (day < 1 || day > 31) return std::nullopt;
  if (hour < 0 || hour > 23) return std::nullopt;
  if (minute < 0 || minute > 59) return std::nullopt;
  if (second < 0 || second > 59) return std::nullopt;

  // Create local SYSTEMTIME (input is interpreted as local time)
  SYSTEMTIME st_local{};
  st_local.wYear = year;
  st_local.wMonth = month;
  st_local.wDay = day;
  st_local.wHour = hour;
  st_local.wMinute = minute;
  st_local.wSecond = second;
  st_local.wMilliseconds = 0;

  // Convert local time to UTC using time zone API
  TIME_ZONE_INFORMATION tzi{};
  GetTimeZoneInformation(&tzi);

  SYSTEMTIME st_utc{};
  if (!TzSpecificLocalTimeToSystemTime(&tzi, &st_local, &st_utc)) {
    return std::nullopt;
  }

  // Convert UTC SYSTEMTIME to FILETIME
  FILETIME ft{};
  if (!SystemTimeToFileTime(&st_utc, &ft)) {
    return std::nullopt;
  }

  return ft;
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

  // Parse --date or -t option
  std::optional<TimePair> date_times = std::nullopt;
  std::string date_str = ctx.get<std::string>("--date", "");
  if (date_str.empty()) date_str = ctx.get<std::string>("-t", "");
  if (!date_str.empty()) {
    auto ft = parse_date_string(date_str);
    if (!ft.has_value()) {
      safeErrorPrint("touch: invalid date format '");
      safeErrorPrint(date_str);
      safeErrorPrint("'\n");
      return std::unexpected("invalid date format");
    }
    date_times = TimePair{*ft, *ft};
  }

  (void)ctx.get<bool>("--no-dereference", false);
  (void)ctx.get<bool>("-h", false);
  (void)ctx.get<bool>("-f", false);

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
