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

// [GNU] procps free.c scale model: values are carried in KiB; the unit
// options pick an exponent (default 0 = KiB as-is, 1 = bytes, 2 = KiB/KB,
// 3 = MiB/MB, 4 = GiB/GB, 5 = TiB/TB, 6 = PiB/PB) and --si (or the
// kilo/mega/... spellings) switches the scaling base from 1024 to 1000.
// (procps-v4.0.4 src/free.c: option cases 'b','k','m','g',TEBI/PEBI and
// KILO/MEGA/GIGA/TERA/PETA with FREE_SI; scale_size().)
struct Config {
  int exponent = 0;
  bool si = false;
  bool human = false;
  bool lohi = false;
  bool total = false;
  bool wide = false;
  bool line = false;
  bool committed = false;
  int interval = 0;
  int count = 0;
};

auto build_config(const CommandContext<FREE_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  // [GNU] free.c check_unit_set(): a second unit option is a hard error,
  // even a repeat of the same one.
  const size_t unit_count =
      ctx.count({"-b", "--bytes", "-k", "--kibi", "-m", "--mebi", "-g",
                 "--gibi", "--tebi", "--pebi", "--kilo", "--mega", "--giga",
                 "--tera", "--peta"});
  if (unit_count > 1) {
    return std::unexpected("Multiple unit options don't make sense.");
  }

  if (ctx.get<bool>("--bytes", false) || ctx.get<bool>("-b", false)) {
    cfg.exponent = 1;
  } else if (ctx.get<bool>("--kibi", false) || ctx.get<bool>("-k", false)) {
    cfg.exponent = 2;
  } else if (ctx.get<bool>("--mebi", false) || ctx.get<bool>("-m", false)) {
    cfg.exponent = 3;
  } else if (ctx.get<bool>("--gibi", false) || ctx.get<bool>("-g", false)) {
    cfg.exponent = 4;
  } else if (ctx.get<bool>("--tebi", false)) {
    cfg.exponent = 5;
  } else if (ctx.get<bool>("--pebi", false)) {
    cfg.exponent = 6;
  } else if (ctx.get<bool>("--kilo", false)) {
    cfg.exponent = 2;
    cfg.si = true;
  } else if (ctx.get<bool>("--mega", false)) {
    cfg.exponent = 3;
    cfg.si = true;
  } else if (ctx.get<bool>("--giga", false)) {
    cfg.exponent = 4;
    cfg.si = true;
  } else if (ctx.get<bool>("--tera", false)) {
    cfg.exponent = 5;
    cfg.si = true;
  } else if (ctx.get<bool>("--peta", false)) {
    cfg.exponent = 6;
    cfg.si = true;
  }
  if (ctx.get<bool>("--si", false)) cfg.si = true;
  cfg.human = ctx.get<bool>("--human", false) || ctx.get<bool>("-h", false);

  cfg.lohi = ctx.get<bool>("--lohi", false) || ctx.get<bool>("-l", false);
  cfg.total = ctx.get<bool>("--total", false) || ctx.get<bool>("-t", false);
  cfg.wide = ctx.get<bool>("--wide", false) || ctx.get<bool>("-w", false);
  cfg.line = ctx.get<bool>("--line", false) || ctx.get<bool>("-L", false);
  cfg.committed =
      ctx.get<bool>("--committed", false) || ctx.get<bool>("-v", false);
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

// [GNU] procps free.c scale_size(): KiB input, integer truncation for the
// fixed units, and the human form that tries a one-decimal rendering
// ("7.7Gi") before falling back to a whole number ("15Gi").
auto scale_size(unsigned long long kib, const Config& cfg) -> std::string {
  const long double base = cfg.si ? 1000.0L : 1024.0L;
  const long double bytes = static_cast<long double>(kib) * 1024.0L;
  char buf[64];

  if (!cfg.human) {
    unsigned long long value;
    if (cfg.exponent == 1) {
      value = static_cast<unsigned long long>(bytes);
    } else if (cfg.exponent == 0) {
      value = static_cast<unsigned long long>(bytes / base);
    } else {
      long double scaled = bytes;
      for (int i = 1; i < cfg.exponent; ++i) scaled /= base;
      value = static_cast<unsigned long long>(scaled);
    }
    snprintf(buf, sizeof(buf), "%llu", value);
    return std::string(buf);
  }

  static constexpr char suffixes[] = {'B', 'K', 'M', 'G', 'T', 'P'};
  const long double raw_bytes = bytes;
  if (raw_bytes < 10000.0L) {
    snprintf(buf, sizeof(buf), "%lluB",
             static_cast<unsigned long long>(raw_bytes));
    if (std::string_view(buf).size() <= 4) return std::string(buf);
  }
  long double scaled = raw_bytes;
  for (int i = 1; i <= 5; ++i) {
    scaled /= base;
    const char suffix = suffixes[i];
    if (cfg.si) {
      snprintf(buf, sizeof(buf), "%.1Lf%c", static_cast<double>(scaled),
               suffix);
      if (std::string_view(buf).size() <= 4) return std::string(buf);
      snprintf(buf, sizeof(buf), "%llu%c",
               static_cast<unsigned long long>(scaled), suffix);
      if (std::string_view(buf).size() <= 4) return std::string(buf);
    } else {
      snprintf(buf, sizeof(buf), "%.1Lf%ci", static_cast<double>(scaled),
               suffix);
      if (std::string_view(buf).size() <= 5) return std::string(buf);
      snprintf(buf, sizeof(buf), "%llu%ci",
               static_cast<unsigned long long>(scaled), suffix);
      if (std::string_view(buf).size() <= 5) return std::string(buf);
    }
  }
  return std::string(buf);
}

// [GNU] free.c prints each row label in a 9-wide field (print_head_col)
// and every value right-aligned in an 11-wide field.
auto pad_label(const std::string& label) -> std::string {
  std::string out = label;
  if (out.size() < 9) out.append(9 - out.size(), ' ');
  return out;
}

auto pad_value(const std::string& value) -> std::string {
  std::string out;
  if (value.size() < 11) out.append(11 - value.size(), ' ');
  out += value;
  return out;
}

auto print_row(const std::string& label,
               std::initializer_list<unsigned long long> values,
               const Config& cfg) -> void {
  std::string line = pad_label(label);
  bool first = true;
  for (unsigned long long value : values) {
    if (!first) line += " ";
    line += pad_value(scale_size(value, cfg));
    first = false;
  }
  safePrintLn(line);
}

struct MemSnapshot {
  unsigned long long total = 0;      // KiB
  unsigned long long used = 0;       // KiB
  unsigned long long free = 0;       // KiB
  unsigned long long available = 0;  // KiB
  unsigned long long swap_total = 0;
  unsigned long long swap_used = 0;
  unsigned long long swap_free = 0;
  unsigned long long commit_limit = 0;
  unsigned long long committed = 0;
};

auto take_snapshot() -> MemSnapshot {
  MemSnapshot snap;
  MEMORYSTATUSEX mem_status;
  mem_status.dwLength = sizeof(mem_status);
  if (!GlobalMemoryStatusEx(&mem_status)) {
    return snap;
  }

  // [DIFFERS] Windows exposes no shared/buffers/cache split and no
  // MemFree distinct from MemAvailable; the available figure fills both
  // the "free" and "available" columns and used = total - available,
  // matching the mapping this command has always shipped.
  snap.total = mem_status.ullTotalPhys / 1024;
  snap.available = mem_status.ullAvailPhys / 1024;
  snap.free = snap.available;
  snap.used = snap.total - snap.available;
  // [DIFFERS] Swap is the commit-backed pagefile space Windows reports.
  snap.swap_total = mem_status.ullTotalPageFile / 1024;
  snap.swap_free = mem_status.ullAvailPageFile / 1024;
  snap.swap_used = snap.swap_total - snap.swap_free;

  PERFORMANCE_INFORMATION perf;
  if (GetPerformanceInfo(&perf, sizeof(perf))) {
    snap.commit_limit = perf.CommitLimit * perf.PageSize / 1024;
    snap.committed = perf.CommitTotal * perf.PageSize / 1024;
  }
  return snap;
}

auto print_once(const Config& cfg) -> void {
  const MemSnapshot snap = take_snapshot();

  // [GNU] free.c -L: a single line of labelled figures.
  if (cfg.line) {
    std::string line = "SwapUse " + pad_value(scale_size(snap.swap_used, cfg));
    line += " CachUse " + pad_value(scale_size(0, cfg));
    line += "  MemUse " + pad_value(scale_size(snap.used, cfg));
    line += " MemFree " + pad_value(scale_size(snap.free, cfg)) + " ";
    safePrintLn(line);
    return;
  }

  if (cfg.wide) {
    safePrintLn(
        "               total        used        free      shared     "
        "buffers       cache   available");
  } else {
    safePrintLn(
        "               total        used        free      shared  "
        "buff/cache   available");
  }

  if (cfg.wide) {
    std::string line = pad_label("Mem:");
    line += pad_value(scale_size(snap.total, cfg));
    line += " " + pad_value(scale_size(snap.used, cfg));
    line += " " + pad_value(scale_size(snap.free, cfg));
    line += " " + pad_value(scale_size(0, cfg));
    line += " " + pad_value(scale_size(0, cfg));
    line += " " + pad_value(scale_size(0, cfg));
    line += " " + pad_value(scale_size(snap.available, cfg));
    safePrintLn(line);
  } else {
    print_row("Mem:", {snap.total, snap.used, snap.free, 0, 0, snap.available},
              cfg);
  }

  // [GNU] free.c -l: Low:/High: lines follow Mem:. On a 64-bit host all
  // memory is low memory (HighTotal is 0 in /proc/meminfo there too).
  if (cfg.lohi) {
    print_row("Low:", {snap.total, snap.used, snap.free}, cfg);
    print_row("High:", {0, 0, 0}, cfg);
  }

  print_row("Swap:", {snap.swap_total, snap.swap_used, snap.swap_free}, cfg);

  if (cfg.total) {
    print_row("Total:",
              {snap.total + snap.swap_total, snap.used + snap.swap_used,
               snap.free + snap.swap_free},
              cfg);
  }

  // [GNU] free.c -v: "Comm:" line: commit limit, committed, headroom.
  if (cfg.committed) {
    const unsigned long long headroom =
        snap.commit_limit >= snap.committed ? snap.commit_limit - snap.committed
                                            : 0;
    print_row("Comm:", {snap.commit_limit, snap.committed, headroom}, cfg);
  }
}

auto run(const Config& cfg) -> int {
  // [GNU] free.c: -c also arms the repeat loop, with a one-second default
  // interval when -s is absent.
  const bool looping = cfg.interval > 0 || cfg.count > 0;
  const int interval = cfg.interval > 0 ? cfg.interval : 1;
  const int max_count = looping ? (cfg.count > 0 ? cfg.count : -1) : 1;
  int iteration = 0;

  while (max_count < 0 || iteration < max_count) {
    if (iteration > 0) {
      Sleep(static_cast<DWORD>(interval * 1000));
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

  // [GNU] free.c rejects operands (optind != argc is a usage error).
  if (!ctx.positionals.empty()) {
    return cp::report_extra_operand("free", ctx.positionals.front());
  }

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"free");
    return 1;
  }

  return run(*cfg_result);
}
