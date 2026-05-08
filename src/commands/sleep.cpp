/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: sleep.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
    std::array{OPTION("", "", "pause for NUMBER seconds", STRING_TYPE)};

namespace sleep_pipeline {
namespace cp = core::pipeline;

struct Config {
  SmallVector<std::string, 64> durations;
};

auto parse_duration(const std::string& duration) -> cp::Result<int64_t> {
  try {
    // Support: N, Ns, Nm, Nh, Nd
    std::string s = duration;

    if (s.empty()) {
      return std::unexpected("invalid duration");
    }

    int64_t multiplier = 1;
    if (s.size() > 1) {
      char suffix = s.back();

      switch (suffix) {
        case 's':
        case 'S':
          multiplier = 1;
          s = s.substr(0, s.size() - 1);
          break;
        case 'm':
        case 'M':
          multiplier = 60;
          s = s.substr(0, s.size() - 1);
          break;
        case 'h':
        case 'H':
          multiplier = 3600;
          s = s.substr(0, s.size() - 1);
          break;
        case 'd':
        case 'D':
          multiplier = 86400;
          s = s.substr(0, s.size() - 1);
          break;
      }
    }

    double value = std::stod(s);
    return static_cast<int64_t>(value * multiplier *
                                1000);  // Convert to milliseconds
  } catch (...) {
    return std::unexpected("invalid duration format");
  }
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
  int64_t total_ms = 0;

  for (const auto& duration_str : cfg.durations) {
    auto duration_result = parse_duration(duration_str);
    if (!duration_result) {
      cp::report_error(duration_result, L"sleep");
      return 1;
    }
    total_ms += *duration_result;
  }

  if (total_ms < 0) {
    total_ms = 0;
  }

  Sleep(static_cast<DWORD>(total_ms));

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
    cp::report_error(cfg_result, L"sleep");
    return 1;
  }

  return run(*cfg_result);
}
