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
 *  - File: users.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for users.
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

auto constexpr USERS_OPTIONS = std::array{
    OPTION("", "", "print who is currently logged in", STRING_TYPE)
    // users has no options
};

namespace users_pipeline {
namespace cp = core::pipeline;

struct Config {
  // No options
};

auto build_config(const CommandContext<USERS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
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

  safePrintLn(user_str);

  return 0;
}

}  // namespace users_pipeline

REGISTER_COMMAND(
    users, "users", "users [OPTION]...",
    "Print the user names of users currently logged in to the current host.\n"
    "\n"
    "Note: This is a Windows implementation. Windows doesn't have\n"
    "the same multi-user concept as Unix, so this command displays\n"
    "only the current interactive user.",
    "  users", "who(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    USERS_OPTIONS) {
  using namespace users_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"users");
    return 1;
  }

  return run(*cfg_result);
}
