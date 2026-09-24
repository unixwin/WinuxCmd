/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
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
  return 0;
}
