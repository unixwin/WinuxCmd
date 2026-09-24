// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for pldd.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PLDD_OPTIONS = std::array{
    // [EXT]
    OPTION("-n", "--name", "print module basenames instead of paths")};

namespace pldd_pipeline {

auto run(const CommandContext<PLDD_OPTIONS.size()>& ctx) -> int {
  if (ctx.positionals.size() != 1) {
    safeErrorPrintLn("pldd: exactly one PID is required");
    safeErrorPrintLn("Try 'pldd --help' for more information.");
    return 1;
  }

  auto pid = win32_parse_pid(ctx.positionals.front());
  if (!pid) {
    safeErrorPrintLn("pldd: invalid process ID '" +
                     std::string(ctx.positionals.front()) + "'");
    return 1;
  }

  auto modules = enumerate_win32_modules(*pid);
  if (!modules) {
    safeErrorPrintLn("pldd: " + std::to_string(*pid) + ": " + modules.error());
    return 1;
  }

  bool names_only =
      ctx.get<bool>("-n", false) || ctx.get<bool>("--name", false);
  for (const auto& module : *modules) {
    safePrintLn(names_only ? module.name : module.path);
  }
  return 0;
}

}  // namespace pldd_pipeline

REGISTER_COMMAND(pldd, "pldd", "pldd [OPTION]... PID",
                 "List DLLs and executable modules loaded by a process.",
                 "  pldd 1234\n"
                 "  pldd --name 1234",
                 "lsof(1), ps(1), pgrep(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", PLDD_OPTIONS) {
  return pldd_pipeline::run(ctx);
}
