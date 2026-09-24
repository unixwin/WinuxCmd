// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pkill.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PKILL_OPTIONS = std::array{
    // [EXT]
    OPTION("-f", "--full", "match against full command line"),
    // [EXT]
    OPTION("-i", "--ignore-case", "match case insensitively"),
    // [EXT]
    OPTION("-x", "--exact", "match the whole name or command line"),
    // [EXT]
    OPTION("-e", "--echo", "display what is killed"),
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

namespace pkill_pipeline {

struct Config {
  bool full = false;
  bool ignore_case = false;
  bool exact = false;
  bool echo = false;
  bool check_only = false;
  std::wstring pattern;
  std::optional<std::wregex> regex;
};

auto build_config(const CommandContext<PKILL_OPTIONS.size()>& ctx)
    -> std::optional<Config> {
  Config cfg;
  cfg.full = ctx.get<bool>("-f", false) || ctx.get<bool>("--full", false);
  cfg.ignore_case =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--ignore-case", false);
  cfg.exact = ctx.get<bool>("-x", false) || ctx.get<bool>("--exact", false);
  cfg.echo = ctx.get<bool>("-e", false) || ctx.get<bool>("--echo", false);
  cfg.check_only = ctx.get<bool>("-0", false);

  if (ctx.positionals.size() != 1) return std::nullopt;
  cfg.pattern = utf8_to_wstring(std::string(ctx.positionals[0]));

  // Compile POSIX ERE pattern (ECMAScript grammar on MSVC).
  try {
    auto flags = std::regex_constants::ECMAScript;
    if (cfg.ignore_case) flags |= std::regex_constants::icase;
    std::wstring re_pattern = cfg.pattern;
    if (cfg.exact) re_pattern = L"^(?:" + re_pattern + L")$";
    cfg.regex.emplace(re_pattern, flags);
  } catch (const std::regex_error&) {
    // Leave regex as nullopt; caller will report the error.
  }

  return cfg;
}

auto matches(const Win32ProcessInfo& proc, const Config& cfg) -> bool {
  std::wstring haystack = cfg.full ? proc.command_line : proc.name;
  if (!cfg.full) haystack = win32_basename_without_exe(haystack);
  if (!cfg.regex) return false;
  return std::regex_search(haystack, *cfg.regex);
}

auto run(const Config& cfg) -> int {
  auto processes = enumerate_win32_processes(cfg.full);
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
      safeErrorPrintLn("pkill: " + std::to_string(proc.pid) + ": " +
                       result.message);
      ++failed;
      continue;
    }
    if (cfg.echo && !cfg.check_only) {
      safePrintLn(std::to_wstring(proc.pid) + L" " + proc.name);
    }
  }

  if (matched == 0) return 1;
  return failed == 0 ? 0 : 1;
}

}  // namespace pkill_pipeline

REGISTER_COMMAND(pkill, "pkill", "pkill [OPTION]... PATTERN",
                 "Signal processes selected by name or command line.\n"
                 "On Windows, TERM/KILL are mapped to Win32 termination.",
                 "  pkill -f worker-token\n"
                 "  pkill -0 explorer",
                 "pgrep(1), kill(1), killall(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PKILL_OPTIONS) {
  auto cfg = pkill_pipeline::build_config(ctx);
  if (!cfg) {
    safeErrorPrintLn("pkill: exactly one PATTERN is required");
    safeErrorPrintLn("Try 'pkill --help' for more information.");
    return 2;
  }
  if (!cfg->regex) {
    safeErrorPrintLn("pkill: invalid regular expression");
    return 2;
  }
  return pkill_pipeline::run(*cfg);
}
