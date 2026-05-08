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
 *  - File: seq.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for seq.
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

auto constexpr SEQ_OPTIONS = std::array{
    OPTION("-f", "--format", "use printf style floating-point FORMAT",
           STRING_TYPE),
    OPTION("-s", "--separator", "use STRING to separate numbers", STRING_TYPE),
    OPTION("-w", "--equal-width",
           "equalize width by padding with leading zeroes", BOOL_TYPE)};

namespace seq_pipeline {
namespace cp = core::pipeline;

struct Config {
  std::string format;
  std::string separator;
  bool equal_width = false;
  bool has_first = false;
  bool has_increment = false;
  double first = 1.0;
  double increment = 1.0;
  double last = 1.0;
};

auto build_config(const CommandContext<SEQ_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.format = ctx.get<std::string>("--format", "");
  cfg.separator = ctx.get<std::string>("--separator", "\n");
  cfg.equal_width =
      ctx.get<bool>("--equal-width", false) || ctx.get<bool>("-w", false);

  // Parse positionals
  size_t num_args = ctx.positionals.size();

  if (num_args == 0) {
    return std::unexpected("missing operand");
  }

  if (num_args == 1) {
    // seq LAST
    cfg.last = std::stod(std::string(ctx.positionals[0]));
  } else if (num_args == 2) {
    // seq FIRST LAST
    cfg.first = std::stod(std::string(ctx.positionals[0]));
    cfg.last = std::stod(std::string(ctx.positionals[1]));
    cfg.has_first = true;
  } else if (num_args == 3) {
    // seq FIRST INCREMENT LAST
    cfg.first = std::stod(std::string(ctx.positionals[0]));
    cfg.increment = std::stod(std::string(ctx.positionals[1]));
    cfg.last = std::stod(std::string(ctx.positionals[2]));
    cfg.has_first = true;
    cfg.has_increment = true;
  } else {
    return std::unexpected("extra operand");
  }

  return cfg;
}

auto format_number(double value, const std::string& fmt, bool equal_width,
                   int max_width) -> std::string {
  char buf[128];

  if (!fmt.empty()) {
    // Use custom format
    snprintf(buf, sizeof(buf), fmt.c_str(), value);
  } else if (value == std::floor(value)) {
    // Integer value
    if (equal_width) {
      snprintf(buf, sizeof(buf), "%0*.*f", max_width, 0, value);
    } else {
      snprintf(buf, sizeof(buf), "%.0f", value);
    }
  } else {
    // Floating point value
    snprintf(buf, sizeof(buf), "%g", value);
  }

  return std::string(buf);
}

auto run(const Config& cfg) -> int {
  // Determine direction
  bool increasing = (cfg.increment > 0);

  // Determine if we should output anything
  bool should_output = false;
  if (increasing) {
    should_output = (cfg.first <= cfg.last);
  } else {
    should_output = (cfg.first >= cfg.last);
  }

  if (!should_output) {
    return 0;
  }

  // Calculate max width for equal-width formatting
  int max_width = 0;
  if (cfg.equal_width) {
    char buf[128];

    if (cfg.format.empty()) {
      if (cfg.first == std::floor(cfg.first)) {
        snprintf(buf, sizeof(buf), "%.0f", cfg.first);
      } else {
        snprintf(buf, sizeof(buf), "%g", cfg.first);
      }
      max_width = std::max(max_width, static_cast<int>(strlen(buf)));

      if (cfg.last == std::floor(cfg.last)) {
        snprintf(buf, sizeof(buf), "%.0f", cfg.last);
      } else {
        snprintf(buf, sizeof(buf), "%g", cfg.last);
      }
      max_width = std::max(max_width, static_cast<int>(strlen(buf)));
    }
  }

  // Generate sequence
  SmallVector<std::string, 1024> results;
  double current = cfg.first;
  int count = 0;
  const int MAX_COUNT = 1000000;  // Prevent infinite loops

  while ((increasing && current <= cfg.last) ||
         (!increasing && current >= cfg.last)) {
    if (count >= MAX_COUNT) {
      break;
    }

    results.push_back(
        format_number(current, cfg.format, cfg.equal_width, max_width));
    current += cfg.increment;
    count++;
  }

  // Output results
  for (size_t i = 0; i < results.size(); ++i) {
    safePrint(results[i]);
    if (i < results.size() - 1) {
      safePrint(cfg.separator);
    }
  }

  if (!results.empty()) {
    if (cfg.separator != "\n") {
      safePrintLn("");
    } else {
      safePrintLn("");  // Always add final newline
    }
  }

  return 0;
}

}  // namespace seq_pipeline

REGISTER_COMMAND(seq, "seq",
                 "seq [OPTION]... LAST\n"
                 "seq [OPTION]... FIRST LAST\n"
                 "seq [OPTION]... FIRST INCREMENT LAST",
                 "Print numbers from FIRST to LAST, in steps of INCREMENT.",
                 "  seq 10\n"
                 "  seq 1 10\n"
                 "  seq 1 2 10\n"
                 "  seq -s ' ' 5 10\n"
                 "  seq -w 1 10",
                 "printf(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 SEQ_OPTIONS) {
  using namespace seq_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"seq");
    return 1;
  }

  return run(*cfg_result);
}
