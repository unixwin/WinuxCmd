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
 *  - File: id.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for id.
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

auto constexpr ID_OPTIONS = std::array{
    OPTION("-a", "", "ignore, for compatibility with other versions",
           BOOL_TYPE),
    OPTION("-g", "--group", "print only the effective group ID", BOOL_TYPE),
    OPTION("-G", "--groups", "print all group IDs", BOOL_TYPE),
    OPTION("-n", "--name", "print a name instead of a number", BOOL_TYPE),
    OPTION("-r", "--real", "print the real ID instead of the effective ID",
           BOOL_TYPE),
    OPTION("-u", "--user", "print only the effective user ID", BOOL_TYPE),
    OPTION("-Z", "--context",
           "print only the security context (not implemented)", BOOL_TYPE)
    // --zero (not implemented)
};

namespace id_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool print_group = false;
  bool print_groups = false;
  bool print_name = false;
  bool print_user = false;
  SmallVector<std::string, 64> users;
};

auto build_config(const CommandContext<ID_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.print_group =
      ctx.get<bool>("--group", false) || ctx.get<bool>("-g", false);
  cfg.print_groups =
      ctx.get<bool>("--groups", false) || ctx.get<bool>("-G", false);
  cfg.print_name = ctx.get<bool>("--name", false) || ctx.get<bool>("-n", false);
  cfg.print_user = ctx.get<bool>("--user", false) || ctx.get<bool>("-u", false);

  for (auto arg : ctx.positionals) {
    cfg.users.push_back(std::string(arg));
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  // Get current user info
  WCHAR username[256];
  DWORD username_size = 256;

  if (!GetUserNameW(username, &username_size)) {
    return 1;
  }

  std::wstring ws(username);
  std::string user_str(ws.begin(), ws.end());

  // Windows doesn't have POSIX UIDs/GIDs, so we use SIDs
  // For simplicity, we'll just print the username

  if (cfg.print_user) {
    // Print uid=0(username) format when -u option is used
    safePrint("uid=0(");
    safePrint(user_str);
    safePrintLn(")");
  } else if (cfg.print_group) {
    safePrintLn("unknown");  // Windows doesn't have POSIX groups
  } else if (cfg.print_groups) {
    safePrint(user_str);
    safePrint(" ");   // Primary group
    safePrintLn("");  // No additional groups on Windows
  } else {
    // Print all info
    safePrint("uid=0(");
    safePrint(user_str);
    safePrint(") gid=0(");
    safePrint(user_str);
    safePrint(") groups=0(");
    safePrint(user_str);
    safePrintLn(")");
  }

  return 0;
}

}  // namespace id_pipeline

REGISTER_COMMAND(
    id, "id", "id [OPTION]... [USER]",
    "Print user and group information for the specified USER,\n"
    "or (when USER omitted) for the current user.\n"
    "\n"
    "Note: This is a Windows implementation. Windows doesn't have\n"
    "POSIX UIDs/GIDs, so this command provides limited functionality.\n"
    "It mainly displays the username.",
    "  id\n"
    "  id -u\n"
    "  id -g\n"
    "  id -G",
    "groups(1), whoami(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    ID_OPTIONS) {
  using namespace id_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"id");
    return 1;
  }

  return run(*cfg_result);
}
