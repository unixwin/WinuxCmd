// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for who.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <wtsapi32.h>

#include "core/command_macros.h"

#pragma comment(lib, "wtsapi32.lib")

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr WHO_OPTIONS = std::array{
    // [DIFFERS] Windows has no Unix utmp records for these views.
    OPTION("-a", "--all", "same as -b -d --login -p -r -t -T -u", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-b", "--boot", "time of last system boot", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-d", "--dead", "print dead processes", BOOL_TYPE),
    OPTION("-H", "--heading", "print line of column headings", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-l", "--login", "print system login processes", BOOL_TYPE),
    // [GNU] Windows output already identifies the current stdin user.
    OPTION("-m", "", "only hostname and user associated with stdin", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-p", "--process", "print active processes spawned by init",
           BOOL_TYPE),
    OPTION("-q", "--count", "all login names and number of users logged on",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-r", "--runlevel", "print current runlevel", BOOL_TYPE),
    OPTION("-s", "--short", "print only name, line, and time", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-t", "--time", "print last system clock change", BOOL_TYPE),
    // [GNU] -T is the short alias of -w.
    OPTION("-T", "", "add user's message status as +, - or ?", BOOL_TYPE),
    // [GNU] Windows output already lists the current logged-in user.
    OPTION("-u", "--users", "list users logged in", BOOL_TYPE),
    OPTION("-w", "--mesg", "add user's message status as +, - or ?", BOOL_TYPE),
    // [DIFFERS] Windows has no utmp hostname canonicalization field.
    OPTION("", "--lookup", "attempt to canonicalize hostnames via DNS",
           BOOL_TYPE),
    // [GNU] Alias for --mesg.
    OPTION("", "--message", "same as --mesg"),
    // [DIFFERS] Windows has no utmp terminal writability state.
    OPTION("", "--writable", "list only users with a writable terminal")};

namespace who_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool heading = false;
  bool short_output = true;
  bool short_list = false;  // -q
  bool include_mesg = false;
  bool include_idle = false;  // -u: idle + pid columns
  bool my_line_only = false;  // -m or the two-operand "am i" form
  std::optional<std::string> unsupported;
};

auto build_config(const CommandContext<WHO_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.heading = ctx.get<bool>("--heading", false) || ctx.get<bool>("-H", false);
  cfg.short_list = ctx.get<bool>("--count", false) || ctx.get<bool>("-q", false);
  // [GNU] --message and -T forward to the --mesg behavior.
  cfg.include_mesg = ctx.get<bool>("--mesg", false) ||
                     ctx.get<bool>("-w", false) ||
                     ctx.get<bool>("--message", false) ||
                     ctx.get<bool>("-T", false);
  cfg.include_idle =
      ctx.get<bool>("--users", false) || ctx.get<bool>("-u", false);
  cfg.my_line_only = ctx.get<bool>("-m", false);
  // [GNU] who.c: with no record-selecting option the output stays in the
  // short form (the "assumptions" path sets short_output); -u selects the
  // long form, and an explicit -s forces the short form back on.
  cfg.short_output = !cfg.include_idle;
  if (ctx.get<bool>("--short", false) || ctx.get<bool>("-s", false)) {
    cfg.short_output = true;
  }

  // [GNU] who.c: zero, one (utmp FILE), or two ("am i") operands.
  if (ctx.positionals.size() > 2) {
    return std::unexpected("__extra_operand__");
  }
  if (ctx.positionals.size() == 2) {
    cfg.my_line_only = true;
  } else if (ctx.positionals.size() == 1) {
    // [DIFFERS] A utmp FILE operand is accepted, but its records are not
    // parsed: the session table replaces utmp on Windows (simulation
    // layer, niu #758). A missing file is still the GNU error.
    std::error_code ec;
    if (!std::filesystem::exists(ctx.positionals.front(), ec)) {
      return std::unexpected(std::string(ctx.positionals.front()) +
                             ": No such file or directory");
    }
  }

  const auto reject = [&](std::string option) {
    if (!cfg.unsupported) cfg.unsupported = std::move(option);
  };
  if (ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false))
    reject("--all");
  if (ctx.get<bool>("-b", false) || ctx.get<bool>("--boot", false))
    reject("--boot");
  if (ctx.get<bool>("-d", false) || ctx.get<bool>("--dead", false))
    reject("--dead");
  if (ctx.get<bool>("-l", false) || ctx.get<bool>("--login", false))
    reject("--login");
  if (ctx.get<bool>("-p", false) || ctx.get<bool>("--process", false))
    reject("--process");
  if (ctx.get<bool>("-r", false) || ctx.get<bool>("--runlevel", false))
    reject("--runlevel");
  if (ctx.get<bool>("-t", false) || ctx.get<bool>("--time", false))
    reject("--time");
  if (ctx.get<bool>("--lookup", false)) reject("--lookup");
  if (ctx.get<bool>("--writable", false)) reject("--writable");
  if (cfg.unsupported) {
    return std::unexpected(
        winux::i18n::translate("command.who.error.unsupported_option",
                               "who: option is not supported on Windows: ") +
        *cfg.unsupported);
  }
  return cfg;
}

struct WhoEntry {
  std::string user;
  std::string line;
  long long logon_time = -1;  // unix seconds; -1 when unknown
  long long idle_seconds = -1;
  long pid = -1;
  std::string comment;
  DWORD session_id = 0;
};

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

auto query_logon_time(DWORD session_id) -> long long {
  LARGE_INTEGER value{};
  DWORD size = 0;
  LPWSTR buffer = nullptr;
  long long result = -1;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session_id,
                                  WTSLogonTime, &buffer, &size) &&
      buffer != nullptr && size >= sizeof(LARGE_INTEGER)) {
    memcpy(&value, buffer, sizeof(value));
    const unsigned long long ticks = static_cast<unsigned long long>(value.QuadPart);
    result = static_cast<long long>(ticks / 10000000ULL) - 11644473600LL;
  }
  if (buffer) WTSFreeMemory(buffer);
  return result;
}

auto query_idle_seconds(DWORD session_id) -> long long {
  DWORD value = 0;
  DWORD size = 0;
  LPWSTR buffer = nullptr;
  long long result = -1;
  if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session_id,
                                  WTSIdleTime, &buffer, &size) &&
      buffer != nullptr && size >= sizeof(DWORD)) {
    memcpy(&value, buffer, sizeof(value));
    result = value / 1000;
  }
  if (buffer) WTSFreeMemory(buffer);
  return result;
}

// The utmp ut_pid of a login entry corresponds to the session's login
// (leader) process: the oldest process living in the session.
auto collect_leader_pids() -> std::map<DWORD, long> {
  std::map<DWORD, long> leaders;
  std::map<DWORD, unsigned long long> created;
  HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snapshot == INVALID_HANDLE_VALUE) return leaders;

  PROCESSENTRY32W entry;
  entry.dwSize = sizeof(entry);
  if (Process32FirstW(snapshot, &entry)) {
    do {
      DWORD session_id = 0;
      if (!ProcessIdToSessionId(entry.th32ProcessID, &session_id)) continue;
      HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                   entry.th32ProcessID);
      if (process == nullptr) continue;
      FILETIME creation, exit_time, kernel, user;
      if (GetProcessTimes(process, &creation, &exit_time, &kernel, &user)) {
        ULARGE_INTEGER v;
        v.LowPart = creation.dwLowDateTime;
        v.HighPart = creation.dwHighDateTime;
        const auto it = created.find(session_id);
        if (it == created.end() || v.QuadPart < it->second) {
          created[session_id] = v.QuadPart;
          leaders[session_id] = static_cast<long>(entry.th32ProcessID);
        }
      }
      CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
  }
  CloseHandle(snapshot);
  return leaders;
}

auto current_user_name() -> std::string {
  WCHAR username[256];
  DWORD username_size = 256;
  if (!GetUserNameW(username, &username_size)) return {};
  return wstring_to_utf8(std::wstring(username));
}

auto enumerate_entries(bool want_idle_pid) -> std::vector<WhoEntry> {
  std::vector<WhoEntry> entries;
  const auto leaders = want_idle_pid ? collect_leader_pids()
                                     : std::map<DWORD, long>{};
  const DWORD console_session = WTSGetActiveConsoleSessionId();

  PWTS_SESSION_INFOW sessions = nullptr;
  DWORD session_count = 0;
  if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions,
                            &session_count)) {
    for (DWORD i = 0; i < session_count; ++i) {
      const DWORD session_id = sessions[i].SessionId;
      if (sessions[i].State != WTSActive &&
          sessions[i].State != WTSDisconnected) {
        continue;
      }
      WhoEntry entry;
      entry.session_id = session_id;
      entry.user = query_wstring(session_id, WTSUserName);
      if (entry.user.empty()) continue;
      entry.line = query_wstring(session_id, WTSWinStationName);
      entry.logon_time = query_logon_time(session_id);
      if (want_idle_pid) {
        entry.idle_seconds = query_idle_seconds(session_id);
        const auto it = leaders.find(session_id);
        if (it != leaders.end()) entry.pid = it->second;
      }
      if (session_id == console_session) {
        entry.comment = "(console)";
      } else {
        const std::string client = query_wstring(session_id, WTSClientName);
        if (!client.empty()) entry.comment = "(" + client + ")";
      }
      entries.push_back(std::move(entry));
    }
    WTSFreeMemory(sessions);
  }

  if (entries.empty()) {
    // Simulation face: with no enumerable login sessions, report the
    // current interactive user, as this command has always done.
    WhoEntry entry;
    entry.user = current_user_name();
    entry.line = "console";
    entry.comment = "(console)";
    entry.logon_time =
        static_cast<long long>(std::time(nullptr));
    DWORD session_id = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &session_id);
    entry.session_id = session_id;
    entries.push_back(std::move(entry));
  }
  return entries;
}

// [GNU] coreutils who.c idle_string(): the idle value rendered from a
// duration ("  .  " under a minute, HH:MM under a day, " old " past it,
// "  ?" when unknown).
auto idle_string(long long idle_seconds) -> std::string {
  if (idle_seconds < 0) return "  ?";
  if (idle_seconds < 60) return "  .  ";
  if (idle_seconds < 24 * 60 * 60) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%02lld:%02lld", idle_seconds / (60 * 60),
             (idle_seconds % (60 * 60)) / 60);
    return std::string(buf);
  }
  return " old ";
}

auto pad_right(const std::string& value, size_t width) -> std::string {
  if (value.size() >= width) return value;
  return value + std::string(width - value.size(), ' ');
}

auto pad_left(const std::string& value, size_t width) -> std::string {
  if (value.size() >= width) return value;
  return std::string(width - value.size(), ' ') + value;
}

auto rtrim(std::string value) -> std::string {
  while (!value.empty() && value.back() == ' ') value.pop_back();
  return value;
}

// [GNU] coreutils who.c print_line(): "%-8s" user, optional " x" mesg,
// " %-12s" line, " %-16s" time, optional " %-6s" idle and " %10s" pid
// (both suppressed in short output), " %-8s" comment; trailing spaces
// are stripped.
auto print_line(const Config& cfg, const std::string& user, char state,
                const std::string& line, const std::string& time_str,
                const std::string& idle, const std::string& pid,
                const std::string& comment) -> std::string {
  std::string out = pad_right(user, 8);
  if (cfg.include_mesg) {
    out += " ";
    out += state;
  }
  out += " " + pad_right(line, 12);
  out += " " + pad_right(time_str, 16);
  if (cfg.include_idle && !cfg.short_output) {
    out += " " + pad_right(idle, 6);
    out += " " + pad_left(pid, 10);
  }
  out += " " + pad_right(comment, 8);
  return rtrim(out);
}

auto format_logon(long long logon_time) -> std::string {
  if (logon_time < 0) return {};
  const std::time_t t = static_cast<std::time_t>(logon_time);
  std::tm local{};
  localtime_s(&local, &t);
  char buf[32];
  strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &local);
  return std::string(buf);
}

auto run(const Config& cfg) -> int {
  auto entries = enumerate_entries(cfg.include_idle);

  if (cfg.my_line_only && !cfg.short_list) {
    DWORD my_session = 0;
    ProcessIdToSessionId(GetCurrentProcessId(), &my_session);
    std::vector<WhoEntry> mine;
    for (const auto& entry : entries) {
      if (entry.session_id == my_session) mine.push_back(entry);
    }
    entries = std::move(mine);
  }

  // [GNU] who.c list_entries_who(): names space-joined on one line,
  // then "# users=N".
  if (cfg.short_list) {
    std::string line;
    for (size_t i = 0; i < entries.size(); ++i) {
      if (i > 0) line += " ";
      line += entries[i].user;
    }
    safePrintLn(line);
    safePrintLn("# users=" + std::to_string(entries.size()));
    return 0;
  }

  if (cfg.heading) {
    safePrintLn(print_line(cfg, "NAME", ' ', "LINE", "TIME", "IDLE", "PID",
                           "COMMENT"));
  }

  for (const auto& entry : entries) {
    // [DIFFERS] Windows has no tty-writability bit; interactive
    // terminals accept messages, so the mesg state is '+'.
    safePrintLn(print_line(cfg, entry.user, '+', entry.line,
                           format_logon(entry.logon_time),
                           idle_string(entry.idle_seconds),
                           entry.pid >= 0 ? std::to_string(entry.pid) : "",
                           entry.comment));
  }
  return 0;
}

}  // namespace who_pipeline

REGISTER_COMMAND(
    who, "who", "who [OPTION]... [ FILE | ARG1 ARG2 ]",
    "Print information about users who are currently logged in.\n"
    "\n"
    "Note: This is a Windows implementation. Windows doesn't have\n"
    "the same multi-user concept as Unix, so this command mainly\n"
    "displays the current interactive user.",
    "  who\n"
    "  who -H\n"
    "  who -q",
    "w(1), users(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", WHO_OPTIONS) {
  using namespace who_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "__extra_operand__" &&
        ctx.positionals.size() > 2) {
      return cp::report_extra_operand("who", ctx.positionals[2]);
    }
    cp::report_error(cfg_result, L"who");
    return 1;
  }

  return run(*cfg_result);
}
