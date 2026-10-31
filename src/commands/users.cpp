// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for users.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <wtsapi32.h>  // For WTSEnumerateSessionsW, WTSQuerySessionInformationW

#include "core/command_macros.h"

#pragma comment(lib, "wtsapi32.lib")

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr USERS_OPTIONS = std::array{
    // [EXT] option
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
  // [GNU] coreutils users.c: one login name per USER_PROCESS entry,
  // sorted, space-separated on a single line; duplicate names are
  // kept (one per session). The WTS session table is the Windows
  // source of login entries (simulation layer, niu #758).
  std::vector<std::string> names;

  PWTS_SESSION_INFOW pSessionInfo = nullptr;
  DWORD sessionCount = 0;

  if (WTSEnumerateSessionsW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &pSessionInfo,
                            &sessionCount)) {
    for (DWORD i = 0; i < sessionCount; ++i) {
      if (pSessionInfo[i].State != WTSActive &&
          pSessionInfo[i].State != WTSDisconnected) {
        continue;
      }

      LPWSTR buffer = nullptr;
      DWORD bufferSize = 0;
      if (WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE,
                                      pSessionInfo[i].SessionId, WTSUserName,
                                      &buffer, &bufferSize) &&
          buffer != nullptr && bufferSize > sizeof(wchar_t)) {
        std::string user = wstring_to_utf8(std::wstring(buffer));
        if (!user.empty()) {
          names.push_back(std::move(user));
        }
        WTSFreeMemory(buffer);
      }
    }
    WTSFreeMemory(pSessionInfo);
  }

  if (names.empty()) {
    // Simulation face: with no enumerable login sessions, report the
    // current interactive user, as this command has always done.
    WCHAR username[256];
    DWORD username_size = 256;
    if (!GetUserNameW(username, &username_size)) {
      return 1;
    }
    names.push_back(wstring_to_utf8(std::wstring(username)));
  }

  std::sort(names.begin(), names.end());

  std::string line;
  for (size_t i = 0; i < names.size(); ++i) {
    if (i > 0) line += " ";
    line += names[i];
  }
  safePrintLn(line);

  return 0;
}

}  // namespace users_pipeline

REGISTER_COMMAND(
    users, "users", "users [OPTION]...",
    "Print the user names of users currently logged in to the current host.",
    "  users", "who(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    USERS_OPTIONS) {
  using namespace users_pipeline;

  // [GNU] users.c takes at most one operand (the utmp FILE).
  if (ctx.positionals.size() > 1) {
    return cp::report_extra_operand("users", ctx.positionals[1]);
  }
  if (ctx.positionals.size() == 1) {
    // [DIFFERS] The FILE operand is accepted but its records are not
    // parsed: the session table replaces utmp on Windows (simulation
    // layer, niu #758). A missing file is still the GNU error.
    std::error_code ec;
    if (!std::filesystem::exists(ctx.positionals.front(), ec)) {
      cp::report_custom_error(
          L"users", utf8_to_wstring(ctx.positionals.front() +
                                    ": No such file or directory"));
      return 1;
    }
  }

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"users");
    return 1;
  }

  return run(*cfg_result);
}
