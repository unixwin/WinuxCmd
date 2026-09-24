// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for uptime.
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

auto constexpr UPTIME_OPTIONS = std::array{
    // [EXT] option
    OPTION("-p", "--pretty", "show uptime in pretty format", BOOL_TYPE),
    // [EXT] option
    OPTION("-s", "--since", "system up since", BOOL_TYPE)};

namespace uptime_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool pretty = false;
  bool since = false;
};

auto build_config(const CommandContext<UPTIME_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.pretty = ctx.get<bool>("--pretty", false) || ctx.get<bool>("-p", false);
  cfg.since = ctx.get<bool>("--since", false) || ctx.get<bool>("-s", false);
  return cfg;
}

auto format_uptime(std::chrono::seconds uptime) -> std::string {
  auto days = std::chrono::duration_cast<std::chrono::hours>(uptime) / 24;
  auto hours = std::chrono::duration_cast<std::chrono::hours>(uptime) % 24;
  auto minutes = std::chrono::duration_cast<std::chrono::minutes>(uptime) % 60;

  std::string result;
  if (days.count() > 0) {
    result += std::to_string(days.count()) + " day";
    if (days.count() > 1) result += "s";
    if (hours.count() > 0 || minutes.count() > 0) result += ", ";
  }
  if (hours.count() > 0) {
    result += std::to_string(hours.count()) + " hour";
    if (hours.count() > 1) result += "s";
    if (minutes.count() > 0) result += ", ";
  }
  if (minutes.count() > 0 || (days.count() == 0 && hours.count() == 0)) {
    result += std::to_string(minutes.count()) + " minute";
    if (minutes.count() != 1) result += "s";
  }

  return result;
}

// [GNU] the default uptime line renders the duration as a clock, e.g.
// "up 22:03" / "up 2 days,  4:13", not "up 22 hours, 3 minutes"
// (uutils #13027, #13453)
auto format_uptime_clock(std::chrono::seconds uptime) -> std::string {
  auto days = std::chrono::duration_cast<std::chrono::hours>(uptime) / 24;
  auto hours = std::chrono::duration_cast<std::chrono::hours>(uptime) % 24;
  auto minutes = std::chrono::duration_cast<std::chrono::minutes>(uptime) % 60;

  char buf[64];
  if (days.count() > 0) {
    std::string day_part =
        std::to_string(days.count()) + (days.count() == 1 ? " day" : " days");
    snprintf(buf, sizeof(buf), "%s, %2d:%02d", day_part.c_str(),
             static_cast<int>(hours.count()),
             static_cast<int>(minutes.count()));
  } else {
    snprintf(buf, sizeof(buf), "%2d:%02d", static_cast<int>(hours.count()),
             static_cast<int>(minutes.count()));
  }
  return std::string(buf);
}

auto run(const Config& cfg) -> int {
  // Get system uptime
  auto uptime = std::chrono::seconds(GetTickCount64() / 1000);

  // Get current time
  auto now = std::chrono::system_clock::now();
  auto now_time_t = std::chrono::system_clock::to_time_t(now);

  // Format current time
  std::stringstream time_ss;
  time_ss << std::put_time(std::localtime(&now_time_t), "%H:%M:%S");

  if (cfg.since) {
    // Calculate boot time
    auto boot_time = now - uptime;
    auto boot_time_t = std::chrono::system_clock::to_time_t(boot_time);

    std::stringstream boot_ss;
    boot_ss << std::put_time(std::localtime(&boot_time_t), "%Y-%m-%d %H:%M:%S");

    safePrintLn(boot_ss.str());
  } else if (cfg.pretty) {
    safePrint("up ");
    safePrintLn(format_uptime(uptime));
  } else {
    safePrint(" ");
    safePrint(time_ss.str());
    safePrint(" up ");
    safePrint(format_uptime_clock(uptime));
    safePrint(",  ");
    safePrintLn("load average: N/A, N/A, N/A");
  }

  return 0;
}

}  // namespace uptime_pipeline

REGISTER_COMMAND(
    uptime, "uptime", "uptime [OPTION]...",
    "Tell how long the system has been running.\n"
    "\n"
    "Note: This implementation provides Windows-specific information.\n"
    "Load average is not available on Windows.",
    "  uptime\n"
    "  uptime -p\n"
    "  uptime -s",
    "w(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", UPTIME_OPTIONS) {
  using namespace uptime_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"uptime");
    return 1;
  }

  return run(*cfg_result);
}
