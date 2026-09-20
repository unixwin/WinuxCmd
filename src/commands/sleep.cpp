// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sleep.
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

auto constexpr SLEEP_OPTIONS =
    std::array{// [GNU] NUMBER: pause for seconds
               OPTION("", "", "pause for NUMBER seconds", STRING_TYPE)};

namespace sleep_pipeline {
namespace cp = core::pipeline;

struct Config {
  SmallVector<std::string, 64> durations;
};

auto invalid_interval(const std::string& duration)
    -> std::unexpected<std::string> {
  return std::unexpected(
      winux::i18n::format("command.sleep.error.invalid_interval",
                          "invalid time interval '{}'", duration));
}

// [GNU] sleep.c: intervals are parsed with strtod, so hexadecimal floats,
// leading '+', scientific notation, "inf"/"infinity" are accepted; NaN and
// negative values are rejected.  Only a trailing [smhd] acts as a suffix.
// Returns milliseconds (may be +inf).
auto parse_duration(const std::string& duration) -> cp::Result<double> {
  std::string s = duration;
  if (auto first = s.find_first_not_of(" \t\r\n");
      first != std::string::npos && first > 0) {
    s.erase(0, first);
  }

  if (s.empty()) {
    return invalid_interval(duration);
  }

  double multiplier = 1.0;
  if (s.size() > 1) {
    switch (s.back()) {
      case 's':
        multiplier = 1.0;
        s.pop_back();
        break;
      case 'm':
        multiplier = 60.0;
        s.pop_back();
        break;
      case 'h':
        multiplier = 3600.0;
        s.pop_back();
        break;
      case 'd':
        multiplier = 86400.0;
        s.pop_back();
        break;
    }
  }

  errno = 0;
  char* end = nullptr;
  const double value = std::strtod(s.c_str(), &end);
  if (end != s.c_str() + s.size() || std::isnan(value) || value < 0 ||
      (end == s.c_str())) {
    return invalid_interval(duration);
  }
  return value * multiplier * 1000.0;  // Convert to milliseconds
}

auto build_config(const CommandContext<SLEEP_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  for (auto arg : ctx.positionals) {
    cfg.durations.push_back(std::string(arg));
  }

  if (cfg.durations.empty()) {
    return std::unexpected("missing operand");
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  double total_ms = 0.0;
  bool infinite = false;
  SmallVector<std::string, 8> invalid_durations;

  for (const auto& duration_str : cfg.durations) {
    auto duration_result = parse_duration(duration_str);
    if (!duration_result) {
      invalid_durations.push_back(std::string(duration_result.error()));
      continue;
    }
    if (std::isinf(*duration_result)) {
      infinite = true;
    } else {
      total_ms += *duration_result;
    }
  }

  if (!invalid_durations.empty()) {
    for (const auto& error : invalid_durations) {
      safeErrorPrintLn("sleep: " + error);
    }
    safeErrorPrintLn("Try 'sleep --help' for more information.");
    return 1;
  }

  // [GNU] "sleep inf" / "sleep infinity" pauses forever; so does any
  // interval that overflows a machine duration (e.g. "sleep 1e300").
  if (infinite || std::isinf(total_ms) ||
      total_ms >= static_cast<double>(std::numeric_limits<int64_t>::max())) {
    while (true) {
      Sleep(INFINITE);
    }
  }

  int64_t remaining_ms = static_cast<int64_t>(total_ms);
  while (remaining_ms > 0) {
    const auto chunk =
        std::min<int64_t>(remaining_ms, std::numeric_limits<DWORD>::max());
    Sleep(static_cast<DWORD>(chunk));
    remaining_ms -= chunk;
  }

  return 0;
}

}  // namespace sleep_pipeline

REGISTER_COMMAND(
    sleep, "sleep", "sleep NUMBER[SUFFIX]...",
    "Pause for NUMBER seconds.\n"
    "\n"
    "SUFFIX may be 's' for seconds (the default), 'm' for minutes,\n"
    "'h' for hours or 'd' for days.\n"
    "\n"
    "Unlike most implementations that require NUMBER be an integer,\n"
    "here NUMBER may be an arbitrary floating point number.\n"
    "\n"
    "Note: This implementation supports floating point numbers.",
    "  sleep 1\n"
    "  sleep 2.5\n"
    "  sleep 1m 30s\n"
    "  sleep 0.5h",
    "pause(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SLEEP_OPTIONS) {
  using namespace sleep_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("sleep: missing operand");
      safeErrorPrintLn("Try 'sleep --help' for more information.");
      return 1;
    }
    cp::report_error(cfg_result, L"sleep");
    return 1;
  }

  return run(*cfg_result);
}
