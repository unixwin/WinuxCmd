// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [DIFFERS] Windows has no Unix utmp records for these views.
    OPTION("-a", "--all", "same as -b -d --login -p -r -t -T -u", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-b", "--boot", "time of last system boot", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-d", "--dead", "print dead processes", BOOL_TYPE),
    OPTION("-H", "--heading", "print line of column headings", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-l", "--login", "print system login processes", BOOL_TYPE),
    // [GNU] Windows output already identifies the current stdin user.
    OPTION("-m", "", "only hostname and user associated with stdin", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-p", "--process", "print active processes spawned by init",
           BOOL_TYPE),
    OPTION("-q", "--count", "all login names and number of users logged on",
           BOOL_TYPE),
    // [DIFFERS]
    OPTION("-r", "--runlevel", "print current runlevel", BOOL_TYPE),
    OPTION("-s", "--short", "print only name, line, and time", BOOL_TYPE),
    // [DIFFERS]
    OPTION("-t", "--time", "print last system clock change", BOOL_TYPE),
    // [GNU] -T is the short alias of -w.
    OPTION("-T", "", "add user's message status as +, - or ?", BOOL_TYPE),
    // [GNU] Windows output already lists the current logged-in user.
    OPTION("-u", "--users", "list users logged in", BOOL_TYPE),
    OPTION("-w", "--mesg", "add user's message status as +, - or ?", BOOL_TYPE),
    // [DIFFERS] Windows who output has no hostname canonicalization field.
    OPTION("", "--lookup", "attempt to canonicalize hostnames via DNS",
           BOOL_TYPE),
    // [GNU] Alias for --mesg.
    OPTION("", "--message", "same as --mesg"),
    // [DIFFERS] Windows has no utmp terminal writability state.
    OPTION("", "--writable", "list only users with a writable terminal")};

namespace who_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool heading = false;
  bool short_format = false;
  bool count = false;
  bool mesg = false;
  bool users = false;
  std::optional<std::string> unsupported;
};

auto build_config(const CommandContext<WHO_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.heading = ctx.get<bool>("--heading", false) || ctx.get<bool>("-H", false);
  cfg.short_format =
      ctx.get<bool>("--short", false) || ctx.get<bool>("-s", false);
  cfg.count = ctx.get<bool>("--count", false) || ctx.get<bool>("-q", false);
  // [GNU] --message and -T forward to the --mesg behavior.
  cfg.mesg = ctx.get<bool>("--mesg", false) || ctx.get<bool>("-w", false) ||
             ctx.get<bool>("--message", false) || ctx.get<bool>("-T", false);
  // [GNU] -u/--users is the default Windows output.
  cfg.users = ctx.get<bool>("--users", false) || ctx.get<bool>("-u", false);
  // [GNU] -m: only hostname and user associated with stdin
  (void)ctx.get<bool>("-m", false);

  const auto reject = [&](std::string option) {
    if (!cfg.unsupported) cfg.unsupported = std::move(option);
  };
  if (ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false))
    reject("--all");
  if (ctx.get<bool>("-b", false) || ctx.get<bool>("--boot", false))
    reject("--boot");
  if (ctx.get<bool>("-d", false) || ctx.get<bool>("--dead", false))
    reject("--dead");
  if (ctx.get<bool>("-l", false) || ctx.get<bool>("--login", false))
    reject("--login");
  if (ctx.get<bool>("-p", false) || ctx.get<bool>("--process", false))
    reject("--process");
  if (ctx.get<bool>("-r", false) || ctx.get<bool>("--runlevel", false))
    reject("--runlevel");
  if (ctx.get<bool>("-t", false) || ctx.get<bool>("--time", false))
    reject("--time");
  if (ctx.get<bool>("--lookup", false)) reject("--lookup");
  if (ctx.get<bool>("--writable", false)) reject("--writable");
  if (cfg.unsupported) {
    return std::unexpected(
        winux::i18n::translate("command.who.error.unsupported_option",
                               "who: option is not supported on Windows: ") +
        *cfg.unsupported);
  }
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
  std::string user_str = wstring_to_utf8(ws);

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
    std::string mesg_status = cfg.mesg ? " + " : " - ";
    if (cfg.short_format) {
      safePrint(user_str);
      safePrint(" pts/0  ");
      safePrintLn(time_buf);
    } else {
      safePrint(user_str);
      safePrint(mesg_status);
      safePrint("pts/0        ");
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
