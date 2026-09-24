// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pgrep.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PGREP_OPTIONS = std::array{
    // [EXT]
    OPTION("-f", "--full", "match against full command line"),
    // [EXT]
    OPTION("-i", "--ignore-case", "match case insensitively"),
    // [EXT]
    OPTION("-l", "--list-name", "list PID and process name"),
    // [EXT]
    OPTION("-a", "--list-full", "list PID and full command line"),
    // [EXT]
    OPTION("-x", "--exact", "match the whole name or command line"),
    // [EXT]
    OPTION("-c", "--count", "print only a count of matching processes")};

namespace pgrep_pipeline {

struct Config {
  bool full = false;
  bool ignore_case = false;
  bool list_name = false;
  bool list_full = false;
  bool exact = false;
  bool count = false;
  std::wstring pattern;
  std::optional<std::wregex> regex;
};

auto matches(const Win32ProcessInfo& proc, const Config& cfg) -> bool {
  std::wstring haystack = cfg.full ? proc.command_line : proc.name;
  if (!cfg.full) {
    haystack = win32_basename_without_exe(haystack);
  }
  if (!cfg.regex) return false;
  return std::regex_search(haystack, *cfg.regex);
}

auto build_config(const CommandContext<PGREP_OPTIONS.size()>& ctx)
    -> std::optional<Config> {
  Config cfg;
  cfg.full = ctx.get<bool>("-f", false) || ctx.get<bool>("--full", false);
  cfg.ignore_case =
      ctx.get<bool>("-i", false) || ctx.get<bool>("--ignore-case", false);
  cfg.list_name =
      ctx.get<bool>("-l", false) || ctx.get<bool>("--list-name", false);
  cfg.list_full =
      ctx.get<bool>("-a", false) || ctx.get<bool>("--list-full", false);
  cfg.exact = ctx.get<bool>("-x", false) || ctx.get<bool>("--exact", false);
  cfg.count = ctx.get<bool>("-c", false) || ctx.get<bool>("--count", false);

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

auto run(const Config& cfg) -> int {
  auto processes = enumerate_win32_processes();
  const DWORD self = GetCurrentProcessId();
  std::vector<const Win32ProcessInfo*> matched;
  for (const auto& proc : processes) {
    if (proc.pid == self) continue;
    if (matches(proc, cfg)) matched.push_back(&proc);
  }

  if (cfg.count) {
    safePrintLn(std::to_string(matched.size()));
    return matched.empty() ? 1 : 0;
  }

  for (const auto* proc : matched) {
    if (cfg.list_full) {
      safePrintLn(std::to_wstring(proc->pid) + L" " + proc->command_line);
    } else if (cfg.list_name) {
      safePrintLn(std::to_wstring(proc->pid) + L" " + proc->name);
    } else {
      safePrintLn(std::to_wstring(proc->pid));
    }
  }

  return matched.empty() ? 1 : 0;
}

}  // namespace pgrep_pipeline

REGISTER_COMMAND(pgrep, "pgrep", "pgrep [OPTION]... PATTERN",
                 "Look up processes by name or command line.\n"
                 "The current pgrep process is excluded from matches.",
                 "  pgrep explorer\n"
                 "  pgrep -af powershell",
                 "ps(1), pkill(1), pidof(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PGREP_OPTIONS) {
  using namespace pgrep_pipeline;

  auto cfg = build_config(ctx);
  if (!cfg) {
    safeErrorPrintLn("pgrep: exactly one PATTERN is required");
    safeErrorPrintLn("Try 'pgrep --help' for more information.");
    return 2;
  }
  if (!cfg->regex) {
    safeErrorPrintLn("pgrep: invalid regular expression");
    return 2;
  }
  return run(*cfg);
}
