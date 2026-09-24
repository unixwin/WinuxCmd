// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for mkgroup.
/// @Version: 0.1.0
/// @License: MIT

#include <lm.h>

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "netapi32.lib")

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr MKGROUP_OPTIONS = std::array{
    // [EXT]
    OPTION("-c", "--current", "print groups from the current process token"),
    // [EXT]
    OPTION("-l", "--local", "enumerate local machine groups (default)")};

namespace mkgroup_pipeline {

auto field(std::string value) -> std::string {
  for (auto& ch : value) {
    if (ch == ':') ch = '_';
  }
  return value;
}

auto print_group_line(const Win32AccountInfo& account) -> void {
  const std::string name =
      field(account.name.empty() ? account.id : account.name);
  const std::string gid = account.id.empty() ? "0" : account.id;
  safePrintLn(name + ":*:" + gid + ":");
}

auto print_current() -> int {
  auto accounts = win32_current_token_accounts();
  if (!accounts) {
    safeErrorPrintLn("mkgroup: " + accounts.error());
    return 1;
  }
  if (accounts->size() <= 1) return 1;

  for (size_t i = 1; i < accounts->size(); ++i) {
    print_group_line((*accounts)[i]);
  }
  return 0;
}

auto print_local_groups() -> int {
  DWORD_PTR resume = 0;
  DWORD status = NERR_Success;
  bool any = false;

  do {
    LPBYTE raw = nullptr;
    DWORD entries = 0;
    DWORD total = 0;
    status = NetLocalGroupEnum(nullptr, 0, &raw, MAX_PREFERRED_LENGTH, &entries,
                               &total, &resume);

    if (status != NERR_Success && status != ERROR_MORE_DATA) {
      safeErrorPrintLn("mkgroup: NetLocalGroupEnum failed: " +
                       win32_posix_error_text(status));
      if (raw) NetApiBufferFree(raw);
      return 1;
    }

    auto* groups = reinterpret_cast<LOCALGROUP_INFO_0*>(raw);
    for (DWORD i = 0; i < entries; ++i) {
      std::wstring name = groups[i].lgrpi0_name ? groups[i].lgrpi0_name : L"";
      auto account = win32_lookup_account(name);
      Win32AccountInfo info = account.value_or(
          Win32AccountInfo{.id = "0", .name = wstring_to_utf8(name)});
      if (info.name.empty()) info.name = wstring_to_utf8(name);
      print_group_line(info);
      any = true;
    }
    if (raw) NetApiBufferFree(raw);
  } while (status == ERROR_MORE_DATA);

  return any ? 0 : 1;
}

auto run(const CommandContext<MKGROUP_OPTIONS.size()>& ctx) -> int {
  if (!ctx.positionals.empty()) {
    safeErrorPrintLn("mkgroup: unexpected operand '" +
                     std::string(ctx.positionals.front()) + "'");
    return 1;
  }
  const bool current =
      ctx.get<bool>("-c", false) || ctx.get<bool>("--current", false);
  return current ? print_current() : print_local_groups();
}

}  // namespace mkgroup_pipeline

REGISTER_COMMAND(mkgroup, "mkgroup", "mkgroup [OPTION]...",
                 "Generate group-like entries from Windows groups.",
                 "  mkgroup --current\n"
                 "  mkgroup --local",
                 "groups(1), id(1), mkpasswd(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", MKGROUP_OPTIONS) {
  return mkgroup_pipeline::run(ctx);
}
