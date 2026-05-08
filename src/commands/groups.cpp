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
 *  - File: groups.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for groups.
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

auto constexpr GROUPS_OPTIONS =
    std::array{OPTION("", "", "print the groups a user is in", STRING_TYPE)};

namespace groups_pipeline {
namespace cp = core::pipeline;

struct Config {
  SmallVector<std::string, 64> users;
};

auto build_config(const CommandContext<GROUPS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  for (auto arg : ctx.positionals) {
    cfg.users.push_back(std::string(arg));
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  // Get current user if no users specified
  std::string user_str;

  if (cfg.users.empty()) {
    WCHAR username[256];
    DWORD username_size = 256;

    if (!GetUserNameW(username, &username_size)) {
      return 1;
    }

    std::wstring ws(username);
    user_str = std::string(ws.begin(), ws.end());
  } else {
    user_str = cfg.users[0];
  }

  // Windows doesn't have POSIX groups in the same way
  // For simplicity, we'll just show the user as being in their own "group"
  safePrintLn(user_str);

  return 0;
}

}  // namespace groups_pipeline

REGISTER_COMMAND(
    groups, "groups", "groups [OPTION]... [USERNAME]...",
    "Print a list of the groups a user is in.\n"
    "\n"
    "Note: This is a Windows implementation. Windows doesn't have\n"
    "POSIX groups in the same way. This command is provided for\n"
    "compatibility and shows limited information.",
    "  groups\n"
    "  groups username",
    "id(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", GROUPS_OPTIONS) {
  using namespace groups_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"groups");
    return 1;
  }

  return run(*cfg_result);
}
