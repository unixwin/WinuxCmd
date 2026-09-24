// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for mkpasswd.
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

auto constexpr MKPASSWD_OPTIONS = std::array{
    // [EXT]
    OPTION("-c", "--current", "print only the current process user"),
    // [EXT] [DIFFERS] GNU mkpasswd: -l means password length; here it means
    // local users
    OPTION("-l", "--local", "enumerate local machine users (default)"),
    // [EXT]
    OPTION("-p", "--path-prefix", "home directory prefix", STRING_TYPE),
    // [EXT] [DIFFERS] GNU mkpasswd: -s means salt; here it means login shell
    OPTION("-s", "--shell", "login shell path", STRING_TYPE),
    // [DIFFERS] GNU mkpasswd: read password from file descriptor; not
    // applicable on Windows
    OPTION("-P", "--password-fd", "read password from file descriptor",
           STRING_TYPE),
    // [DIFFERS] GNU mkpasswd: encrypt method (des,md5,sha-256,sha-512); not
    // applicable on Windows
    OPTION("-S", "--method", "encrypt method (not supported on Windows)",
           STRING_TYPE)};

namespace mkpasswd_pipeline {

struct Config {
  bool current = false;
  std::string path_prefix = "/home";
  std::string shell = "/bin/sh";
};

auto field(std::string value) -> std::string {
  for (auto& ch : value) {
    if (ch == ':') ch = '_';
  }
  return value;
}

auto print_passwd_line(const Win32AccountInfo& account, const Config& cfg)
    -> void {
  const std::string name =
      field(account.name.empty() ? account.id : account.name);
  const std::string uid = account.id.empty() ? "0" : account.id;
  const std::string gid = uid;
  safePrintLn(name + ":*:" + uid + ":" + gid + ":Windows user:" +
              cfg.path_prefix + "/" + name + ":" + cfg.shell);
}

auto print_current(const Config& cfg) -> int {
  auto accounts = win32_current_token_accounts();
  if (!accounts || accounts->empty()) {
    safeErrorPrintLn("mkpasswd: " +
                     (accounts ? std::string("cannot query current user")
                               : accounts.error()));
    return 1;
  }
  print_passwd_line(accounts->front(), cfg);
  return 0;
}

auto print_local_users(const Config& cfg) -> int {
  DWORD resume = 0;
  DWORD status = NERR_Success;
  bool any = false;

  do {
    LPBYTE raw = nullptr;
    DWORD entries = 0;
    DWORD total = 0;
    status = NetUserEnum(nullptr, 0, FILTER_NORMAL_ACCOUNT, &raw,
                         MAX_PREFERRED_LENGTH, &entries, &total, &resume);

    if (status != NERR_Success && status != ERROR_MORE_DATA) {
      safeErrorPrintLn("mkpasswd: NetUserEnum failed: " +
                       win32_posix_error_text(status));
      if (raw) NetApiBufferFree(raw);
      return 1;
    }

    auto* users = reinterpret_cast<USER_INFO_0*>(raw);
    for (DWORD i = 0; i < entries; ++i) {
      std::wstring name = users[i].usri0_name ? users[i].usri0_name : L"";
      auto account = win32_lookup_account(name);
      Win32AccountInfo info = account.value_or(
          Win32AccountInfo{.id = "0", .name = wstring_to_utf8(name)});
      if (info.name.empty()) info.name = wstring_to_utf8(name);
      print_passwd_line(info, cfg);
      any = true;
    }
    if (raw) NetApiBufferFree(raw);
  } while (status == ERROR_MORE_DATA);

  return any ? 0 : 1;
}

auto run(const CommandContext<MKPASSWD_OPTIONS.size()>& ctx) -> int {
  Config cfg;
  cfg.current = ctx.get<bool>("-c", false) || ctx.get<bool>("--current", false);
  // -l/--local: always lists local users (default behavior)
  (void)ctx.has("--local");
  cfg.path_prefix = ctx.get<std::string>(
      "-p", ctx.get<std::string>("--path-prefix", "/home"));
  cfg.shell =
      ctx.get<std::string>("-s", ctx.get<std::string>("--shell", "/bin/sh"));

  // [DIFFERS] -P/--password-fd: GNU mkpasswd reads password from fd; not
  // applicable on Windows
  if (ctx.has("-P") || ctx.has("--password-fd")) {
    safeErrorPrintLn(
        "mkpasswd: --password-fd is not supported on Windows [DIFFERS from GNU "
        "mkpasswd]");
    return 1;
  }
  // [DIFFERS] -S/--method: GNU mkpasswd uses encryption method; not applicable
  // on Windows
  if (ctx.has("-S") || ctx.has("--method")) {
    safeErrorPrintLn(
        "mkpasswd: --method is not supported on Windows [DIFFERS from GNU "
        "mkpasswd]");
    return 1;
  }

  if (!ctx.positionals.empty()) {
    safeErrorPrintLn("mkpasswd: unexpected operand '" +
                     std::string(ctx.positionals.front()) + "'");
    return 1;
  }

  return cfg.current ? print_current(cfg) : print_local_users(cfg);
}

}  // namespace mkpasswd_pipeline

REGISTER_COMMAND(
    mkpasswd, "mkpasswd",
    "mkpasswd [-c] [-l] [-p PREFIX] [-s SHELL] [-P FD] [-S METHOD]",
    "Generate passwd-like entries from Windows accounts.",
    "  mkpasswd --current\n"
    "  mkpasswd --path-prefix /home --shell /bin/sh\n"
    "  mkpasswd -P 3 -S sha-512  # [DIFFERS] not supported",
    "id(1), whoami(1), mkgroup(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    MKPASSWD_OPTIONS) {
  return mkpasswd_pipeline::run(ctx);
}
