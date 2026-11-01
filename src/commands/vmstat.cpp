// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Implementation for vmstat (report virtual memory
///     statistics), following procps 4.0.4 src/vmstat.c output formats
///     and mode dispatch. Windows data sources are documented per
///     field in REPORT.md (cluster fix/1127-p2b-procps); fields with no
///     Windows equivalent (swap traffic counters, buffer cache split,
///     slab allocator) report 0 or header-only rather than invented
///     numbers.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <winioctl.h>

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr VMSTAT_OPTIONS = std::array{
    OPTION("-a", "--active", "show active and inactive memory"),
    OPTION("-d", "--disk", "display disk statistics"),
    OPTION("-D", "--disk-sum", "display disk summary"),
    OPTION("-f", "--forks", "display the number of forks since boot"),
    OPTION("-m", "--slabs", "display slabinfo"),
    OPTION("-n", "--one-header", "do not redisplay the header"),
    OPTION("-p", "--partition", "display partition statistics", STRING_TYPE),
    OPTION("-s", "--stats", "display event counter statistics"),
    OPTION("-S", "--unit", "define display unit (b, k, K, m or M)",
           STRING_TYPE),
    OPTION("-t", "--timestamp", "show timestamp"),
    OPTION("-w", "--wide", "use wide output"),
    OPTION("-y", "--no-first", "skips first line of output")};

namespace vmstat_pipeline {

enum class Mode { kReport, kStats, kDisk, kDiskSum, kPartition, kSlabs };

struct Config {
  Mode mode = Mode::kReport;
  bool active = false;
  bool wide = false;
  bool one_header = false;
  bool timestamp = false;
  bool no_first = false;
  std::string partition;
  unsigned long data_unit = 1024;  // upstream dataUnit, K default
  char unit_char = 'K';
  bool has_delay = false;
  unsigned long delay = 0;
  bool infinite = false;
  long num_updates = 1;
};

struct ConfigError {
  std::string message;
};

auto build_config(const CommandContext<VMSTAT_OPTIONS.size()>& ctx)
    -> std::expected<Config, ConfigError> {
  Config cfg;
  cfg.active = ctx.has("-a") || ctx.has("--active");
  cfg.wide = ctx.has("-w") || ctx.has("--wide");
  cfg.one_header = ctx.has("-n") || ctx.has("--one-header");
  cfg.timestamp = ctx.has("-t") || ctx.has("--timestamp");
  cfg.no_first = ctx.has("-y") || ctx.has("--no-first");

  int modes = 0;
  if (ctx.has("-s") || ctx.has("--stats")) {
    cfg.mode = Mode::kStats;
    ++modes;
  }
  if (ctx.has("-d") || ctx.has("--disk")) {
    cfg.mode = Mode::kDisk;
    ++modes;
  }
  if (ctx.has("-D") || ctx.has("--disk-sum")) {
    cfg.mode = Mode::kDiskSum;
    ++modes;
  }
  if (ctx.has("-m") || ctx.has("--slabs")) {
    cfg.mode = Mode::kSlabs;
    ++modes;
  }
  if (ctx.has("-p") || ctx.has("--partition")) {
    cfg.mode = Mode::kPartition;
    ++modes;
    cfg.partition = ctx.get<std::string>("--partition", "");
    // vmstat.c strips a leading "/dev/" from the partition argument.
    if (cfg.partition.starts_with("/dev/")) cfg.partition.erase(0, 5);
  }
  // Upstream ORs the mode bits and the dispatch switch only accepts
  // single-mode values; combinations are a usage error.
  if (modes > 1) {
    return std::unexpected(
        ConfigError{"vmstat: only one of -s, -d, -D, -p and -m may be used"});
  }

  if (ctx.has("-S") || ctx.has("--unit")) {
    const auto text = ctx.get<std::string>("--unit", "K");
    const char c = text.empty() ? '\0' : text[0];
    switch (c) {
      case 'b':
      case 'B':
        cfg.data_unit = 1;
        break;
      case 'k':
        cfg.data_unit = 1000;
        break;
      case 'K':
        cfg.data_unit = 1024;
        break;
      case 'm':
        cfg.data_unit = 1000UL * 1000UL;
        break;
      case 'M':
        cfg.data_unit = 1024UL * 1024UL;
        break;
      default:
        return std::unexpected(
            ConfigError{"vmstat: -S requires k, K, m or M (default is KiB)"});
    }
    cfg.unit_char = c;
  }

  // Positionals: [delay [count]], parsed with strtol_or_err rules.
  auto parse_number = [](std::string_view text, long& out) -> bool {
    if (text.empty()) return false;
    const std::string str(text);
    char* end = nullptr;
    out = std::strtol(str.c_str(), &end, 10);
    return end != str.c_str() && *end == '\0';
  };
  if (!ctx.positionals.empty()) {
    long delay = 0;
    if (!parse_number(ctx.positionals[0], delay)) {
      return std::unexpected(ConfigError{"vmstat: failed to parse argument: '" +
                                         std::string(ctx.positionals[0]) +
                                         "'"});
    }
    if (delay < 1) {
      return std::unexpected(
          ConfigError{"vmstat: delay must be positive integer"});
    }
    if (static_cast<unsigned long>(delay) > 0xFFFFFFFFUL) {
      return std::unexpected(ConfigError{"vmstat: too large delay value"});
    }
    cfg.has_delay = true;
    cfg.delay = static_cast<unsigned long>(delay);
    cfg.infinite = true;
    cfg.num_updates = 1;
  }
  if (ctx.positionals.size() >= 2) {
    long count = 0;
    if (!parse_number(ctx.positionals[1], count)) {
      return std::unexpected(ConfigError{"vmstat: failed to parse argument: '" +
                                         std::string(ctx.positionals[1]) +
                                         "'"});
    }
    cfg.num_updates = count;
    cfg.infinite = false;
  }
  if (ctx.positionals.size() > 2) {
    return std::unexpected(ConfigError{"vmstat: too many arguments"});
  }
  return cfg;
}

// ===========================================================================
// Windows data sources
// ===========================================================================

using NtQuerySystemInformationFn = long(WINAPI*)(unsigned long, void*,
                                                 unsigned long, unsigned long*);

auto nt_query_system_information() -> NtQuerySystemInformationFn {
  static NtQuerySystemInformationFn fn = []() {
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) return static_cast<NtQuerySystemInformationFn>(nullptr);
    return reinterpret_cast<NtQuerySystemInformationFn>(
        GetProcAddress(ntdll, "NtQuerySystemInformation"));
  }();
  return fn;
}

auto nt_query(unsigned long info_class, unsigned long initial_size)
    -> std::vector<std::byte> {
  auto fn = nt_query_system_information();
  if (!fn) return {};
  std::vector<std::byte> buffer(initial_size);
  unsigned long needed = 0;
  long status = fn(info_class, buffer.data(),
                   static_cast<unsigned long>(buffer.size()), &needed);
  for (int attempt = 0; status == static_cast<long>(0xC0000004L) && attempt < 6;
       ++attempt) {
    buffer.resize(needed > 0 ? needed : buffer.size() * 2);
    status = fn(info_class, buffer.data(),
                static_cast<unsigned long>(buffer.size()), &needed);
  }
  if (status != 0) return {};
  return buffer;
}

struct Sample {
  unsigned long long mem_used_swap_kib = 0;
  unsigned long long mem_free_kib = 0;
  unsigned long long mem_buffer_kib = 0;  // no Windows split; stays 0
  unsigned long long mem_cache_kib = 0;
  unsigned long long mem_active_kib = 0;
  unsigned long long mem_inactive_kib = 0;  // no Windows source; stays 0
  unsigned long long swap_total_kib = 0;
  unsigned long long swap_used_kib = 0;
  unsigned long long swap_free_kib = 0;
  unsigned long long phys_total_kib = 0;
  unsigned long long phys_used_kib = 0;
  // Cumulative totals (since boot).
  unsigned long long pswpin_pages = 0;   // no cumulative source; stays 0
  unsigned long long pswpout_pages = 0;  // no cumulative source; stays 0
  unsigned long long pgpgin_kib = 0;
  unsigned long long pgpgout_kib = 0;
  unsigned long long interrupts = 0;
  unsigned long long ctx_switches = 0;
  // CPU times in 100ns units (GetSystemTimes semantics).
  unsigned long long cpu_user = 0;
  unsigned long long cpu_sys = 0;  // kernel time excluding idle
  unsigned long long cpu_idle = 0;
  unsigned long procs_running = 0;
  unsigned long procs_blocked = 0;
  unsigned long process_count = 0;
  unsigned long long uptime_seconds = 1;
};

auto filetime_ticks(const FILETIME& ft) -> unsigned long long {
  ULARGE_INTEGER value{};
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return value.QuadPart;
}

// SYSTEM_PROCESS_INFORMATION fixed part is 256 bytes on 64-bit Windows,
// followed by NumberOfThreads SYSTEM_THREAD_INFORMATION entries of 80
// bytes. Only the fields below are read (documented layout, phnt).
struct VmThreadEntry {
  long long kernel_time;
  long long user_time;
  long long create_time;
  unsigned long wait_time;
  unsigned long reserved0;
  void* start_address;
  void* unique_process;
  void* unique_thread;
  long priority;
  long base_priority;
  unsigned long context_switches;
  unsigned long thread_state;
  unsigned long wait_reason;
  unsigned long reserved1;
};
static_assert(sizeof(VmThreadEntry) == 80);
static_assert(offsetof(VmThreadEntry, context_switches) == 64);
static_assert(offsetof(VmThreadEntry, thread_state) == 68);
static_assert(offsetof(VmThreadEntry, wait_reason) == 72);
inline constexpr size_t kVmProcessHeaderSize = 256;

auto take_sample() -> Sample {
  Sample sample;
  sample.uptime_seconds = (GetTickCount64() / 1000) + 1;

  MEMORYSTATUSEX mem{};
  mem.dwLength = sizeof(mem);
  if (GlobalMemoryStatusEx(&mem)) {
    sample.phys_total_kib = mem.ullTotalPhys / 1024;
    sample.phys_used_kib = (mem.ullTotalPhys - mem.ullAvailPhys) / 1024;
    sample.mem_free_kib = mem.ullAvailPhys / 1024;
    sample.swap_total_kib = mem.ullTotalPageFile / 1024;
    sample.swap_free_kib = mem.ullAvailPageFile / 1024;
    sample.swap_used_kib = sample.swap_total_kib - sample.swap_free_kib;
    sample.mem_used_swap_kib = sample.swap_used_kib;
    sample.mem_active_kib = sample.phys_used_kib;
  }

  // System file cache size (SystemFileCacheInformation = 21); the
  // CurrentSize field is in bytes on current Windows releases.
  struct FileCacheInfo {
    size_t current_size;
    size_t peak_size;
    unsigned long page_fault_count;
    size_t minimum_working_set;
    size_t maximum_working_set;
    size_t current_size_including_transition;
    size_t peak_size_including_transition;
    unsigned long transition_reallocation_count;
    unsigned long flags;
  };
  auto cache_buffer = nt_query(21, sizeof(FileCacheInfo));
  if (cache_buffer.size() >= sizeof(FileCacheInfo)) {
    const auto* info =
        reinterpret_cast<const FileCacheInfo*>(cache_buffer.data());
    sample.mem_cache_kib = info->current_size / 1024;
  }

  // Io transfer totals are the first LARGE_INTEGER fields of
  // SYSTEM_PERFORMANCE_INFORMATION (SystemPerformanceInformation = 2).
  struct PerformanceHead {
    long long idle_process_time;
    long long io_read_transfer_count;
    long long io_write_transfer_count;
    long long io_other_transfer_count;
  };
  auto perf_buffer = nt_query(2, 4096);
  if (perf_buffer.size() >= sizeof(PerformanceHead)) {
    const auto* perf =
        reinterpret_cast<const PerformanceHead*>(perf_buffer.data());
    sample.pgpgin_kib =
        static_cast<unsigned long long>(perf->io_read_transfer_count) / 1024;
    sample.pgpgout_kib =
        static_cast<unsigned long long>(perf->io_write_transfer_count) / 1024;
  }

  // Per-processor interrupt totals (SystemProcessorPerformance-
  // Information = 1; layout per the documented
  // SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION in winternl.h).
  struct ProcessorPerformanceInfo {
    long long idle_time;
    long long kernel_time;
    long long user_time;
    long long dpc_time;
    long long interrupt_time;
    unsigned long interrupt_count;
    unsigned long reserved;
  };
  static_assert(sizeof(ProcessorPerformanceInfo) == 48);
  auto cpu_buffer = nt_query(1, sizeof(ProcessorPerformanceInfo) * 64);
  if (!cpu_buffer.empty()) {
    const size_t count = cpu_buffer.size() / sizeof(ProcessorPerformanceInfo);
    const auto* cpus =
        reinterpret_cast<const ProcessorPerformanceInfo*>(cpu_buffer.data());
    for (size_t i = 0; i < count; ++i) {
      sample.interrupts += cpus[i].interrupt_count;
    }
  }

  FILETIME idle{};
  FILETIME kernel{};
  FILETIME user{};
  if (GetSystemTimes(&idle, &kernel, &user)) {
    sample.cpu_idle = filetime_ticks(idle);
    sample.cpu_user = filetime_ticks(user);
    const auto kernel_total = filetime_ticks(kernel);
    sample.cpu_sys =
        kernel_total > sample.cpu_idle ? kernel_total - sample.cpu_idle : 0;
  }

  // Thread states / context switches (SystemProcessInformation = 5).
  auto procs_buffer = nt_query(5, 1 << 20);
  if (!procs_buffer.empty()) {
    const std::byte* base = procs_buffer.data();
    const std::byte* limit = base + procs_buffer.size();
    size_t offset = 0;
    while (base + offset + 8 <= limit) {
      const std::byte* entry = base + offset;
      unsigned long next_offset = 0;
      unsigned long thread_count = 0;
      std::memcpy(&next_offset, entry, sizeof(next_offset));
      std::memcpy(&thread_count, entry + 4, sizeof(thread_count));
      ++sample.process_count;
      const std::byte* threads = entry + kVmProcessHeaderSize;
      if (threads > limit) break;
      if (next_offset != 0 && entry + next_offset > limit) break;
      const size_t available = static_cast<size_t>(
          (next_offset != 0 ? entry + next_offset : limit) - threads);
      const size_t readable =
          std::min<size_t>(thread_count, available / sizeof(VmThreadEntry));
      for (size_t i = 0; i < readable; ++i) {
        const auto* thread =
            reinterpret_cast<const VmThreadEntry*>(threads) + i;
        sample.ctx_switches += thread->context_switches;
        switch (thread->thread_state) {
          case 1:  // Ready
          case 2:  // Running
          case 3:  // Standby
          case 7:  // DeferredReady
            ++sample.procs_running;
            break;
          case 5:  // Waiting
            switch (thread->wait_reason) {
              case 1:   // FreePage
              case 2:   // PageIn
              case 8:   // WrFreePage
              case 9:   // WrPageIn
              case 23:  // WrProcessInSwap
                ++sample.procs_blocked;
                break;
              default:
                break;
            }
            break;
          default:
            break;
        }
      }
      if (next_offset == 0) break;
      offset += next_offset;
    }
  }
  if (sample.process_count == 0) {
    sample.process_count = enumerate_win32_processes(false).size();
  }
  return sample;
}

// procps unitConvert(): (double)size / dataUnit * 1024 with size in
// KiB, truncated to unsigned long.
auto unit_convert(const Config& cfg, unsigned long long size_kib)
    -> unsigned long long {
  const double converted =
      static_cast<double>(size_kib) / cfg.data_unit * 1024.0;
  return static_cast<unsigned long long>(converted);
}

auto timestamp_now() -> std::string {
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char buffer[32];
  std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &local);
  return buffer;
}

auto timezone_name() -> std::string {
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char buffer[64];
  if (std::strftime(buffer, sizeof(buffer), "%Z", &local) == 0) return "";
  std::string name = buffer;
  if (name.size() > 19) name.resize(19);  // upstream truncates to 19
  return name;
}

auto console_height() -> long {
  long rows = 24;
  CONSOLE_SCREEN_BUFFER_INFO info{};
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out != nullptr && out != INVALID_HANDLE_VALUE &&
      GetConsoleScreenBufferInfo(out, &info)) {
    rows = info.srWindow.Bottom - info.srWindow.Top + 1;
    if (rows <= 0) rows = 24;
  }
  const long height = rows - 3;
  return height > 0 ? height : 22;
}

template <typename... Args>
auto format_line(std::format_string<Args...> fmt, Args&&... args)
    -> std::string {
  return std::format(fmt, std::forward<Args>(args)...);
}

// ===========================================================================
// Report mode (vmstat.c new_format)
// ===========================================================================

auto print_report_header(const Config& cfg) -> void {
  std::string line1 =
      cfg.wide
          ? "--procs-- -----------------------memory---------------------- "
            "---swap-- -----io---- -system-- ----------cpu----------"
          : "procs -----------memory---------- ---swap-- -----io---- "
            "-system-- -------cpu-------";
  if (cfg.timestamp) line1 += " -----timestamp-----";
  safePrintLn(line1);
  std::string line2 =
      cfg.wide ? format_line(
                     "{:>4} {:>4} {:>12} {:>12} {:>12} {:>12} {:>4} {:>4} "
                     "{:>5} {:>5} {:>4} {:>4} {:>3} {:>3} {:>3} {:>3} {:>3} "
                     "{:>3}",
                     "r", "b", "swpd", "free", cfg.active ? "inact" : "buff",
                     cfg.active ? "active" : "cache", "si", "so", "bi", "bo",
                     "in", "cs", "us", "sy", "id", "wa", "st", "gu")
               : format_line(
                     "{:>2} {:>2} {:>6} {:>6} {:>6} {:>6} {:>4} {:>4} {:>5} "
                     "{:>5} {:>4} {:>4} {:>2} {:>2} {:>2} {:>2} {:>2} {:>2}",
                     "r", "b", "swpd", "free", cfg.active ? "inact" : "buff",
                     cfg.active ? "active" : "cache", "si", "so", "bi", "bo",
                     "in", "cs", "us", "sy", "id", "wa", "st", "gu");
  if (cfg.timestamp) {
    // Upstream right-aligns the zone name in a 19-wide field.
    const auto tz = timezone_name();
    line2 += format_line(" {:>19}", tz);
  }
  safePrintLn(line2);
}

auto report_row(const Config& cfg, const Sample& sample, unsigned long long r,
                unsigned long long b, unsigned long long si,
                unsigned long long so, unsigned long long bi,
                unsigned long long bo, unsigned long long in,
                unsigned long long cs, unsigned long long us,
                unsigned long long sy, unsigned long long id,
                unsigned long long wa, unsigned long long st,
                unsigned long long gu) -> std::string {
  std::string row =
      cfg.wide ? format_line(
                     "{:4} {:4} {:12} {:12} {:12} {:12} {:4} {:4} {:5} {:5} "
                     "{:4} {:4} {:3} {:3} {:3} {:3} {:3} {:3}",
                     r, b, unit_convert(cfg, sample.mem_used_swap_kib),
                     unit_convert(cfg, sample.mem_free_kib),
                     unit_convert(cfg, cfg.active ? sample.mem_inactive_kib
                                                  : sample.mem_buffer_kib),
                     unit_convert(cfg, cfg.active ? sample.mem_active_kib
                                                  : sample.mem_cache_kib),
                     si, so, bi, bo, in, cs, us, sy, id, wa, st, gu)
               : format_line(
                     "{:2} {:2} {:6} {:6} {:6} {:6} {:4} {:4} {:5} {:5} {:4} "
                     "{:4} {:2} {:2} {:2} {:2} {:2} {:2}",
                     r, b, unit_convert(cfg, sample.mem_used_swap_kib),
                     unit_convert(cfg, sample.mem_free_kib),
                     unit_convert(cfg, cfg.active ? sample.mem_inactive_kib
                                                  : sample.mem_buffer_kib),
                     unit_convert(cfg, cfg.active ? sample.mem_active_kib
                                                  : sample.mem_cache_kib),
                     si, so, bi, bo, in, cs, us, sy, id, wa, st, gu);
  if (cfg.timestamp) row += " " + timestamp_now();
  return row;
}

// CPU percentages with the upstream rounding: (100*x + Div/2) / Div,
// guest folded into user upstream and subtracted there; Windows
// exposes no guest time, so user is used as-is.
auto cpu_percents(const Sample& s, unsigned long long& us,
                  unsigned long long& sy, unsigned long long& id,
                  unsigned long long& wa, unsigned long long& st,
                  unsigned long long& gu) -> void {
  unsigned long long div = s.cpu_user + s.cpu_sys + s.cpu_idle;
  if (div == 0) {
    div = 1;
    us = sy = wa = st = gu = 0;
    id = 100;
    return;
  }
  const unsigned long long half = div / 2;
  us = (100 * s.cpu_user + half) / div;
  sy = (100 * s.cpu_sys + half) / div;
  id = (100 * s.cpu_idle + half) / div;
  wa = 0;
  st = 0;
  gu = 0;
}

auto delta_of(unsigned long long now, unsigned long long before)
    -> unsigned long long {
  return now >= before ? now - before : 0;
}

auto run_report(const Config& cfg) -> int {
  const bool more_headers = !cfg.one_header;
  const long height = console_height();
  Sample previous = take_sample();

  print_report_header(cfg);
  if (!cfg.no_first) {
    // First row: since-boot averages, exactly as vmstat.c new_format.
    const Sample& s = previous;
    unsigned long long us, sy, id, wa, st, gu;
    cpu_percents(s, us, sy, id, wa, st, gu);
    const auto uptime = s.uptime_seconds;
    // Upstream divides total context switches by Div in USER_HZ
    // ticks; the sampled CPU times are 100ns units (100000 per tick).
    unsigned long long div_ticks =
        (s.cpu_user + s.cpu_sys + s.cpu_idle) / 100000;
    if (div_ticks == 0) div_ticks = 1;
    safePrintLn(report_row(cfg, s, s.procs_running, s.procs_blocked,
                           unit_convert(cfg, s.pswpin_pages * 4) / uptime,
                           unit_convert(cfg, s.pswpout_pages * 4) / uptime,
                           s.pgpgin_kib / uptime, s.pgpgout_kib / uptime,
                           s.interrupts / uptime, s.ctx_switches / div_ticks,
                           us, sy, id, wa, st, gu));
  }

  const long total_rows = cfg.no_first ? cfg.num_updates + 1 : cfg.num_updates;
  for (long i = 1; cfg.infinite || i < total_rows; ++i) {
    Sleep(static_cast<DWORD>(cfg.delay * 1000));
    Sample current = take_sample();
    if (more_headers && height > 0 && (i % height) == 0) {
      print_report_header(cfg);
    }
    Sample delta;
    delta.cpu_user = delta_of(current.cpu_user, previous.cpu_user);
    delta.cpu_sys = delta_of(current.cpu_sys, previous.cpu_sys);
    delta.cpu_idle = delta_of(current.cpu_idle, previous.cpu_idle);
    unsigned long long us, sy, id, wa, st, gu;
    cpu_percents(delta, us, sy, id, wa, st, gu);
    const unsigned long long half = cfg.delay / 2;
    auto rate = [&](unsigned long long d) -> unsigned long long {
      return cfg.delay > 0 ? (d + half) / cfg.delay : d;
    };
    safePrintLn(report_row(
        cfg, current, current.procs_running, current.procs_blocked,
        rate(unit_convert(
            cfg, delta_of(current.pswpin_pages, previous.pswpin_pages) * 4)),
        rate(unit_convert(
            cfg, delta_of(current.pswpout_pages, previous.pswpout_pages) * 4)),
        rate(delta_of(current.pgpgin_kib, previous.pgpgin_kib)),
        rate(delta_of(current.pgpgout_kib, previous.pgpgout_kib)),
        rate(delta_of(current.interrupts, previous.interrupts)),
        rate(delta_of(current.ctx_switches, previous.ctx_switches)), us, sy, id,
        wa, st, gu));
    previous = current;
  }
  return 0;
}

// ===========================================================================
// -s event counter statistics (vmstat.c sum_format)
// ===========================================================================

auto run_stats(const Config& cfg) -> int {
  const Sample s = take_sample();
  const unsigned long long ticks = 100000;  // 100ns units per USER_HZ tick
  auto out = format_line("{:13} {} total memory\n",
                         unit_convert(cfg, s.phys_total_kib), cfg.unit_char);
  out += format_line("{:13} {} used memory\n",
                     unit_convert(cfg, s.phys_used_kib), cfg.unit_char);
  out += format_line("{:13} {} active memory\n",
                     unit_convert(cfg, s.mem_active_kib), cfg.unit_char);
  out += format_line("{:13} {} inactive memory\n",
                     unit_convert(cfg, s.mem_inactive_kib), cfg.unit_char);
  out += format_line("{:13} {} free memory\n",
                     unit_convert(cfg, s.mem_free_kib), cfg.unit_char);
  out += format_line("{:13} {} buffer memory\n",
                     unit_convert(cfg, s.mem_buffer_kib), cfg.unit_char);
  out += format_line("{:13} {} swap cache\n",
                     unit_convert(cfg, s.mem_cache_kib), cfg.unit_char);
  out += format_line("{:13} {} total swap\n",
                     unit_convert(cfg, s.swap_total_kib), cfg.unit_char);
  out += format_line("{:13} {} used swap\n", unit_convert(cfg, s.swap_used_kib),
                     cfg.unit_char);
  out += format_line("{:13} {} free swap\n", unit_convert(cfg, s.swap_free_kib),
                     cfg.unit_char);
  out += format_line("{:13} non-nice user cpu ticks\n", s.cpu_user / ticks);
  out += format_line("{:13} nice user cpu ticks\n", 0ULL);
  out += format_line("{:13} system cpu ticks\n", s.cpu_sys / ticks);
  out += format_line("{:13} idle cpu ticks\n", s.cpu_idle / ticks);
  out += format_line("{:13} IO-wait cpu ticks\n", 0ULL);
  out += format_line("{:13} IRQ cpu ticks\n", 0ULL);
  out += format_line("{:13} softirq cpu ticks\n", 0ULL);
  out += format_line("{:13} stolen cpu ticks\n", 0ULL);
  out += format_line("{:13} non-nice guest cpu ticks\n", 0ULL);
  out += format_line("{:13} nice guest cpu ticks\n", 0ULL);
  out += format_line("{:13} K paged in\n", s.pgpgin_kib);
  out += format_line("{:13} K paged out\n", s.pgpgout_kib);
  out += format_line("{:13} pages swapped in\n", s.pswpin_pages);
  out += format_line("{:13} pages swapped out\n", s.pswpout_pages);
  out += format_line("{:13} interrupts\n", s.interrupts);
  out += format_line("{:13} CPU context switches\n", s.ctx_switches);
  const unsigned long long now_epoch =
      static_cast<unsigned long long>(std::time(nullptr));
  out += format_line("{:13} boot time\n", now_epoch - (s.uptime_seconds - 1));
  // Windows has no cumulative process-creation counter in the
  // documented APIs used here; the current process count is reported
  // as the platform approximation (see REPORT.md).
  out += format_line("{:13} forks\n", s.process_count);
  safePrint(out);
  return 0;
}

// ===========================================================================
// Disk modes (vmstat.c diskformat / disksum_format / partition)
// ===========================================================================

struct DiskStats {
  int index = 0;
  unsigned long long reads = 0;
  unsigned long long read_sectors = 0;
  unsigned long long read_ms = 0;
  unsigned long long writes = 0;
  unsigned long long write_sectors = 0;
  unsigned long long write_ms = 0;
  unsigned long long queue_depth = 0;
};

auto read_disk_stats(int index) -> std::optional<DiskStats> {
  const auto path = L"\\\\.\\PhysicalDrive" + std::to_wstring(index);
  HANDLE raw = CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
  if (raw == INVALID_HANDLE_VALUE) return std::nullopt;
  UniqueHandle disk(raw);
  DISK_PERFORMANCE perf{};
  DWORD returned = 0;
  if (!DeviceIoControl(disk.get(), IOCTL_DISK_PERFORMANCE, nullptr, 0, &perf,
                       sizeof(perf), &returned, nullptr)) {
    return std::nullopt;
  }
  DiskStats stats;
  stats.index = index;
  stats.reads = perf.ReadCount;
  stats.read_sectors =
      static_cast<unsigned long long>(perf.BytesRead.QuadPart) / 512;
  stats.read_ms =
      static_cast<unsigned long long>(perf.ReadTime.QuadPart) / 10000;
  stats.writes = perf.WriteCount;
  stats.write_sectors =
      static_cast<unsigned long long>(perf.BytesWritten.QuadPart) / 512;
  stats.write_ms =
      static_cast<unsigned long long>(perf.WriteTime.QuadPart) / 10000;
  stats.queue_depth = perf.QueueDepth;
  return stats;
}

auto enumerate_disks() -> std::vector<DiskStats> {
  std::vector<DiskStats> disks;
  int misses = 0;
  for (int i = 0; i < 32 && misses < 4; ++i) {
    if (auto stats = read_disk_stats(i)) {
      disks.push_back(*stats);
      misses = 0;
    } else {
      ++misses;
    }
  }
  return disks;
}

auto print_disk_header(const Config& cfg) -> void {
  std::string line1 =
      cfg.wide ? "disk- -------------------reads------------------- "
                 "-------------------writes------------------ ------IO-------"
               : "disk- ------------reads------------ "
                 "------------writes----------- -----IO------";
  if (cfg.timestamp) line1 += " -----timestamp-----";
  safePrintLn(line1);
  std::string line2 =
      cfg.wide ? format_line(
                     "{:>5} {:>9} {:>9} {:>11} {:>11} {:>9} {:>9} {:>11} "
                     "{:>11} {:>7} {:>7}",
                     " ", "total", "merged", "sectors", "ms", "total", "merged",
                     "sectors", "ms", "cur", "sec")
               : format_line(
                     "{:>5} {:>6} {:>6} {:>7} {:>7} {:>6} {:>6} {:>7} {:>7} "
                     "{:>6} {:>6}",
                     " ", "total", "merged", "sectors", "ms", "total", "merged",
                     "sectors", "ms", "cur", "sec");
  if (cfg.timestamp) {
    const auto tz = timezone_name();
    line2 += format_line(" {:>19}", tz);
  }
  safePrintLn(line2);
}

auto run_disk(const Config& cfg) -> int {
  const bool more_headers = !cfg.one_header;
  const long height = console_height();
  print_disk_header(cfg);
  for (long i = 0; cfg.infinite || i < cfg.num_updates; ++i) {
    if (i > 0) Sleep(static_cast<DWORD>(cfg.delay * 1000));
    auto disks = enumerate_disks();
    for (size_t j = 0; j < disks.size(); ++j) {
      if (more_headers && height > 0 && j > 0 &&
          (static_cast<long>(j) % height) == 0) {
        print_disk_header(cfg);
      }
      const auto& d = disks[j];
      const std::string name = "PhysicalDrive" + std::to_string(d.index);
      std::string row =
          cfg.wide ? format_line(
                         "{:<5} {:9} {:9} {:11} {:11} {:9} {:9} {:11} {:11} "
                         "{:7} {:7}",
                         name, d.reads, 0ULL, d.read_sectors, d.read_ms,
                         d.writes, 0ULL, d.write_sectors, d.write_ms,
                         d.queue_depth / 1000, (d.read_ms + d.write_ms) / 1000)
                   : format_line(
                         "{:<5} {:6} {:6} {:7} {:7} {:6} {:6} {:7} {:7} {:6} "
                         "{:6}",
                         name, d.reads, 0ULL, d.read_sectors, d.read_ms,
                         d.writes, 0ULL, d.write_sectors, d.write_ms,
                         d.queue_depth / 1000, (d.read_ms + d.write_ms) / 1000);
      if (cfg.timestamp) row += " " + timestamp_now();
      safePrintLn(row);
    }
  }
  return 0;
}

auto run_disk_sum(const Config& /*cfg*/) -> int {
  auto disks = enumerate_disks();
  unsigned long long reads = 0, read_sectors = 0, milli_reading = 0;
  unsigned long long writes = 0, written_sectors = 0, milli_writing = 0;
  unsigned long long inprogress = 0, milli_spent = 0, milli_weighted = 0;
  for (const auto& d : disks) {
    reads += d.reads;
    read_sectors += d.read_sectors;
    milli_reading += d.read_ms;
    writes += d.writes;
    written_sectors += d.write_sectors;
    milli_writing += d.write_ms;
    inprogress += d.queue_depth / 1000;
    // vmstat.c divides libproc's microsecond-based counters by 1000
    // for these two sums; the DISK_PERFORMANCE times here are already
    // milliseconds, so they sum directly. No separate weighted-time
    // counter exists, so weighted tracks spent.
    milli_spent += d.read_ms + d.write_ms;
    milli_weighted += d.read_ms + d.write_ms;
  }
  DWORD drive_mask = GetLogicalDrives();
  int partitions = 0;
  while (drive_mask != 0) {
    partitions += static_cast<int>(drive_mask & 1);
    drive_mask >>= 1;
  }
  std::string out =
      format_line("{:13} disks\n", static_cast<int>(disks.size()));
  out += format_line("{:13} partitions\n", partitions);
  out += format_line("{:13} total reads\n", reads);
  out += format_line("{:13} merged reads\n", 0ULL);
  out += format_line("{:13} read sectors\n", read_sectors);
  out += format_line("{:13} milli reading\n", milli_reading);
  out += format_line("{:13} writes\n", writes);
  out += format_line("{:13} merged writes\n", 0ULL);
  out += format_line("{:13} written sectors\n", written_sectors);
  out += format_line("{:13} milli writing\n", milli_writing);
  out += format_line("{:13} inprogress IO\n", inprogress);
  out += format_line("{:13} milli spent IO\n", milli_spent);
  out += format_line("{:13} milli weighted IO\n", milli_weighted);
  safePrint(out);
  return 0;
}

auto iequals_prefix(const std::string& text, const std::string& prefix)
    -> bool {
  if (text.size() < prefix.size()) return false;
  for (size_t i = 0; i < prefix.size(); ++i) {
    char a = text[i];
    char b = prefix[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
    if (a != b) return false;
  }
  return true;
}

auto resolve_partition_disk(const Config& cfg) -> std::optional<int> {
  const std::string& name = cfg.partition;
  // PhysicalDriveN spelled out (any case) selects the disk directly.
  const std::string prefix = "physicaldrive";
  if (name.size() > prefix.size() && iequals_prefix(name, prefix)) {
    const auto rest = name.substr(prefix.size());
    if (!rest.empty() && std::ranges::all_of(rest, [](char c) {
          return c >= '0' && c <= '9';
        })) {
      return std::stoi(rest);
    }
  }
  // A volume designator (C, C:, C:\, or any volume path) resolves to
  // its first physical extent.
  std::wstring volume = utf8_to_wstring(name);
  if (volume.size() == 1) volume += L':';
  if (!volume.starts_with(L"\\\\")) volume = L"\\\\.\\" + volume;
  while (!volume.empty() && (volume.back() == L'\\' || volume.back() == L'/') &&
         volume.size() > 4) {
    volume.pop_back();
  }
  HANDLE raw = CreateFileW(volume.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
  if (raw == INVALID_HANDLE_VALUE) return std::nullopt;
  UniqueHandle handle(raw);
  std::vector<std::byte> buffer(sizeof(VOLUME_DISK_EXTENTS) +
                                sizeof(DISK_EXTENT) * 8);
  DWORD returned = 0;
  if (!DeviceIoControl(handle.get(), IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                       nullptr, 0, buffer.data(),
                       static_cast<DWORD>(buffer.size()), &returned, nullptr)) {
    return std::nullopt;
  }
  const auto* extents =
      reinterpret_cast<const VOLUME_DISK_EXTENTS*>(buffer.data());
  if (extents->NumberOfDiskExtents < 1) return std::nullopt;
  return static_cast<int>(extents->Extents[0].DiskNumber);
}

auto run_partition(const Config& cfg) -> int {
  auto disk = resolve_partition_disk(cfg);
  if (!disk) {
    safeErrorPrintLn("vmstat: Disk/Partition " + cfg.partition + " not found");
    return 1;
  }
  safePrintLn(format_line("{:<10} {:>10}  {:>16}  {:>10}  {:>16}",
                          cfg.partition, "reads", "read sectors", "writes",
                          "requested writes"));
  for (long i = 0; cfg.infinite || i < cfg.num_updates; ++i) {
    if (i > 0) Sleep(static_cast<DWORD>(cfg.delay * 1000));
    auto stats = read_disk_stats(*disk);
    if (!stats) {
      safeErrorPrintLn("vmstat: Disk/Partition " + cfg.partition +
                       " not found");
      return 1;
    }
    safePrintLn(format_line("{:21}  {:16}  {:10}  {:16}", stats->reads,
                            stats->read_sectors, stats->writes,
                            stats->write_sectors));
  }
  return 0;
}

auto run_slabs(const Config& cfg) -> int {
  // Windows has no slab allocator; upstream prints the header and one
  // row per cache. Only the header can be honored here.
  safePrintLn(format_line("{:<24} {:>6} {:>6} {:>6} {:>6}", "Cache", "Num",
                          "Total", "Size", "Pages"));
  for (long i = 1; cfg.infinite || i < cfg.num_updates; ++i) {
    Sleep(static_cast<DWORD>(cfg.delay * 1000));
  }
  return 0;
}

}  // namespace vmstat_pipeline

REGISTER_COMMAND(vmstat, "vmstat", "vmstat [OPTION]... [delay [count]]",
                 "Report virtual memory statistics.",
                 "  vmstat\n"
                 "  vmstat 1 5\n"
                 "  vmstat -s\n"
                 "  vmstat -d",
                 "free(1), ps(1), top(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", VMSTAT_OPTIONS) {
  using namespace vmstat_pipeline;

  // vmstat.c runs fork_format() and exits during option processing,
  // before modes and positionals are judged.
  if (ctx.has("-f") || ctx.has("--forks")) {
    const auto processes = enumerate_win32_processes(false);
    safePrintLn(format_line("{:13} forks\n",
                            static_cast<unsigned long long>(processes.size())));
    return 0;
  }

  auto built = build_config(ctx);
  if (!built) {
    safeErrorPrintLn(built.error().message);
    safeErrorPrintLn("Try 'vmstat --help' for more information.");
    return 1;
  }
  const Config& cfg = *built;
  switch (cfg.mode) {
    case Mode::kReport:
      return run_report(cfg);
    case Mode::kStats:
      return run_stats(cfg);
    case Mode::kDisk:
      return run_disk(cfg);
    case Mode::kDiskSum:
      return run_disk_sum(cfg);
    case Mode::kPartition:
      return run_partition(cfg);
    case Mode::kSlabs:
      return run_slabs(cfg);
  }
  return 0;
}
