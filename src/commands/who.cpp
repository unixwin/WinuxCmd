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
 *  - File: who.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for who.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr WHO_OPTIONS = std::array{
    OPTION("-a", "--all", "same as -b -d --login -p -r -t -T -u", BOOL_TYPE),
    OPTION("-b", "--boot", "time of last system boot", BOOL_TYPE),
    OPTION("-d", "--dead", "print dead processes", BOOL_TYPE),
    OPTION("-H", "--heading", "print line of column headings", BOOL_TYPE),
    OPTION("-l", "--login", "print system login processes", BOOL_TYPE),
    OPTION("-m", "", "only hostname and user associated with stdin", BOOL_TYPE),
    OPTION("-p", "--process", "print active processes spawned by init",
           BOOL_TYPE),
    OPTION("-q", "--count", "all login names and number of users logged on",
           BOOL_TYPE),
    OPTION("-r", "--runlevel", "print current runlevel", BOOL_TYPE),
    OPTION("-s", "--short", "print only name, line, and time", BOOL_TYPE),
    OPTION("-t", "--time", "print last system clock change", BOOL_TYPE),
    OPTION("-T", "", "add user's message status as +, - or ?", BOOL_TYPE),
    OPTION("-u", "--users", "list users logged in", BOOL_TYPE)};

namespace who_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool heading = false;
  bool short_format = false;
  bool count = false;
};

auto build_config(const CommandContext<WHO_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.heading = ctx.get<bool>("--heading", false) || ctx.get<bool>("-H", false);
  cfg.short_format =
      ctx.get<bool>("--short", false) || ctx.get<bool>("-s", false);
  cfg.count = ctx.get<bool>("--count", false) || ctx.get<bool>("-q", false);
  return cfg;
}

auto run(const Config& cfg) -> int {
  // Get current user
  WCHAR username[256];
  DWORD username_size = 256;

  if (!GetUserNameW(username, &username_size)) {
    return 1;
  }

  std::wstring ws(username);
  std::string user_str(ws.begin(), ws.end());

  // Get current time
  SYSTEMTIME st;
  GetLocalTime(&st);

  char time_buf[32];
  sprintf_s(time_buf, "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth, st.wDay,
            st.wHour, st.wMinute);

  if (cfg.heading) {
    safePrintLn("NAME     LINE         TIME             COMMENT");
  }

  if (cfg.count) {
    safePrint("# users=");
    safePrintLn("1");
  } else {
    if (cfg.short_format) {
      safePrint(user_str);
      safePrint(" pts/0  ");
      safePrintLn(time_buf);
    } else {
      safePrint(user_str);
      safePrint("  pts/0        ");
      safePrint(time_buf);
      safePrintLn("  (console)");
    }
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
    cp::report_error(cfg_result, L"who");
    return 1;
  }

  return run(*cfg_result);
}
