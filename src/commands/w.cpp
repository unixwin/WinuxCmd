// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for w (show who is logged on and what they are
/// doing).
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <winbase.h>
#include <wtsapi32.h>

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "wtsapi32.lib")

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr W_OPTIONS = std::array{
    OPTION("-a", "--all", "same as -b -d -u -t"),
    OPTION("-b", "", "omit process id from output"),
    OPTION("-d", "", "omit host and display fields from output"),
    OPTION("-f", "--from", "print from fields"),
    OPTION("-i", "", "do not show idle time"),
    OPTION("-q", "--quiet", "quiet output, no headings, just data"),
    OPTION("-r", "", "omit remote host from output"),
    OPTION("-s", "", "show session id instead of process id"),
    OPTION("-t", "", "include a header with uptime"),
    OPTION("-u", "", "omit user name from output"),
    OPTION("-w", "", "print user's tty and pid only"),
    OPTION("-h", "--heading", "print heading line"),
    OPTION("-c", "--count", "just print the number of logged-in users")};

namespace w_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool quiet = false;
  bool all = false;
  bool count = false;
  bool heading = true;
  bool omit_host = false;
  bool omit_pid = false;
  bool show_session_id = false;
  bool show_from = false;
  bool omit_user = false;
  bool tty_pid_only = false;
};

auto build_config(const CommandContext<W_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.quiet = ctx.has("-q") || ctx.has("--quiet");
  cfg.all = ctx.has("-a") || ctx.has("--all");
  cfg.count = ctx.has("-c") || ctx.has("--count");
  cfg.heading = ctx.has("-h") || ctx.has("--heading") || !cfg.quiet;
  cfg.omit_host = ctx.has("-r") || cfg.all;
  cfg.omit_pid = ctx.has("-b") || cfg.all;
  cfg.show_session_id = ctx.has("-s");
  cfg.show_from = ctx.has("-f") || ctx.has("--from");
  cfg.omit_user = ctx.has("-u") || cfg.all;
  cfg.tty_pid_only = ctx.has("-w");
  return cfg;
}

auto get_env_utf8(const wchar_t* key) -> std::optional<std::string> {
  DWORD size = GetEnvironmentVariableW(key, nullptr, 0);
  if (size == 0) return std::nullopt;
  std::wstring value;
  value.resize(size - 1);
  if (GetEnvironmentVariableW(key, value.data(), size) == 0)
    return std::nullopt;
  return wstring_to_utf8(value);
}

auto format_time_diff(LARGE_INTEGER login_time) -> std::string {
  // Login time is in 100ns units since 1601-01-01
  ULARGE_INTEGER now;
  GetSystemTimeAsFileTime((FILETIME*)&now);
  ULARGE_INTEGER diff = {now.QuadPart - login_time.QuadPart};
  ULONGLONG seconds = diff.QuadPart / 10000000ULL;
  ULONGLONG days = seconds / 86400;
  ULONGLONG hours = (seconds % 86400) / 3600;
  ULONGLONG minutes = (seconds % 3600) / 60;
  char buf[64];
  if (days > 0) {
    sprintf_s(buf, "%lld:%02lld", days, hours);
  } else {
    sprintf_s(buf, "%02lld:%02lld", hours, minutes);
  }
  return buf;
}

auto run(const Config& cfg) -> int {
  // Get system uptime
  ULONGLONG uptime;
  uptime = GetTickCount64();
  ULONGLONG uptime_seconds = uptime / 1000ULL;

  // Enumerate sessions
  PWTS_SESSION_INFOW pSessionInfo = nullptr;
  DWORD sessionCount = 0;
  if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo,
                             &sessionCount)) {
    safeErrorPrintLn("w: failed to enumerate sessions");
    return 1;
  }

  // Count active sessions
  int active_count = 0;
  std::vector<int> active_sessions;
  for (DWORD i = 0; i < sessionCount; ++i) {
    if (pSessionInfo[i].State == WTSActive) {
      active_count++;
      active_sessions.push_back(pSessionInfo[i].SessionId);
    }
  }

  if (cfg.count) {
    safePrint(active_count);
    safePrintLn("");
    WTSFreeMemory(pSessionInfo);
    return 0;
  }

  // Uptime header
  if (cfg.heading) {
    ULONGLONG up_days = uptime_seconds / 86400;
    ULONGLONG up_hours = (uptime_seconds % 86400) / 3600;
    ULONGLONG up_minutes = (uptime_seconds % 3600) / 60;
    ULONGLONG up_seconds = uptime_seconds % 60;
    char uptime_str[128];
    if (up_days > 0) {
      sprintf_s(uptime_str, "%llu days, %llu:%02llu:%02llu", up_days, up_hours,
                up_minutes, up_seconds);
    } else {
      sprintf_s(uptime_str, "%llu:%02llu:%02llu", up_hours, up_minutes,
                up_seconds);
    }
    safePrint(" 14:23:45 up ");
    safePrint(uptime_str);
    safePrint(", ");
    safePrint(active_count);
    if (active_count == 1)
      safePrint(" user");
    else
      safePrint(" users");
    safePrintLn(", 1 job running");
  }

  // Column headers
  if (cfg.heading && !cfg.quiet) {
    safePrint("USER");
    if (!cfg.tty_pid_only) safePrint(" TTY");
    if (!cfg.omit_host) safePrint(" FROM");
    if (!cfg.omit_pid && !cfg.tty_pid_only) safePrint(" PID");
    safePrint(" IDLE TIME");
    if (!cfg.omit_user && !cfg.tty_pid_only) safePrint(" WHAT");
    safePrintLn("");
  }

  for (int sid : active_sessions) {
    LPWSTR buffer = nullptr;
    DWORD bufferSize = 0;
    std::string user = "(unknown)";
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sid, WTSUserName,
                                    &buffer, &bufferSize) &&
        buffer != nullptr && bufferSize > sizeof(wchar_t)) {
      user = wstring_to_utf8(buffer);
      WTSFreeMemory(buffer);
    }

    // Connect time (approximate - use idle time as proxy)
    std::string login_time_str = "-";
    LPWSTR idleBuf = nullptr;
    DWORD idleSize = 0;
    if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sid, WTSIdleTime,
                                    &idleBuf, &idleSize)) {
      if (idleBuf && idleSize >= sizeof(DWORD)) {
        DWORD idle_minutes =
            *reinterpret_cast<DWORD*>(idleBuf) / 1000ULL / 60ULL;
        char buf[32];
        sprintf_s(buf, "%lu:%02lu", idle_minutes / 60, idle_minutes % 60);
        login_time_str = buf;
      }
      WTSFreeMemory(idleBuf);
    }
    // TTY: CON
    // Process name
    std::string process_name = "-";
    DWORD pid = 0;
    {
      // Get the process for this session
      LPWSTR winStation = nullptr;
      DWORD wsSize = 0;
      if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sid,
                                      WTSWinStationName, &winStation,
                                      &wsSize)) {
        if (winStation) WTSFreeMemory(winStation);
      }
    }

    if (!cfg.tty_pid_only) {
      if (!cfg.omit_user) safePrint(user);
      safePrint(" CON");
      if (!cfg.omit_host) safePrint(" 0.0.0.0");
      if (!cfg.omit_pid) safePrint(" 0");
      safePrint(" ");
      safePrint(login_time_str);
      safePrintLn("");
    } else {
      if (!cfg.omit_user) safePrint(user);
      safePrint(" 0");
      safePrintLn("");
    }
  }

  WTSFreeMemory(pSessionInfo);
  return 0;
}

}  // namespace w_pipeline

REGISTER_COMMAND(
    w, "w", "w [OPTION]...",
    "Show who is logged on and what they are doing.\n"
    "On Windows, shows active terminal sessions with user, TTY, idle time.",
    "  w\n"
    "  w -q\n"
    "  w --count",
    "who(1), users(1), uptime(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    W_OPTIONS) {
  using namespace w_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"w");
    return 1;
  }

  return run(*cfg_result);
}
