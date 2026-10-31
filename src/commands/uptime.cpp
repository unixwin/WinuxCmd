// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for uptime.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <wtsapi32.h>  // For WTSEnumerateSessionsW, WTSQuerySessionInformationW

#include "core/command_macros.h"

#pragma comment(lib, "wtsapi32.lib")

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr UPTIME_OPTIONS = std::array{
    // [EXT] option
    OPTION("-p", "--pretty", "show uptime in pretty format", BOOL_TYPE),
    // [EXT] option
    OPTION("-s", "--since", "system up since", BOOL_TYPE)};

namespace uptime_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool pretty = false;
  bool since = false;
};

auto build_config(const CommandContext<UPTIME_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.pretty = ctx.get<bool>("--pretty", false) || ctx.get<bool>("-p", false);
  cfg.since = ctx.get<bool>("--since", false) || ctx.get<bool>("-s", false);
  return cfg;
}

// [GNU] procps uptime -p (library/uptime.c procps_uptime_sprint_short):
// the duration is decomposed into decades/years/weeks/days/hours/minutes,
// each larger unit taken only while the remainder strictly exceeds it,
// nonzero components joined with ", ", and a minutes component is always
// shown when nothing larger remains (so 30s renders "up 0 minutes").
auto format_uptime(std::chrono::seconds uptime) -> std::string {
  long long rem = static_cast<long long>(uptime.count());
  const long long original = rem;

  auto take = [&rem](long long unit) -> long long {
    if (rem > unit) {
      const long long value = rem / unit;
      rem -= value * unit;
      return value;
    }
    return 0;
  };

  const long long decades = take(60LL * 60 * 24 * 365 * 10);
  const long long years = take(60LL * 60 * 24 * 365);
  const long long weeks = take(60LL * 60 * 24 * 7);
  const long long days = take(60LL * 60 * 24);
  const long long hours = take(60LL * 60);
  const long long minutes = take(60LL);

  std::string result = "up ";
  bool first = true;
  auto append = [&result, &first](long long value, const char* singular,
                                  const char* plural) {
    if (value == 0) return;
    if (!first) result += ", ";
    result += std::to_string(value);
    result += " ";
    result += (value > 1 ? plural : singular);
    first = false;
  };
  append(decades, "decade", "decades");
  append(years, "year", "years");
  append(weeks, "week", "weeks");
  append(days, "day", "days");
  append(hours, "hour", "hours");
  if (minutes > 0 || original < 60) {
    if (!first) result += ", ";
    result += std::to_string(minutes);
    result += (minutes != 1 ? " minutes" : " minute");
  }

  return result;
}

// [GNU] procps count_users(): one per live login session. On Windows the
// session table replaces utmp: count active sessions that carry a user.
auto count_logged_in_users() -> int {
  PWTS_SESSION_INFOW sessions = nullptr;
  DWORD session_count = 0;
  if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions,
                             &session_count)) {
    return 1;
  }

  int users = 0;
  for (DWORD i = 0; i < session_count; ++i) {
    if (sessions[i].State != WTSActive) continue;
    LPWSTR buffer = nullptr;
    DWORD buffer_size = 0;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
                                    sessions[i].SessionId, WTSUserName,
                                    &buffer, &buffer_size) &&
        buffer != nullptr && buffer_size > sizeof(wchar_t) &&
        buffer[0] != L'\0') {
      ++users;
    }
    if (buffer) WTSFreeMemory(buffer);
  }
  WTSFreeMemory(sessions);
  return users > 0 ? users : 1;
}

// [GNU] the default uptime line (procps library/uptime.c
// procps_uptime_sprint): " HH:MM:SS up <duration>, <N> user(s),
// load average: ..." where the duration is "D day(s), H:MM" when days
// remain, "H:MM" when hours remain, and "M min" under one hour.
auto format_default_line(std::chrono::seconds uptime, int users,
                         const std::string& now_text) -> std::string {
  const long long total = static_cast<long long>(uptime.count());
  const long long days = total / (60 * 60 * 24);
  const long long hours = (total / (60 * 60)) % 24;
  const long long minutes = (total / 60) % 60;

  std::string line = " " + now_text + " up ";
  char buf[96];
  if (days > 0) {
    snprintf(buf, sizeof(buf), "%lld %s, ", days, days > 1 ? "days" : "day");
    line += buf;
  }
  if (hours > 0) {
    snprintf(buf, sizeof(buf), "%2lld:%02lld, ", hours, minutes);
    line += buf;
  } else {
    // procps takes this branch whenever no whole hours remain, even
    // with days present ("2 days, 30 min, ...").
    snprintf(buf, sizeof(buf), "%lld min, ", minutes);
    line += buf;
  }
  // [DIFFERS] Windows exposes no load average; the figures are reported
  // as N/A rather than fabricated (simulation-layer dependency).
  snprintf(buf, sizeof(buf), "%2d %s,  load average: N/A, N/A, N/A", users,
           users == 1 ? "user" : "users");
  line += buf;
  return line;
}

auto run(const Config& cfg) -> int {
  // Get system uptime
  auto uptime = std::chrono::seconds(GetTickCount64() / 1000);

  // Get current time
  auto now = std::chrono::system_clock::now();
  auto now_time_t = std::chrono::system_clock::to_time_t(now);

  // Format current time
  std::stringstream time_ss;
  time_ss << std::put_time(std::localtime(&now_time_t), "%H:%M:%S");

  if (cfg.since) {
    // Calculate boot time
    auto boot_time = now - uptime;
    auto boot_time_t = std::chrono::system_clock::to_time_t(boot_time);

    std::stringstream boot_ss;
    boot_ss << std::put_time(std::localtime(&boot_time_t), "%Y-%m-%d %H:%M:%S");

    safePrintLn(boot_ss.str());
  } else if (cfg.pretty) {
    safePrintLn(format_uptime(uptime));
  } else {
    safePrintLn(format_default_line(uptime, count_logged_in_users(),
                                    time_ss.str()));
  }

  return 0;
}

}  // namespace uptime_pipeline

REGISTER_COMMAND(
    uptime, "uptime", "uptime [OPTION]...",
    "Tell how long the system has been running.\n"
    "\n"
    "Note: This implementation provides Windows-specific information.\n"
    "Load average is not available on Windows.",
    "  uptime\n"
    "  uptime -p\n"
    "  uptime -s",
    "w(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", UPTIME_OPTIONS) {
  using namespace uptime_pipeline;

  // [GNU] procps uptime rejects operands (usage error, exit 1).
  if (!ctx.positionals.empty()) {
    return cp::report_extra_operand("uptime", ctx.positionals.front());
  }

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"uptime");
    return 1;
  }

  return run(*cfg_result);
}
