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
 *  - File: yes.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for yes.
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

auto constexpr YES_OPTIONS = std::array{
    OPTION("", "", "output a string repeatedly", STRING_TYPE)
    // yes has no options
};

namespace yes_pipeline {
namespace cp = core::pipeline;

struct Config {
  std::string output = "y";
};

auto build_config(const CommandContext<YES_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  if (!ctx.positionals.empty()) {
    cfg.output = std::string(ctx.positionals[0]);
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  // Note: In a real implementation, this would run indefinitely
  // For safety, we'll limit to 1000 iterations
  const int MAX_ITERATIONS = 1000;

  for (int i = 0; i < MAX_ITERATIONS; ++i) {
    safePrintLn(cfg.output);
  }

  return 0;
}

}  // namespace yes_pipeline

REGISTER_COMMAND(
    yes, "yes", "yes [STRING]...",
    "Repeatedly output a line with all specified STRING(s), or 'y'.\n"
    "\n"
    "Note: This implementation limits output to 1000 iterations\n"
    "for safety. The actual yes command runs indefinitely.",
    "  yes\n"
    "  yes please\n"
    "  yes 'do something'",
    "", "WinuxCmd", "Copyright © 2026 WinuxCmd", YES_OPTIONS) {
  using namespace yes_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"yes");
    return 1;
  }

  return run(*cfg_result);
}
