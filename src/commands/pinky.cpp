/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
// include other header after pch.h
#include <wtsapi32.h>

#pragma comment(lib, "wtsapi32.lib")

import std;
import core;
import utils;
import container;

auto constexpr PINKY_OPTIONS = std::array{
    // [GNU]
    OPTION("-l", "", "produce long format output", BOOL_TYPE),
    // [GNU]
    OPTION("-b", "", "omit the user home directory and shell in long format",
           BOOL_TYPE),
    // [GNU]
    OPTION("-f", "", "omit the line of column headings in short format",
           BOOL_TYPE),
    // [GNU]
    OPTION("-w", "", "omit the user full name in short format", BOOL_TYPE),
    // [GNU]
    OPTION("-i", "", "omit the user full name and remote host in short format",
           BOOL_TYPE),
    // [GNU]
    OPTION("-q", "", "omit the user full name, remote host, and idle time",
           BOOL_TYPE),
    // [GNU]
    // [DIFFERS] - project files do not exist on Windows
    OPTION("-h", "", "omit the user project file in long format", BOOL_TYPE),
    // [GNU]
    // [DIFFERS] - plan files do not exist on Windows
    OPTION("-p", "", "omit the user plan file in long format", BOOL_TYPE),
    // [GNU]
    // [DIFFERS] - short format is already the default
    OPTION("-s", "", "do short format output", BOOL_TYPE),
    // [GNU]
    // [DIFFERS] - hostname lookup not applicable on Windows
    OPTION("", "--lookup", "attempt to canonicalize hostnames", BOOL_TYPE)};

namespace {
auto current_user_name() -> std::string {
  wchar_t username[UNLEN + 1];
  DWORD size = UNLEN + 1;
  if (GetUserNameW(username, &size)) return wstring_to_utf8(username);
  return "";
}

auto current_domain_name() -> std::string {
  wchar_t buffer[256];
  DWORD size = static_cast<DWORD>(std::size(buffer));
  if (GetEnvironmentVariableW(L"USERDOMAIN", buffer, size) > 0) {
    return wstring_to_utf8(buffer);
  }
  return "";
}

auto append_padded(std::string& out, const std::string& text, size_t width)
    -> void {
  out += text;
  if (text.size() < width) out += std::string(width - text.size(), " "[0]);
}

auto print_short_heading(bool include_heading, bool include_fullname,
                         bool include_idle, bool include_where) -> void {
  if (!include_heading) return;
  std::string out;
  append_padded(out, "Login", 8);
  if (include_fullname) {
    out += " ";
    append_padded(out, "Name", 19);
  }
  out += " ";
  append_padded(out, " TTY", 9);
  if (include_idle) {
    out += " ";
    append_padded(out, "Idle", 6);
  }
  out += " ";
  out += "When";
  out += std::string(8, " "[0]);
  if (include_where) out += " Where";
  safePrintLn(out);
}

struct SessionEntry {
  std::string user;
  long long logon_time = -1;    // unix seconds; -1 when unknown
  long long idle_seconds = -1;  // -1 when unknown
};

// [GNU] The short format lists one row per login (USER_PROCESS) entry
// (pinky.c scan_entries). The WTS session table is the Windows source
// of login entries (simulation layer, niu #758), as in who/users.
auto session_entries() -> std::vector<SessionEntry> {
  std::vector<SessionEntry> entries;
  PWTS_SESSION_INFOW sessions = nullptr;
  DWORD count = 0;
  if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions,
                            &count)) {
    for (DWORD i = 0; i < count; ++i) {
      if (sessions[i].State != WTSActive &&
          sessions[i].State != WTSDisconnected) {
        continue;
      }
      const DWORD sid = sessions[i].SessionId;
      LPWSTR buffer = nullptr;
      DWORD buffer_size = 0;
      if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sid,
                                       WTSUserName, &buffer, &buffer_size) ||
          buffer == nullptr) {
        continue;
      }
      std::string user = wstring_to_utf8(std::wstring(buffer));
      WTSFreeMemory(buffer);
      if (user.empty()) continue;

      SessionEntry entry;
      entry.user = std::move(user);
      LPWSTR raw = nullptr;
      DWORD raw_size = 0;
      if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sid,
                                      WTSLogonTime, &raw, &raw_size) &&
          raw != nullptr && raw_size >= sizeof(LARGE_INTEGER)) {
        LARGE_INTEGER value{};
        memcpy(&value, raw, sizeof(value));
        entry.logon_time =
            static_cast<long long>(
                static_cast<unsigned long long>(value.QuadPart) / 10000000ULL) -
            11644473600LL;
      }
      if (raw) WTSFreeMemory(raw);
      raw = nullptr;
      raw_size = 0;
      DWORD idle_ms = 0;
      if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, sid,
                                      WTSIdleTime, &raw, &raw_size) &&
          raw != nullptr && raw_size >= sizeof(DWORD)) {
        memcpy(&idle_ms, raw, sizeof(idle_ms));
        entry.idle_seconds = idle_ms / 1000;
      }
      if (raw) WTSFreeMemory(raw);
      entries.push_back(std::move(entry));
    }
    WTSFreeMemory(sessions);
  }

  if (entries.empty()) {
    // Simulation face: with no enumerable login sessions, report the
    // current interactive user, as who/users do.
    SessionEntry entry;
    entry.user = current_user_name();
    entry.logon_time = static_cast<long long>(std::time(nullptr));
    if (!entry.user.empty()) entries.push_back(std::move(entry));
  }
  return entries;
}

auto pad_left(const std::string& text, size_t width) -> std::string {
  if (text.size() >= width) return text;
  return std::string(width - text.size(), ' ') + text;
}

// [GNU] pinky.c print_entry(): "%-8s" login; full name as " %-19.19s"
// (a user with no GECOS entry prints the right-aligned "???" field);
// " " + mesg char + "%-8s" line; idle as " %-6s" ("?????" when
// unknown, blank under a minute, HH:MM under a day, else "<days>d");
// " " + the login time ("%b %d %H:%M", the width-12 default form).
auto print_short_entry(const SessionEntry& entry, bool include_fullname,
                       bool include_idle) -> void {
  std::string out;
  append_padded(out, entry.user, 8);
  if (include_fullname) {
    // [DIFFERS] Windows accounts carry no GECOS full name; GNU prints
    // the "???" field for a name it cannot resolve.
    out += " ";
    out += pad_left("???", 19);
  }
  out += " ";
  // [DIFFERS] No tty file exists to stat for the mesg char; GNU uses
  // '?' when the stat fails.
  out += "?";
  append_padded(out, "console", 8);
  if (include_idle) {
    std::string idle;
    if (entry.idle_seconds < 0) {
      idle = "?????";
    } else if (entry.idle_seconds < 60) {
      idle = "     ";
    } else if (entry.idle_seconds < 24 * 60 * 60) {
      char buf[16];
      snprintf(buf, sizeof(buf), "%02lld:%02lld", entry.idle_seconds / 3600,
               (entry.idle_seconds % 3600) / 60);
      idle = buf;
    } else {
      idle = std::to_string(entry.idle_seconds / (24 * 60 * 60)) + "d";
    }
    out += " ";
    append_padded(out, idle, 6);
  }
  out += " ";
  if (entry.logon_time >= 0) {
    const std::time_t t = static_cast<std::time_t>(entry.logon_time);
    std::tm local{};
    localtime_s(&local, &t);
    char buf[32];
    strftime(buf, sizeof(buf), "%b %d %H:%M", &local);
    out += buf;
  }
  while (!out.empty() && out.back() == ' ') out.pop_back();
  safePrintLn(out);
}

auto print_long_entry(const std::string& user, bool include_home_shell)
    -> void {
  safePrint("Login name: ");
  safePrint(user);
  if (user.size() < 28) safePrint(std::string(28 - user.size(), " "[0]));
  safePrint("In real life: ");
  std::string current = current_user_name();
  if (!current.empty() && user == current) {
    std::string domain = current_domain_name();
    if (!domain.empty())
      safePrint(" " + domain + "\\" + user);
    else
      safePrint(" " + user);
    safePrintLn("");
    if (include_home_shell) {
      safePrint("Directory: ");
      std::string dir = "C:/Users/" + user;
      safePrint(dir);
      if (dir.size() < 29) safePrint(std::string(29 - dir.size(), " "[0]));
      safePrint("Shell:  /usr/bin/bash");
      safePrintLn("");
    }
    safePrintLn("");
  } else {
    safePrintLn(" ???");
  }
}
}  // namespace

REGISTER_COMMAND(pinky, "pinky", "pinky [OPTION]... [USER]...",
                 "A lightweight finger-compatible user information tool.",
                 "pinky\npinky -f\npinky -l USER", "finger, who, users",
                 "WinuxCmd", "Copyright © 2026 WinuxCmd", PINKY_OPTIONS) {
  bool long_format = ctx.get<bool>("-l", false);
  bool include_heading = !ctx.get<bool>("-f", false);
  bool include_fullname = !ctx.get<bool>("-w", false);
  bool include_idle = true;
  bool include_where = true;
  if (ctx.get<bool>("-i", false)) {
    include_fullname = false;
    include_where = false;
  }
  if (ctx.get<bool>("-q", false)) {
    include_fullname = false;
    include_where = false;
    include_idle = false;
  }
  bool include_home_shell = !ctx.get<bool>("-b", false);
  // [DIFFERS] - project/plan files do not exist on Windows
  (void)ctx.get<bool>("-h", false);
  (void)ctx.get<bool>("-p", false);
  (void)ctx.get<bool>("-s", false);
  (void)ctx.get<bool>("--lookup", false);

  if (long_format) {
    if (ctx.positionals.empty()) {
      safeErrorPrintLn(
          "pinky: no username specified; at least one must be specified when "
          "using -l");
      return 1;
    }
    for (auto arg : ctx.positionals)
      print_long_entry(std::string(arg), include_home_shell);
    return 0;
  }

  print_short_heading(include_heading, include_fullname, include_idle,
                      include_where);
  // [GNU] One row per login entry; with USER arguments, only entries
  // for those users (an unknown name simply matches nothing).
  // [DIFFERS] The Where column stays empty: console sessions have no
  // remote host (ut_host is empty for them in GNU output too).
  for (const auto& entry : session_entries()) {
    if (!ctx.positionals.empty()) {
      bool wanted = false;
      for (auto arg : ctx.positionals) {
        if (entry.user == std::string(arg)) wanted = true;
      }
      if (!wanted) continue;
    }
    print_short_entry(entry, include_fullname, include_idle);
  }
  return 0;
}
