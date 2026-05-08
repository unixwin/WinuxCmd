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
 *  - File: arch.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for arch.
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

auto constexpr ARCH_OPTIONS =
    std::array{OPTION("", "", "print machine hardware name", STRING_TYPE)};

namespace arch_pipeline {
namespace cp = core::pipeline;

struct Config {
  // No configuration needed
};

auto build_config(const CommandContext<ARCH_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  return cfg;
}

auto run(const Config& cfg) -> int {
  // Get system architecture
  SYSTEM_INFO si;
  GetSystemInfo(&si);

  const char* arch = "unknown";

  switch (si.wProcessorArchitecture) {
    case PROCESSOR_ARCHITECTURE_AMD64:
      arch = "x86_64";
      break;
    case PROCESSOR_ARCHITECTURE_INTEL:
      arch = "i386";
      break;
    case PROCESSOR_ARCHITECTURE_ARM:
      arch = "arm";
      break;
    case PROCESSOR_ARCHITECTURE_ARM64:
      arch = "aarch64";
      break;
    case PROCESSOR_ARCHITECTURE_IA64:
      arch = "ia64";
      break;
    default:
      arch = "unknown";
      break;
  }

  safePrintLn(arch);
  return 0;
}

}  // namespace arch_pipeline

REGISTER_COMMAND(
    arch, "arch", "arch",
    "Print machine architecture.\n"
    "\n"
    "Print machine hardware name.\n"
    "\n"
    "Note: This implementation displays the Windows processor architecture.",
    "  arch", "uname -m", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    ARCH_OPTIONS) {
  using namespace arch_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"arch");
    return 1;
  }

  return run(*cfg_result);
}
