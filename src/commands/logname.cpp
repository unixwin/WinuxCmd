// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for logname command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// logname has no user-facing options beyond --help/--version.
// The framework requires option_count > 0, so a placeholder is used.
auto constexpr LOGNAME_OPTIONS =
    std::array{OPTION("", "", "print user's login name")};

REGISTER_COMMAND(
    logname,
    /* name */
    "logname",

    /* synopsis */
    "logname",
    "Print the name of the current user.\n"
    "\n"
    "Print the login name of the user that is currently logged in.\n"
    "On Windows, this returns the current Windows username.",
    "  logname",

    /* see also */
    "whoami(1), id(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    LOGNAME_OPTIONS) {
  namespace cp = core::pipeline;

  // Get current username
  wchar_t username[UNLEN + 1];
  DWORD size = UNLEN + 1;

  if (GetUserNameW(username, &size)) {
    std::wstring wusername(username);
    std::string utf8_username = wstring_to_utf8(wusername);
    safePrintLn(utf8_username);
    return 0;
  } else {
    safeErrorPrint("logname: failed to get username\n");
    return 1;
  }
}
