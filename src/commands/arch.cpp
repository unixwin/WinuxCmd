// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU]
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
