// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU]
    OPTION("-b", "--bytes", "display amount of memory in bytes", BOOL_TYPE),
    // [GNU] -k/--kibi: show output in kibibytes (1024-based)
    OPTION("-k", "--kibi", "display amount of memory in kibibytes", BOOL_TYPE),
    // [GNU] -m/--mebi: show output in mebibytes (1024-based)
    OPTION("-m", "--mebi", "display amount of memory in mebibytes", BOOL_TYPE),
    // [GNU] -g/--gibi: show output in gibibytes (1024-based)
    OPTION("-g", "--gibi", "display amount of memory in gibibytes", BOOL_TYPE),
    // [GNU]
    OPTION("-h", "--human", "show human-readable output", BOOL_TYPE),
    // [GNU]
    OPTION("-l", "--lohi", "show detailed low and high memory statistics",
           BOOL_TYPE),
    // [GNU]
    OPTION("-t", "--total", "display a line showing the totals", BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--seconds",
           "continuously display memory statistics with delay", INT_TYPE),
    // [GNU]
    OPTION("-c", "--count", "repeat the display COUNT times", INT_TYPE),
    // [GNU]
    OPTION("-w", "--wide", "wide output", BOOL_TYPE),
    // [GNU] --kilo: show output in kilobytes (1000-based SI)
    OPTION("", "--kilo", "display amount of memory in kilobytes (SI)",
           BOOL_TYPE),
    // [GNU] --mega: show output in megabytes (1000-based SI)
    OPTION("", "--mega", "display amount of memory in megabytes (SI)",
           BOOL_TYPE),
    // [GNU] --giga: show output in gigabytes (1000-based SI)
    OPTION("", "--giga", "display amount of memory in gigabytes (SI)",
           BOOL_TYPE),
    // [GNU] --tera: show output in terabytes (1000-based SI)
    OPTION("", "--tera", "display amount of memory in terabytes (SI)",
           BOOL_TYPE),
    // [GNU] --peta: show output in petabytes (1000-based SI)
    OPTION("", "--peta", "display amount of memory in petabytes (SI)",
           BOOL_TYPE),
    // [GNU] --tebi: show output in tebibytes (1024-based)
    OPTION("", "--tebi", "display amount of memory in tebibytes", BOOL_TYPE),
    // [GNU] --pebi: show output in pebibytes (1024-based)
    OPTION("", "--pebi", "display amount of memory in pebibytes", BOOL_TYPE),
    // [GNU] --si: use powers of 1000 not 1024
    OPTION("", "--si", "use powers of 1000 not 1024", BOOL_TYPE),
    // [GNU] -L/--line: show output on a single line
    OPTION("-L", "--line", "show output on a single line", BOOL_TYPE),
    // [GNU] -v/--committed: show committed memory and commit limit
    OPTION("-v", "--committed", "show committed memory and commit limit",
           BOOL_TYPE)};

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
  } else if (ctx.get<bool>("--kibi", false) || ctx.get<bool>("-k", false) ||
             ctx.get<bool>("--kilo", false)) {
    cfg.unit = Unit::Kilo;
  } else if (ctx.get<bool>("--mebi", false) || ctx.get<bool>("-m", false) ||
             ctx.get<bool>("--mega", false)) {
    cfg.unit = Unit::Mega;
  } else if (ctx.get<bool>("--gibi", false) || ctx.get<bool>("-g", false) ||
             ctx.get<bool>("--giga", false)) {
    cfg.unit = Unit::Giga;
  } else if (ctx.get<bool>("--human", false) || ctx.get<bool>("-h", false) ||
             ctx.get<bool>("--si", false)) {
    cfg.unit = Unit::Human;
  } else {
    cfg.unit = Unit::Mega;  // Default
  }

  cfg.lohi = ctx.get<bool>("--lohi", false) || ctx.get<bool>("-l", false);
  cfg.total = ctx.get<bool>("--total", false) || ctx.get<bool>("-t", false);
  cfg.wide = ctx.get<bool>("--wide", false) || ctx.get<bool>("-w", false);
  cfg.interval = ctx.get<int>("--seconds", 0);
  if (cfg.interval == 0) {
    cfg.interval = ctx.get<int>("-s", 0);
  }
  cfg.count = ctx.get<int>("--count", 0);
  if (cfg.count == 0) {
    cfg.count = ctx.get<int>("-c", 0);
  }

  if (cfg.interval < 0) {
    return std::unexpected("invalid interval");
  }
  if (cfg.count < 0) {
    return std::unexpected("invalid count");
  }

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

auto print_once(const Config& cfg) -> void {
  MEMORYSTATUSEX mem_status;
  mem_status.dwLength = sizeof(mem_status);

  if (!GlobalMemoryStatusEx(&mem_status)) {
    return;
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

  // [DIFFERS] Print Swap line (Windows page file)
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

  // [DIFFERS] -l/--lohi: Show virtual memory (low/high not applicable on 64-bit
  // Windows, so we show virtual address space instead)
  if (cfg.lohi) {
    unsigned long long total_virt = mem_status.ullTotalVirtual;
    unsigned long long avail_virt = mem_status.ullAvailVirtual;
    unsigned long long used_virt = total_virt - avail_virt;

    safePrint("Virt:    ");
    safePrint(format_size(total_virt, cfg.unit));
    safePrint("  ");
    safePrint(format_size(used_virt, cfg.unit));
    safePrint("  ");
    safePrint(format_size(avail_virt, cfg.unit));
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
}

auto run(const Config& cfg) -> int {
  // [DIFFERS] -s/--seconds and -c/--count: continuous display loop
  bool looping = cfg.interval > 0;
  int max_count = looping ? (cfg.count > 0 ? cfg.count : -1) : 1;
  int iteration = 0;

  while (max_count < 0 || iteration < max_count) {
    if (iteration > 0) {
      Sleep(static_cast<DWORD>(cfg.interval * 1000));
    }

    print_once(cfg);
    ++iteration;
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
