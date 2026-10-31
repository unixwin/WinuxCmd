// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for w.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <ws2tcpip.h>
#include <wtsapi32.h>

#include "core/command_macros.h"

#pragma comment(lib, "wtsapi32.lib")

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// [GNU] procps w.c option surface (usage() in procps-v4.0.4 src/w.c):
// -h/--no-header, -u/--no-current, -s/--short, -f/--from, -o/--old-style,
// -i/--ip-addr, -p/--pids. Earlier releases of this applet accepted a set
// of invented flags (-a/-b/-d/-q/-r/-c/-w/-t, --quiet/--count/--heading)
// that procps w does not have; they were removed in the #1462 audit.
auto constexpr W_OPTIONS = std::array{
    OPTION("-h", "--no-header", "do not print header", BOOL_TYPE),
    OPTION("-u", "--no-current", "ignore current process username",
           BOOL_TYPE),
    OPTION("-s", "--short", "short format", BOOL_TYPE),
    OPTION("-f", "--from", "show remote hostname field", BOOL_TYPE),
    OPTION("-o", "--old-style", "old style output", BOOL_TYPE),
    OPTION("-i", "--ip-addr",
           "display IP address instead of hostname (if possible)",
           BOOL_TYPE),
    OPTION("-p", "--pids", "show the PID(s) of processes in WHAT",
           BOOL_TYPE)};

namespace w_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool header = true;
  bool longform = true;
  bool from = true;
  bool oldstyle = false;
  bool ip_addresses = false;
  bool pids = false;
  std::string user_filter;
};

auto build_config(const CommandContext<W_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  // [GNU] w.c: -h clears the header flag; it does not force it on.
  cfg.header = !(ctx.get<bool>("--no-header", false) ||
                 ctx.get<bool>("-h", false));
  cfg.longform =
      !(ctx.get<bool>("--short", false) || ctx.get<bool>("-s", false));
  cfg.from = true;
  if (ctx.get<bool>("--from", false) || ctx.get<bool>("-f", false)) {
    cfg.from = !cfg.from;  // [GNU] w.c: 'f' toggles the from field
  }
  if (ctx.get<bool>("--ip-addr", false) || ctx.get<bool>("-i", false)) {
    cfg.ip_addresses = true;
    cfg.from = true;  // [GNU] w.c: -i also enables the from field
  }
  cfg.oldstyle =
      ctx.get<bool>("--old-style", false) || ctx.get<bool>("-o", false);
  cfg.pids = ctx.get<bool>("--pids", false) || ctx.get<bool>("-p", false);
  // [GNU] w.c: a single optional [user] operand filters the listing.
  if (!ctx.positionals.empty()) {
    cfg.user_filter = ctx.positionals.front();
  }
  return cfg;
}

struct SessionRow {
  std::string user;
  std::string tty;
  std::string from;
  long long logon_time = -1;  // seconds since epoch; -1 when unknown
  long long idle_seconds = -1;
  int idle_centi = 0;
  long long jcpu_centis = 0;
  long long pcpu_centis = 0;
  long leader_pid = -1;
  long best_pid = -1;
  std::string what = "-";
};

auto filetime_to_unix(const FILETIME& ft) -> long long {
  ULARGE_INTEGER value;
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return static_cast<long long>(value.QuadPart / 10000000ULL) -
         11644473600LL;
}

auto query_wstring(DWORD session_id, WTS_INFO_CLASS kind) -> std::string {
  LPWSTR buffer = nullptr;
  DWORD buffer_size = 0;
  std::string result;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session_id, kind,
                                  &buffer, &buffer_size) &&
      buffer != nullptr) {
    result = wstring_to_utf8(std::wstring(buffer));
  }
  if (buffer) WTSFreeMemory(buffer);
  return result;
}

auto query_client_from(DWORD session_id) -> std::string {
  // The WTS structure is WTS_CLIENT_ADDRESS (no W suffix exists).
  WTS_CLIENT_ADDRESS* address = nullptr;
  DWORD size = 0;
  std::string result;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session_id,
                                  WTSClientAddress,
                                  reinterpret_cast<LPWSTR*>(&address),
                                  &size) &&
      address != nullptr) {
    if (address->AddressFamily == AF_INET) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%u.%u.%u.%u", address->Address[2],
               address->Address[3], address->Address[4], address->Address[5]);
      result = buf;
    } else if (address->AddressFamily == AF_INET6) {
      IN6_ADDR addr6{};
      memcpy(addr6.u.Byte, address->Address + 2, 16);
      char buf[INET6_ADDRSTRLEN];
      if (InetNtopA(AF_INET6, &addr6, buf, sizeof(buf))) result = buf;
    }
  }
  if (address) WTSFreeMemory(address);
  return result;
}

auto query_logon_time(DWORD session_id) -> long long {
  LARGE_INTEGER value{};
  DWORD size = 0;
  LPWSTR buffer = nullptr;
  long long result = -1;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session_id,
                                  WTSLogonTime, &buffer, &size) &&
      buffer != nullptr && size >= sizeof(LARGE_INTEGER)) {
    memcpy(&value, buffer, sizeof(value));
    FILETIME ft;
    ft.dwLowDateTime = value.LowPart;
    ft.dwHighDateTime = static_cast<DWORD>(value.HighPart);
    result = filetime_to_unix(ft);
  }
  if (buffer) WTSFreeMemory(buffer);
  return result;
}

auto query_idle(DWORD session_id, long long& seconds, int& centi) -> bool {
  DWORD value = 0;
  DWORD size = 0;
  LPWSTR buffer = nullptr;
  bool ok = false;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session_id,
                                  WTSIdleTime, &buffer, &size) &&
      buffer != nullptr && size >= sizeof(DWORD)) {
    memcpy(&value, buffer, sizeof(value));
    // WTSIdleTime is reported in milliseconds since the last input.
    seconds = value / 1000;
    centi = static_cast<int>((value % 1000) / 10);
    ok = true;
  }
  if (buffer) WTSFreeMemory(buffer);
  return ok;
}

struct SessionProcesses {
  long long jcpu_centis = 0;
  long long best_cpu_centis = -1;
  long best_pid = -1;
  std::string best_name;
  long leader_pid = -1;
  unsigned long long leader_created = ~0ULL;
};

// Aggregate per-session process data the way procps find_best_proc does:
// JCPU is the CPU total of every process in the session, PCPU/WHAT come
// from the single busiest process, and the leader is the oldest one.
auto collect_session_processes() -> std::map<DWORD, SessionProcesses> {
  std::map<DWORD, SessionProcesses> by_session;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return by_session;

  PROCESSENTRY32W entry;
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      DWORD session_id = 0;
      if (!ProcessIdToSessionId(entry.th32ProcessID, &session_id)) continue;
      if (session_id == 0) continue;

      SessionProcesses& agg = by_session[session_id];
      const std::string name = wstring_to_utf8(entry.szExeFile);

      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   entry.th32ProcessID);
      if (process == nullptr) continue;
      FILETIME creation, exit_time, kernel, user;
      if (GetProcessTimes(process, &creation, &exit_time, &kernel, &user)) {
        auto ticks100ns = [](const FILETIME& ft) -> unsigned long long {
          ULARGE_INTEGER v;
          v.LowPart = ft.dwLowDateTime;
          v.HighPart = ft.dwHighDateTime;
          return v.QuadPart;
        };
        // procps counts CPU in hundredths of a second (hertz = 100).
        const long long cpu_centis = static_cast<long long>(
            (ticks100ns(kernel) + ticks100ns(user)) / 100000ULL);
        agg.jcpu_centis += cpu_centis;
        if (cpu_centis > agg.best_cpu_centis) {
          agg.best_cpu_centis = cpu_centis;
          agg.best_pid = static_cast<long>(entry.th32ProcessID);
          agg.best_name = name;
        }
        const unsigned long long created = ticks100ns(creation);
        if (created < agg.leader_created) {
          agg.leader_created = created;
          agg.leader_pid = static_cast<long>(entry.th32ProcessID);
        }
      }
      CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return by_session;
}

auto enumerate_rows() -> std::vector<SessionRow> {
  std::vector<SessionRow> rows;
  const auto processes = collect_session_processes();

  PWTS_SESSION_INFOW sessions = nullptr;
  DWORD session_count = 0;
  if (!WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions,
                             &session_count)) {
    return rows;
  }

  for (DWORD i = 0; i < session_count; ++i) {
    const DWORD session_id = sessions[i].SessionId;
    if (sessions[i].State != WTSActive && sessions[i].State != WTSConnected &&
        sessions[i].State != WTSDisconnected) {
      continue;
    }
    SessionRow row;
    row.user = query_wstring(session_id, WTSUserName);
    if (row.user.empty()) continue;  // procps lists USER_PROCESS rows only
    row.tty = query_wstring(session_id, WTSWinStationName);
    row.from = query_client_from(session_id);
    row.logon_time = query_logon_time(session_id);
    if (!query_idle(session_id, row.idle_seconds, row.idle_centi)) {
      row.idle_seconds = -1;
    }
    const auto it = processes.find(session_id);
    if (it != processes.end()) {
      row.jcpu_centis = it->second.jcpu_centis;
      row.pcpu_centis =
          it->second.best_cpu_centis >= 0 ? it->second.best_cpu_centis : 0;
      row.best_pid = it->second.best_pid;
      row.leader_pid = it->second.leader_pid;
      if (!it->second.best_name.empty()) row.what = it->second.best_name;
    }
    rows.push_back(std::move(row));
  }
  WTSFreeMemory(sessions);
  return rows;
}

// [GNU] procps print_time_ival7(): every value is exactly 7 columns.
auto time_ival7(long long seconds, int centi, bool oldstyle) -> std::string {
  char buf[32];
  if (seconds < 0) return "   ?   ";
  if (oldstyle) {
    if (seconds >= 48LL * 60 * 60) {
      snprintf(buf, sizeof(buf), " %2llddays", seconds / (24 * 60 * 60));
    } else if (seconds >= 60LL * 60) {
      snprintf(buf, sizeof(buf), " %2lld:%02lld ", seconds / (60 * 60),
               (seconds / 60) % 60);
    } else if (seconds > 60) {
      snprintf(buf, sizeof(buf), " %2lld:%02llum", seconds / 60, seconds % 60);
    } else {
      return "       ";
    }
  } else {
    if (seconds >= 48LL * 60 * 60) {
      snprintf(buf, sizeof(buf), " %2llddays", seconds / (24 * 60 * 60));
    } else if (seconds >= 60LL * 60) {
      snprintf(buf, sizeof(buf), " %2lld:%02lldm", seconds / (60 * 60),
               (seconds / 60) % 60);
    } else if (seconds > 60) {
      snprintf(buf, sizeof(buf), " %2lld:%02lld ", seconds / 60, seconds % 60);
    } else {
      snprintf(buf, sizeof(buf), " %2lld.%02ds", seconds,
               static_cast<long long>(centi));
    }
  }
  return std::string(buf);
}

// [GNU] procps print_logintime(): " HH:MM  " for logins within the last
// 12 hours (or today), " WwwHH  " within six days, else " DDMonYY".
auto format_logintime(long long logon_time) -> std::string {
  if (logon_time < 0) return "        ";
  const std::time_t logt = static_cast<std::time_t>(logon_time);
  const std::time_t now = std::time(nullptr);
  std::tm logtm{};
  std::tm curtm{};
  localtime_s(&curtm, &now);
  localtime_s(&logtm, &logt);
  char buf[32];
  if (now - logt > 12 * 60 * 60 && logtm.tm_yday != curtm.tm_yday) {
    if (now - logt > 6 * 24 * 60 * 60) {
      char mon[8] = {};
      strftime(mon, sizeof(mon), "%b", &logtm);
      snprintf(buf, sizeof(buf), " %02d%3s%02d", logtm.tm_mday, mon,
               logtm.tm_year % 100);
    } else {
      char wday[8] = {};
      strftime(wday, sizeof(wday), "%a", &logtm);
      snprintf(buf, sizeof(buf), " %3s%02d  ", wday, logtm.tm_hour);
    }
  } else {
    snprintf(buf, sizeof(buf), " %02d:%02d  ", logtm.tm_hour, logtm.tm_min);
  }
  return std::string(buf);
}

// [GNU] procps print_host(): printable host padded to the field width;
// an empty host renders as "-".
auto print_host_field(const std::string& host, size_t width) -> std::string {
  std::string out;
  for (char c : host) {
    if (out.size() >= width) break;
    if (c >= 0x20 && c < 0x7f && c != ' ') {
      out += c;
    } else {
      out += '-';
      break;
    }
  }
  if (out.empty()) out = "-";
  while (out.size() < width) out += ' ';
  return out;
}

auto pad_field(const std::string& value, size_t width, size_t max_chars)
    -> std::string {
  std::string out = value.substr(0, max_chars);
  while (out.size() < width) out += ' ';
  return out;
}

// [GNU] the header's first line is procps_uptime_sprint(), identical in
// shape to the default uptime line (see uptime.cpp).
auto sprint_uptime_line(int users) -> std::string {
  const long long total = static_cast<long long>(GetTickCount64() / 1000);
  const long long days = total / (60 * 60 * 24);
  const long long hours = (total / (60 * 60)) % 24;
  const long long minutes = (total / 60) % 60;

  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char clock_buf[16];
  strftime(clock_buf, sizeof(clock_buf), "%H:%M:%S", &local);

  std::string line = std::string(" ") + clock_buf + " up ";
  char buf[96];
  if (days > 0) {
    snprintf(buf, sizeof(buf), "%lld %s, ", days, days > 1 ? "days" : "day");
    line += buf;
  }
  if (hours > 0) {
    snprintf(buf, sizeof(buf), "%2lld:%02lld, ", hours, minutes);
    line += buf;
  } else {
    snprintf(buf, sizeof(buf), "%lld min, ", minutes);
    line += buf;
  }
  // [DIFFERS] Windows exposes no load average; reported as N/A rather
  // than fabricated (simulation-layer dependency, niu #758).
  snprintf(buf, sizeof(buf), "%2d %s,  load average: N/A, N/A, N/A", users,
           users == 1 ? "user" : "users");
  line += buf;
  return line;
}

auto run(const Config& cfg) -> int {
  auto rows = enumerate_rows();

  if (cfg.header) {
    safePrintLn(sprint_uptime_line(static_cast<int>(rows.size())));
    // [GNU] w.c header: USER in an 8-wide field, FROM in a (fromlen-1)-
    // wide field, then the literal tail for the form in use.
    std::string heading = pad_field("USER", 8, 8) + " TTY      ";
    if (cfg.from) heading += pad_field("FROM", 15, 15);
    heading += cfg.longform ? "  LOGIN@   IDLE   JCPU   PCPU WHAT"
                            : "   IDLE WHAT";
    safePrintLn(heading);
  }

  for (const auto& row : rows) {
    if (!cfg.user_filter.empty() && row.user != cfg.user_filter) continue;
    // [GNU] w.c showinfo(): "%-*.*s" user (9 wide), "%-9.8s" tty.
    std::string line = pad_field(row.user, 9, 8) + pad_field(row.tty, 9, 8);
    if (cfg.from) line += print_host_field(row.from, 16);
    if (cfg.longform) {
      line += format_logintime(row.logon_time);
      line += time_ival7(row.idle_seconds, row.idle_centi, cfg.oldstyle);
      line += time_ival7(row.jcpu_centis / 100,
                         static_cast<int>(row.jcpu_centis % 100),
                         cfg.oldstyle);
      if (row.pcpu_centis > 0) {
        line += time_ival7(row.pcpu_centis / 100,
                           static_cast<int>(row.pcpu_centis % 100),
                           cfg.oldstyle);
      } else {
        line += "   ?   ";
      }
    } else {
      line += time_ival7(row.idle_seconds, row.idle_centi, cfg.oldstyle);
    }
    if (cfg.pids) {
      char buf[32];
      snprintf(buf, sizeof(buf), " %ld/%ld", row.leader_pid, row.best_pid);
      line += buf;
    }
    line += " " + row.what;
    safePrintLn(line);
  }
  return 0;
}

}  // namespace w_pipeline

REGISTER_COMMAND(w, "w", "w [options] [user]",
                 "Show who is logged on and what they are doing.\n"
                 "\n"
                 "This command displays information about currently logged-in\n"
                 "users and their processes, similar to the Linux w command.",
                 "  w\n"
                 "  w -s\n"
                 "  w -h\n"
                 "  w username",
                 "who(1), uptime(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 W_OPTIONS) {
  using namespace w_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"w");
    return 1;
  }

  return run(*cfg_result);
}
