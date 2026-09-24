// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for groups.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#include "pch/pch.h"
// include other header after pch.h
#include <lm.h>  // For NetUserGetGroups, NetApiBufferFree

#include "core/command_macros.h"

#pragma comment(lib, "netapi32.lib")

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr GROUPS_OPTIONS =
    // [GNU]
    std::array{OPTION("", "", "print the groups a user is in", STRING_TYPE)};

namespace groups_pipeline {
namespace cp = core::pipeline;

struct Config {
  SmallVector<std::string, 64> users;
};

// See id.cpp/stat.cpp: the Windows well-known "None" group has no POSIX
// equivalent and is treated as unresolvable (#1024).
auto is_none_group_name(std::string_view name) -> bool {
  return name.size() == 4 &&
         std::tolower(static_cast<unsigned char>(name[0])) == 'n' &&
         std::tolower(static_cast<unsigned char>(name[1])) == 'o' &&
         std::tolower(static_cast<unsigned char>(name[2])) == 'n' &&
         std::tolower(static_cast<unsigned char>(name[3])) == 'e';
}

auto build_config(const CommandContext<GROUPS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  for (auto arg : ctx.positionals) {
    cfg.users.push_back(std::string(arg));
  }

  return cfg;
}

// [GNU] groups.c prints one line: space-separated group names, prefixed
// with "user : " only when a username argument was given.  Collect the
// names here; run() does the formatting.
auto collect_user_groups(const std::string& user_str,
                         std::vector<std::string>& out) -> int {
  // Convert UTF-8 username to wide string for the Win32 API
  std::wstring wuser = utf8_to_wstring(user_str);

  LPBYTE raw = nullptr;
  DWORD entries = 0;
  DWORD total = 0;

  DWORD status = NetUserGetGroups(nullptr, wuser.c_str(), 0, &raw,
                                  MAX_PREFERRED_LENGTH, &entries, &total);

  if (status != NERR_Success) {
    safeErrorPrintLn("groups: " + user_str + ": " +
                     win32_posix_error_text(status));
    if (raw) NetApiBufferFree(raw);
    return 1;
  }

  if (entries == 0) {
    // User has no groups; still a valid result
    if (raw) NetApiBufferFree(raw);
    return 0;
  }

  auto* groups = reinterpret_cast<GROUP_USERS_INFO_0*>(raw);
  for (DWORD i = 0; i < entries; ++i) {
    std::wstring wname = groups[i].grui0_name ? groups[i].grui0_name : L"";
    std::string name = wstring_to_utf8(wname);
    if (name.empty()) {
      continue;
    }
    // [GNU] The default primary group on Windows resolves to a well-known
    // group literally named "None" which has no POSIX equivalent; print
    // its numeric id like an unresolvable gid (#1024).
    if (is_none_group_name(name)) {
      if (auto account = win32_lookup_account(wname);
          account && !account->id.empty()) {
        out.push_back(account->id);
      }
      continue;
    }
    out.push_back(std::move(name));
  }

  if (raw) NetApiBufferFree(raw);
  return 0;
}

auto get_user_groups(const std::string& user_str, bool with_prefix) -> int {
  std::vector<std::string> names;
  int result = collect_user_groups(user_str, names);
  if (result != 0) return result;
  std::string line;
  if (with_prefix) line = user_str + " : ";
  for (size_t i = 0; i < names.size(); ++i) {
    if (i > 0) line += " ";
    line += names[i];
  }
  safePrintLn(line);
  return 0;
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
    user_str = wstring_to_utf8(ws);
    return get_user_groups(user_str, /*with_prefix=*/false);
  }

  // [GNU] with username arguments, each line is prefixed "user : ".
  int exit_code = 0;
  for (const auto& user : cfg.users) {
    int result = get_user_groups(user, /*with_prefix=*/true);
    if (result != 0) {
      exit_code = result;
    }
  }
  return exit_code;
}

}  // namespace groups_pipeline

REGISTER_COMMAND(groups, "groups", "groups [OPTION]... [USERNAME]...",
                 "Print a list of the groups a user is in.",
                 "  groups\n"
                 "  groups username",
                 "id(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 GROUPS_OPTIONS) {
  using namespace groups_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"groups");
    return 1;
  }
  return run(*cfg_result);
}
