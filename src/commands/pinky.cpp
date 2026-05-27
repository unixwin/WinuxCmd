/*
 *  Copyright © 2026 WinuxCmd
 */
#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;
import container;

auto constexpr PINKY_OPTIONS =
    std::array{OPTION("-l", "", "long format", BOOL_TYPE),
               OPTION("-b", "", "omit host and user info", BOOL_TYPE),
               OPTION("-f", "", "omit the line header column", BOOL_TYPE),
               OPTION("-w", "", "omit the user's full name", BOOL_TYPE),
               OPTION("-i", "", "omit the user's idle time", BOOL_TYPE),
               OPTION("-p", "", "omit the user's login time", BOOL_TYPE),
               OPTION("-s", "", "short format (default)", BOOL_TYPE),
               OPTION("-q", "", "omit the user's home directory and shell",
                      BOOL_TYPE)};

REGISTER_COMMAND(pinky,
                 /* cmd_name */ "pinky",
                 /* cmd_synopsis */ "pinky [OPTION] [USER]...",
                 /* cmd_desc */ "A lightweight 'finger' client.",
                 /* examples */ "pinky\npinky -s john\npinky -l john",
                 /* see_also */ "finger",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ PINKY_OPTIONS) {
  bool long_format = ctx.get<bool>("-l", false);
  bool omit_bsh = ctx.get<bool>("-b", false);
  bool omit_header = ctx.get<bool>("-f", false);
  bool omit_name = ctx.get<bool>("-w", false);
  bool omit_idle = ctx.get<bool>("-i", false);
  bool omit_login = ctx.get<bool>("-p", false);
  bool short_format = ctx.get<bool>("-s", false);
  bool omit_dir_shell = ctx.get<bool>("-q", false);

  // Get current time
  SYSTEMTIME st;
  GetLocalTime(&st);
  char time_buf[32];
  sprintf_s(time_buf, "%02d:%02d", st.wHour, st.wMinute);

  auto get_user_info = [&](const std::string& user) {
    if (long_format) {
      safePrint("Login: " + user + "\t");
      if (!omit_name) {
        safePrint("Name: " + user + "\t");
      }
      if (!omit_bsh) {
        safePrint("\nDirectory: C:\\Users\\" + user);
        safePrint("\nShell: C:\\Windows\\System32\\cmd.exe");
      }
      safePrint("\n");
    } else {
      // Short format (default)
      safePrint(user);
      if (!omit_name) {
        safePrint(" " + user);
      }
      safePrint(" pts/0");
      if (!omit_login) {
        safePrint(" " + std::string(time_buf));
      }
      if (!omit_idle) {
        safePrint("  ");
      }
      safePrint("\n");
    }
  };

  if (ctx.positionals.empty()) {
    wchar_t username[UNLEN + 1];
    DWORD size = UNLEN + 1;
    if (GetUserNameW(username, &size)) {
      get_user_info(wstring_to_utf8(username));
    }
  } else {
    for (const auto& user : ctx.positionals) {
      get_user_info(std::string(user));
    }
  }

  return 0;
}
