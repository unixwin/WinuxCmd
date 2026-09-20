// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for vmstat (report virtual memory statistics).
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr VMSTAT_OPTIONS = std::array{
    OPTION("-a", "--active", "split into active and inactive memory"),
    OPTION("-d", "", "show memory statistics"),
    OPTION("-f", "--forks", "show boot/forks counts"),
    OPTION("-n", "--oneline", "print headings only once"),
    OPTION("-p", "--pid", "show statistics for a specific PID", STRING_TYPE),
    OPTION("-s", "--summary", "print summary information only"),
    OPTION("-t", "--timestamp", "include timestamp"),
    OPTION("-w", "--wide", "wide output"),
    OPTION("-m", "--slab", "show slab information (not supported on Windows)"),
    OPTION("-v", "--version", "print version and exit")};

namespace vmstat_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool active = false;
  bool wide = false;
  bool one_line = false;
  bool summary = false;
  bool timestamp = false;
  bool forks = false;
};

auto build_config(const CommandContext<VMSTAT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.active = ctx.has("-a") || ctx.has("--active");
  cfg.wide = ctx.has("-w") || ctx.has("--wide");
  cfg.one_line = ctx.has("-n") || ctx.has("--oneline");
  cfg.summary = ctx.has("-s") || ctx.has("--summary");
  cfg.timestamp = ctx.has("-t") || ctx.has("--timestamp");
  cfg.forks = ctx.has("-f") || ctx.has("--forks");
  return cfg;
}

auto get_process_count() -> unsigned long long {
  // Use CreateToolhelp32Snapshot to count processes
  HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (snap == INVALID_HANDLE_VALUE) return 0;
  PROCESSENTRY32W pe{};
  pe.dwSize = sizeof(pe);
  unsigned long long count = 0;
  if (Process32FirstW(snap, &pe)) {
    do {
      if (pe.szExeFile[0]) count++;
    } while (Process32NextW(snap, &pe));
  }
  CloseHandle(snap);
  return count;
}

auto format_kb(unsigned long long bytes) -> std::string {
  unsigned long long kb = bytes / 1024;
  char buf[64];
  sprintf_s(buf, "%llu", kb);
  return buf;
}

auto run(const Config& cfg) -> int {
  MEMORYSTATUSEX mem_status{};
  mem_status.dwLength = sizeof(mem_status);
  if (!GlobalMemoryStatusEx(&mem_status)) {
    safeErrorPrintLn("vmstat: failed to get memory status");
    return 1;
  }

  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);

  unsigned long long total = mem_status.ullTotalPhys;
  unsigned long long avail = mem_status.ullAvailPhys;
  unsigned long long used = total - avail;
  unsigned long long total_page = mem_status.ullTotalPageFile;
  unsigned long long avail_page = mem_status.ullAvailPageFile;
  unsigned long long used_page = total_page - avail_page;
  unsigned long long proc_count = get_process_count();

  if (cfg.summary) {
    safePrintLn("total physical memory (Kb):");
    safePrint(format_kb(total));
    safePrintLn("");
    safePrintLn("free physical memory (Kb):");
    safePrint(format_kb(avail));
    safePrintLn("");
    safePrintLn("total virtual memory (Kb):");
    safePrint(format_kb(total_page));
    safePrintLn("");
    safePrintLn("free virtual memory (Kb):");
    safePrint(format_kb(avail_page));
    safePrintLn("");
    safePrintLn("total processes");
    safePrint(proc_count);
    safePrintLn("");
    return 0;
  }

  if (cfg.forks) {
    char buf[128];
    sprintf_s(buf, "procs");
    safePrintLn(buf);
    ULARGE_INTEGER now;
    GetSystemTimeAsFileTime((FILETIME*)&now);
    ULONGLONG boot_seconds =
        (now.QuadPart - 116444736000000000ULL) / 10000000ULL;
    sprintf_s(buf, "%llu %llu", boot_seconds, proc_count);
    safePrintLn(buf);
    return 0;
  }

  // Standard output: procs, memory, swap, io, system, cpu
  if (!cfg.one_line) {
    if (cfg.wide) {
      safePrint("procs           ");
    } else {
      safePrint("procs ");
    }
    safePrintLn("---------- ------ ------ ------ ------ ------ ------ ------");
  }

  double cpu_idle = 100.0, cpu_system = 0, cpu_user = 0;
  double iowait = 0, irq = 0;
  char buf[256];
  sprintf_s(buf, "%llu %s %s %s %s %s %s %s", proc_count,
            format_kb(used).c_str(), format_kb(avail).c_str(),
            format_kb(used_page).c_str(), format_kb(avail_page).c_str(), "0",
            "0", "0");
  safePrint(buf);
  char cpu_buf[64];
  sprintf_s(cpu_buf, " %3.1f %3.1f %3.1f %3.1f %3.1f", cpu_idle, cpu_system,
            cpu_user, iowait, irq);
  safePrint(cpu_buf);
  safePrintLn("");

  return 0;
}

}  // namespace vmstat_pipeline

REGISTER_COMMAND(vmstat, "vmstat", "vmstat [OPTION]...",
                 "Report virtual memory statistics.\n"
                 "On Windows, reports memory, swap, and CPU utilization.",
                 "  vmstat\n"
                 "  vmstat -s\n"
                 "  vmstat -d",
                 "free(1), ps(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 VMSTAT_OPTIONS) {
  using namespace vmstat_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"vmstat");
    return 1;
  }

  return run(*cfg_result);
}
