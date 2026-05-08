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
 *  - File: free.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for free.
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

auto constexpr FREE_OPTIONS = std::array{
    OPTION("-b", "--bytes", "display amount of memory in bytes", BOOL_TYPE),
    OPTION("-k", "--kilo", "display amount of memory in kilobytes", BOOL_TYPE),
    OPTION("-m", "--mega", "display amount of memory in megabytes", BOOL_TYPE),
    OPTION("-g", "--giga", "display amount of memory in gigabytes", BOOL_TYPE),
    OPTION("-h", "--human", "show human-readable output", BOOL_TYPE),
    OPTION("-l", "--lohi", "show detailed low and high memory statistics",
           BOOL_TYPE),
    OPTION("-t", "--total", "display a line showing the totals", BOOL_TYPE),
    OPTION("-s", "--seconds",
           "continuously display memory statistics with delay", INT_TYPE),
    OPTION("-c", "--count", "repeat the display COUNT times", INT_TYPE),
    OPTION("-w", "--wide", "wide output", BOOL_TYPE)};

namespace free_pipeline {
namespace cp = core::pipeline;

enum class Unit { Bytes, Kilo, Mega, Giga, Human };

struct Config {
  Unit unit = Unit::Mega;
  bool lohi = false;
  bool total = false;
  bool wide = false;
  int interval = 0;
  int count = 0;
};

auto build_config(const CommandContext<FREE_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  if (ctx.get<bool>("--bytes", false) || ctx.get<bool>("-b", false)) {
    cfg.unit = Unit::Bytes;
  } else if (ctx.get<bool>("--kilo", false) || ctx.get<bool>("-k", false)) {
    cfg.unit = Unit::Kilo;
  } else if (ctx.get<bool>("--giga", false) || ctx.get<bool>("-g", false)) {
    cfg.unit = Unit::Giga;
  } else if (ctx.get<bool>("--human", false) || ctx.get<bool>("-h", false)) {
    cfg.unit = Unit::Human;
  } else {
    cfg.unit = Unit::Mega;  // Default
  }

  cfg.lohi = ctx.get<bool>("--lohi", false) || ctx.get<bool>("-l", false);
  cfg.total = ctx.get<bool>("--total", false) || ctx.get<bool>("-t", false);
  cfg.wide = ctx.get<bool>("--wide", false) || ctx.get<bool>("-w", false);
  cfg.interval = ctx.get<int>("--seconds", 0);
  cfg.count = ctx.get<int>("--count", 0);

  return cfg;
}

auto format_size(unsigned long long bytes, Unit unit) -> std::string {
  char buf[64];

  switch (unit) {
    case Unit::Bytes:
      snprintf(buf, sizeof(buf), "%llu", bytes);
      break;
    case Unit::Kilo:
      snprintf(buf, sizeof(buf), "%llu", bytes / 1024);
      break;
    case Unit::Mega:
      snprintf(buf, sizeof(buf), "%llu", bytes / (1024 * 1024));
      break;
    case Unit::Giga:
      snprintf(buf, sizeof(buf), "%.1f",
               static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
      break;
    case Unit::Human:
      if (bytes < 1024) {
        snprintf(buf, sizeof(buf), "%lluB", bytes);
      } else if (bytes < 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1fK",
                 static_cast<double>(bytes) / 1024.0);
      } else if (bytes < 1024 * 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%.1fM",
                 static_cast<double>(bytes) / (1024.0 * 1024.0));
      } else {
        snprintf(buf, sizeof(buf), "%.1fG",
                 static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0));
      }
      break;
  }

  return std::string(buf);
}

auto run(const Config& cfg) -> int {
  MEMORYSTATUSEX mem_status;
  mem_status.dwLength = sizeof(mem_status);

  if (!GlobalMemoryStatusEx(&mem_status)) {
    cp::report_custom_error(L"free", L"failed to get memory status");
    return 1;
  }

  // Windows provides different metrics than Linux
  // Map Windows metrics to Linux-style output
  unsigned long long total = mem_status.ullTotalPhys;
  unsigned long long available = mem_status.ullAvailPhys;
  unsigned long long used = total - available;
  unsigned long long cached = 0;   // Windows doesn't provide this directly
  unsigned long long buffers = 0;  // Windows doesn't provide this directly

  // Print header
  if (cfg.wide) {
    safePrint(
        "              total        used        free      shared    buffers    "
        "  cached   available\n");
  } else {
    safePrint("              total        used        free      available\n");
  }

  // Print Mem line
  safePrint("Mem:     ");
  safePrint(format_size(total, cfg.unit));
  safePrint("  ");
  safePrint(format_size(used, cfg.unit));
  safePrint("  ");
  safePrint(format_size(available, cfg.unit));

  if (cfg.wide) {
    safePrint("        0        0        ");
    safePrint(format_size(cached, cfg.unit));
    safePrint("  ");
    safePrint(format_size(available, cfg.unit));
  }

  safePrintLn("");

  // Print Swap line (Windows page file)
  MEMORYSTATUSEX page_status;
  page_status.dwLength = sizeof(page_status);
  GlobalMemoryStatusEx(&page_status);

  unsigned long long total_swap = mem_status.ullTotalPageFile;
  unsigned long long avail_swap = mem_status.ullAvailPageFile;
  unsigned long long used_swap = total_swap - avail_swap;

  safePrint("Swap:    ");
  safePrint(format_size(total_swap, cfg.unit));
  safePrint("  ");
  safePrint(format_size(used_swap, cfg.unit));
  safePrint("  ");
  safePrint(format_size(avail_swap, cfg.unit));

  if (cfg.wide) {
    safePrintLn("        0        0        0");
  } else {
    safePrintLn("");
  }

  // Print Total line if requested
  if (cfg.total) {
    unsigned long long total_total = total + total_swap;
    unsigned long long total_used = used + used_swap;
    unsigned long long total_avail = available + avail_swap;

    safePrint("Total:   ");
    safePrint(format_size(total_total, cfg.unit));
    safePrint("  ");
    safePrint(format_size(total_used, cfg.unit));
    safePrint("  ");
    safePrint(format_size(total_avail, cfg.unit));

    if (cfg.wide) {
      safePrintLn("        0        0        0");
    } else {
      safePrintLn("");
    }
  }

  return 0;
}

}  // namespace free_pipeline

REGISTER_COMMAND(free, "free", "free [OPTION]",
                 "Display amount of free and used memory in the system.\n"
                 "\n"
                 "This is a Windows implementation of the Linux free command.\n"
                 "The memory statistics are approximated from Windows APIs.",
                 "  free\n"
                 "  free -h\n"
                 "  free -m -t\n"
                 "  free -g",
                 "vmstat(1), top(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FREE_OPTIONS) {
  using namespace free_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"free");
    return 1;
  }

  return run(*cfg_result);
}
