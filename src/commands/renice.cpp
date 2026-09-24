// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for renice.
/// @Version: 0.1.0
/// @License: MIT

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr RENICE_OPTIONS =
    // [EXT]
    std::array{
        OPTION("-n", "--priority", "set niceness value", STRING_TYPE),
        // [EXT]
        OPTION("-p", "--pid", "interpret operands as process IDs"),
        // [DIFFERS] GNU renice: process group; not supported on Windows
        OPTION("-g", "--pgrp", "interpret operands as process group IDs"),
        // [DIFFERS] GNU renice: user-based renice; not supported on Windows
        OPTION("-u", "--user", "interpret operands as usernames or UIDs")};

namespace renice_pipeline {

auto parse_int(std::string_view text) -> std::optional<int> {
  int value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size())
    return std::nullopt;
  return value;
}

auto run(const CommandContext<RENICE_OPTIONS.size()>& ctx) -> int {
  // [DIFFERS] -g/--pgrp: GNU renice supports process group IDs; not available
  // on Windows
  if (ctx.has("-g") || ctx.has("--pgrp")) {
    safeErrorPrintLn(
        "renice: process group IDs (-g/--pgrp) are not supported on Windows "
        "[DIFFERS from GNU renice]");
    return 1;
  }
  // [DIFFERS] -u/--user: GNU renice supports user-based renice; not available
  // on Windows
  if (ctx.has("-u") || ctx.has("--user")) {
    safeErrorPrintLn(
        "renice: user-based renice (-u/--user) is not supported on Windows "
        "[DIFFERS from GNU renice]");
    return 1;
  }

  // -p/--pid: always interpreted as process IDs on Windows
  (void)ctx.get<bool>("-p", false);
  std::string priority_text = ctx.get<std::string>("-n", "");
  if (priority_text.empty()) {
    priority_text = ctx.get<std::string>("--priority", "");
  }

  std::span<const std::string_view> operands(ctx.positionals.data(),
                                             ctx.positionals.size());
  if (priority_text.empty() && !operands.empty()) {
    priority_text = std::string(operands.front());
    operands = operands.subspan(1);
  }

  auto niceness = parse_int(priority_text);
  if (!niceness) {
    safeErrorPrintLn("renice: invalid priority '" + priority_text + "'");
    return 1;
  }
  *niceness = std::clamp(*niceness, -20, 19);

  if (operands.empty()) {
    safeErrorPrintLn("renice: missing process ID");
    return 1;
  }

  DWORD priority_class = win32_priority_class_for_niceness(*niceness);
  int status = 0;

  for (auto operand : operands) {
    auto pid = win32_parse_pid(operand);
    if (!pid) {
      safeErrorPrintLn("renice: invalid process ID '" + std::string(operand) +
                       "'");
      status = 1;
      continue;
    }

    UniqueHandle process(
        OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_SET_INFORMATION,
                    FALSE, *pid));
    if (!process) {
      DWORD error = GetLastError();
      safeErrorPrintLn("renice: failed to get priority for " +
                       std::to_string(*pid) + ": " +
                       win32_posix_error_text(error));
      status = 1;
      continue;
    }

    int old_nice =
        win32_niceness_from_priority_class(GetPriorityClass(process.get()));
    if (!SetPriorityClass(process.get(), priority_class)) {
      DWORD error = GetLastError();
      safeErrorPrintLn("renice: failed to set priority for " +
                       std::to_string(*pid) + ": " +
                       win32_posix_error_text(error));
      status = 1;
      continue;
    }

    safePrintLn(std::to_string(*pid) + ": old priority " +
                std::to_string(old_nice) + ", new priority " +
                std::to_string(*niceness));
  }

  return status;
}

}  // namespace renice_pipeline

REGISTER_COMMAND(
    renice, "renice",
    "renice [-n] PRIORITY [-p] PID...  # also: -g PGRP, -u USER [DIFFERS]",
    "Alter the scheduling priority of running processes.\n"
    "Windows priority classes are mapped to Unix-like niceness "
    "values from -20 to 19.",
    "  renice 10 -p 1234\n"
    "  renice -n 0 1234",
    "nice(1), ps(1), pgrep(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    RENICE_OPTIONS) {
  return renice_pipeline::run(ctx);
}
