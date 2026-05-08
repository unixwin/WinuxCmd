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
               OPTION("-b", "", "omit host and user info", BOOL_TYPE)};

REGISTER_COMMAND(pinky,
                 /* cmd_name */ "pinky",
                 /* cmd_synopsis */ "pinky [OPTION] [USER]...",
                 /* cmd_desc */ "A lightweight 'finger' client.",
                 /* examples */ "pinky\npinky john",
                 /* see_also */ "finger",
                 /* author */ "WinuxCmd",
                 /* copyright */ "Copyright © 2026 WinuxCmd",
                 /* options */ PINKY_OPTIONS) {
  bool long_format = ctx.get<bool>("-l", false);
  bool brief = ctx.get<bool>("-b", false);

  if (ctx.positionals.empty()) {
    // Current user
    wchar_t username[UNLEN + 1];
    DWORD size = UNLEN + 1;
    if (GetUserNameW(username, &size)) {
      std::string user = wstring_to_utf8(username);
      safePrintLn("Login: " + user + "        Name: " + user);
      safePrintLn("Directory: C:\\Users\\" + user);
      safePrintLn("Shell: C:\\Windows\\System32\\cmd.exe");
    }
  } else {
    for (const auto& user : ctx.positionals) {
      std::string user_str(user);
      safePrintLn("Login: " + user_str + "        Name: " + user_str);
      safePrintLn("Directory: C:\\Users\\" + user_str);
      safePrintLn("Shell: C:\\Windows\\System32\\cmd.exe");
    }
  }

  return 0;
}
