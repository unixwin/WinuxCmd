// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pidof.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PIDOF_OPTIONS =
    // [EXT]
    std::array{OPTION("-x", "--exact", "exact match only")};

namespace pidof_pipeline {

auto process_matches(const Win32ProcessInfo& proc, std::wstring_view wanted,
                     bool exact) -> bool {
  auto name = win32_basename_without_exe(proc.name);
  if (ascii_iequals(name, wanted) || ascii_iequals(proc.name, wanted)) {
    return true;
  }
  if (exact) return false;

  auto command = win32_basename_without_exe(proc.command_line);
  return ascii_iequals(command, wanted) ||
         ascii_contains_ci(proc.command_line, wanted);
}

auto run(const CommandContext<PIDOF_OPTIONS.size()>& ctx) -> int {
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("pidof: missing program name");
    safeErrorPrintLn("Try 'pidof --help' for more information.");
    return 1;
  }

  bool exact = ctx.get<bool>("-x", false) || ctx.get<bool>("--exact", false);
  auto processes = enumerate_win32_processes(exact);
  DWORD self = GetCurrentProcessId();
  std::vector<DWORD> pids;

  for (auto raw_name : ctx.positionals) {
    std::wstring wanted =
        win32_basename_without_exe(utf8_to_wstring(std::string(raw_name)));
    for (const auto& proc : processes) {
      if (proc.pid == self) continue;
      if (process_matches(proc, wanted, exact)) pids.push_back(proc.pid);
    }
  }

  if (pids.empty()) return 1;

  for (size_t i = 0; i < pids.size(); ++i) {
    if (i != 0) safePrint(" ");
    safePrint(std::to_string(pids[i]));
  }
  safePrintLn("");
  return 0;
}

}  // namespace pidof_pipeline

REGISTER_COMMAND(pidof, "pidof", "pidof [OPTION]... PROGRAM...",
                 "Find the process ID of a running program.",
                 "  pidof explorer\n"
                 "  pidof -x powershell",
                 "pgrep(1), ps(1), kill(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PIDOF_OPTIONS) {
  return pidof_pipeline::run(ctx);
}
