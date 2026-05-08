/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: logname.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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

auto constexpr LOGNAME_OPTIONS =
    std::array{OPTION("", "", "print user's login name", STRING_TYPE)};

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
