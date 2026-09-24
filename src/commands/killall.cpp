// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for killall.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr KILLALL_OPTIONS = std::array{
    // [EXT]
    OPTION("-e", "--exact", "require an exact process-name match"),
    // [EXT]
    OPTION("-I", "--ignore-case", "match case insensitively"),
    // [EXT]
    OPTION("-q", "--quiet", "do not complain if no processes were killed"),
    // [EXT]
    OPTION("-v", "--verbose", "report successful signals"),
    // [EXT]
    OPTION("-0", "", "only check for matching processes"),
    // [GNU] Signal spellings forward to the Win32 termination operation.
    OPTION("-9", "", "send SIGKILL; mapped to Win32 termination"),
    // [EXT]
    OPTION("-TERM", "", "send SIGTERM; mapped to Win32 termination"),
    // [EXT]
    OPTION("-SIGTERM", "", "send SIGTERM; mapped to Win32 termination"),
    // [EXT]
    OPTION("-KILL", "", "send SIGKILL; mapped to Win32 termination"),
    // [EXT]
    OPTION("-SIGKILL", "", "send SIGKILL; mapped to Win32 termination")};

namespace killall_pipeline {

struct Config {
  bool exact = false;
  bool ignore_case = false;
  bool quiet = false;
  bool verbose = false;
  bool check_only = false;
  std::vector<std::wstring> names;
};

auto build_config(const CommandContext<KILLALL_OPTIONS.size()>& ctx)
    -> std::optional<Config> {
  Config cfg;
  cfg.exact = ctx.get<bool>("-e", false) || ctx.get<bool>("--exact", false);
  cfg.ignore_case =
      ctx.get<bool>("-I", false) || ctx.get<bool>("--ignore-case", false);
  cfg.quiet = ctx.get<bool>("-q", false) || ctx.get<bool>("--quiet", false);
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  cfg.check_only = ctx.get<bool>("-0", false);

  if (ctx.positionals.empty()) return std::nullopt;
  for (auto arg : ctx.positionals) {
    cfg.names.push_back(
        win32_basename_without_exe(utf8_to_wstring(std::string(arg))));
  }
  return cfg;
}

auto one_name_matches(std::wstring haystack, std::wstring needle,
                      const Config& cfg) -> bool {
  haystack = win32_basename_without_exe(std::move(haystack));
  if (cfg.ignore_case) {
    haystack = ascii_lower_copy(haystack);
    needle = ascii_lower_copy(needle);
  }
  if (cfg.exact) return haystack == needle;
  return haystack.find(needle) != std::wstring::npos;
}

auto matches(const Win32ProcessInfo& proc, const Config& cfg) -> bool {
  return std::ranges::any_of(cfg.names, [&](const auto& name) {
    return one_name_matches(proc.name, name, cfg);
  });
}

auto run(const Config& cfg) -> int {
  auto processes = enumerate_win32_processes(false);
  const DWORD self = GetCurrentProcessId();
  int matched = 0;
  int failed = 0;

  for (const auto& proc : processes) {
    if (proc.pid == 0 || proc.pid == 4 || proc.pid == self) continue;
    if (!matches(proc, cfg)) continue;

    ++matched;
    auto result = cfg.check_only ? win32_check_process(proc.pid)
                                 : win32_terminate_process(proc.pid);
    if (!result.ok) {
      if (!cfg.quiet) {
        safeErrorPrintLn("killall: " + std::to_string(proc.pid) + ": " +
                         result.message);
      }
      ++failed;
      continue;
    }
    if (cfg.verbose && !cfg.check_only) {
      safePrintLn(std::to_wstring(proc.pid) + L" " + proc.name);
    }
  }

  if (matched == 0) {
    if (!cfg.quiet) safeErrorPrintLn("killall: no process found");
    return 1;
  }
  return failed == 0 ? 0 : 1;
}

}  // namespace killall_pipeline

REGISTER_COMMAND(killall, "killall", "killall [OPTION]... NAME...",
                 "Signal processes by executable name.\n"
                 "On Windows, TERM/KILL are mapped to Win32 termination.",
                 "  killall notepad\n"
                 "  killall -0 explorer",
                 "kill(1), pkill(1), pgrep(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", KILLALL_OPTIONS) {
  auto cfg = killall_pipeline::build_config(ctx);
  if (!cfg) {
    safeErrorPrintLn("killall: missing process name");
    safeErrorPrintLn("Try 'killall --help' for more information.");
    return 1;
  }
  return killall_pipeline::run(*cfg);
}
