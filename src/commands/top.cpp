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
 *  - File: top.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - @contributor1 caomengxuan666 2507560089@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
///   - @description:
/// @Description: Implementation for top - display dynamic real-time information
/// about running processes.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"

#pragma comment(lib, "kernel32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ws2_32.lib")

#include <conio.h>
#include <time.h>
#include <winsock2.h>
#include <winternl.h>

#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

using namespace core::pipeline;

// Windows API for getting process command line
typedef NTSTATUS(NTAPI* pNtQueryInformationProcess)(
    HANDLE ProcessHandle, DWORD ProcessInformationClass,
    PVOID ProcessInformation, DWORD ProcessInformationLength,
    PDWORD ReturnLength);

constexpr DWORD ProcessCommandLineInformation = 60;

// ======================================================
// Data Structures
// ======================================================

/**
 * @brief Process information structure
 */
struct ProcessInfo {
  DWORD pid = 0;
  std::wstring name;
  std::string name_utf8;
  std::string username;
  float cpu_percent = 0.0f;
  SIZE_T memory_bytes = 0;          // RES: Working Set Size
  SIZE_T virtual_memory_bytes = 0;  // VIRT: Virtual Memory Size
  float memory_percent = 0.0f;
  FILETIME create_time;
  FILETIME kernel_time;
  FILETIME user_time;
  ULONGLONG total_cpu_time = 0;
  DWORD thread_count = 0;
  DWORD priority = 0;
  std::string full_path;
  std::wstring command_line;
  std::string command_line_utf8;
  LARGE_INTEGER timestamp;

  ProcessInfo() { QueryPerformanceCounter(&timestamp); }
};

/**
 * @brief System statistics structure
 */
struct SystemStats {
  float cpu_usage = 0.0f;
  SIZE_T total_memory = 0;
  SIZE_T available_memory = 0;
  SIZE_T used_memory = 0;
  float memory_usage = 0.0f;
  DWORD total_processes = 0;
  DWORD running_processes = 0;
  ULONGLONG total_uptime = 0;
  ULONGLONG total_idle_time = 0;
  size_t cpu_count = 0;
  SYSTEM_INFO sys_info;

  SystemStats() {
    GetSystemInfo(&sys_info);
    cpu_count = sys_info.dwNumberOfProcessors;
  }
};

/**
 * @brief CPU snapshot for calculating CPU usage
 */
struct CPUSnapshot {
  FILETIME idle_time;
  FILETIME kernel_time;
  FILETIME user_time;
  LARGE_INTEGER timestamp;

  CPUSnapshot() { QueryPerformanceCounter(&timestamp); }
};

/**
 * @brief Sort field enumeration
 */
enum class SortField { CPU, MEM, TIME, PID, NAME };

/**
 * @brief Pipeline configuration
 */
struct TopConfig {
  bool batch_mode = false;
  int delay = 3;
  int max_iterations = -1;
  SortField sort_field = SortField::CPU;
  bool should_exit = false;

  // Runtime interactive options
  bool show_full_command = false;
  bool show_threads = false;
  bool ignore_idle = false;
  std::string user_filter;
  int selected_pid = 0;
};

/**
 * @brief TOP command options
 */
constexpr auto TOP_OPTIONS = std::array{
    OPTION("-b", "--batch",
           "batch mode: don't accept input, run until -n iterations"),
    OPTION("-d", "--delay", "delay between updates, in seconds"),
    OPTION("-n", "--iterations", "number of iterations before exiting"),
    OPTION("-p", "--pid", "monitor only processes with given PIDs"),
    OPTION("-u", "--user", "monitor only processes with given user"),
    OPTION("-U", "--User", "monitor only processes with real user ID/name"),
    OPTION("-s", "--secure-mode", "secure mode: disables some features"),
    OPTION("-c", "--command", "show command line instead of process name"),
    OPTION("-H", "--threads", "show threads as if they were processes"),
    OPTION("-o", "--field-sort", "override sort field"),
    OPTION("-w", "--width", "override output width"),
    OPTION("-v", "--version", "print version information"),
    OPTION("-h", "--help", "display this help")};

// ======================================================
// Helper Functions
// ======================================================

/**
 * @brief Convert FILETIME to ULONGLONG
 */
auto fileTimeToULong(const FILETIME& ft) -> ULONGLONG {
  return (static_cast<ULONGLONG>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
}

/**
 * @brief Get user name from process handle
 */
auto getUserName(HANDLE hProcess) -> std::string {
  HANDLE hToken = nullptr;
  if (!OpenProcessToken(hProcess, TOKEN_QUERY, &hToken)) {
    return "N/A";
  }

  TOKEN_USER* pTokenUser = nullptr;
  DWORD dwSize = 0;
  if (!GetTokenInformation(hToken, TokenUser, nullptr, 0, &dwSize) &&
      GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
    CloseHandle(hToken);
    return "N/A";
  }

  pTokenUser = static_cast<TOKEN_USER*>(malloc(dwSize));
  if (!GetTokenInformation(hToken, TokenUser, pTokenUser, dwSize, &dwSize)) {
    free(pTokenUser);
    CloseHandle(hToken);
    return "N/A";
  }

  SID_NAME_USE sidUse = SidTypeUnknown;
  WCHAR szUserName[256] = {0};
  DWORD dwUserName = 256;
  WCHAR szDomainName[256] = {0};
  DWORD dwDomainName = 256;

  if (!LookupAccountSidW(nullptr, pTokenUser->User.Sid, szUserName, &dwUserName,
                         szDomainName, &dwDomainName, &sidUse)) {
    free(pTokenUser);
    CloseHandle(hToken);
    return "N/A";
  }

  free(pTokenUser);
  CloseHandle(hToken);

  return wstring_to_utf8(szUserName);
}

/**
 * @brief Get process memory information
 */
auto getProcessMemoryInfo(HANDLE hProcess, ProcessInfo& info) -> bool {
  PROCESS_MEMORY_COUNTERS_EX pmc;
  if (GetProcessMemoryInfo(hProcess,
                           reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&pmc),
                           sizeof(pmc))) {
    info.memory_bytes = pmc.WorkingSetSize;         // RES: Working Set Size
    info.virtual_memory_bytes = pmc.PagefileUsage;  // VIRT: Pagefile Usage
    return true;
  }
  return false;
}

/**
 * @brief Get thread count for all processes (optimized)
 */
auto getAllThreadCounts() -> std::unordered_map<DWORD, DWORD> {
  std::unordered_map<DWORD, DWORD> threadCounts;

  HANDLE hThreadSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
  if (hThreadSnapshot == INVALID_HANDLE_VALUE) {
    return threadCounts;
  }

  THREADENTRY32 te32;
  te32.dwSize = sizeof(THREADENTRY32);

  if (Thread32First(hThreadSnapshot, &te32)) {
    do {
      threadCounts[te32.th32OwnerProcessID]++;
    } while (Thread32Next(hThreadSnapshot, &te32));
  }

  CloseHandle(hThreadSnapshot);
  return threadCounts;
}

// ======================================================
// Process Enumerator
// ======================================================

class ProcessEnumerator {
 public:
  auto enumerate(bool getThreadCounts = false) -> std::vector<ProcessInfo> {
    std::vector<ProcessInfo> processes;

    // Get all thread counts once (only if needed for display)
    std::unordered_map<DWORD, DWORD> threadCounts;
    if (getThreadCounts) {
      threadCounts = getAllThreadCounts();
    }

    HANDLE hSnapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (hSnapshot == INVALID_HANDLE_VALUE) {
      return processes;
    }

    PROCESSENTRY32W pe32;
    pe32.dwSize = sizeof(PROCESSENTRY32W);

    if (!Process32FirstW(hSnapshot, &pe32)) {
      CloseHandle(hSnapshot);
      return processes;
    }

    do {
      ProcessInfo info;
      info.pid = pe32.th32ProcessID;
      info.name = pe32.szExeFile;
      info.name_utf8 = wstring_to_utf8(pe32.szExeFile);

      // Get thread count from map (optimized, only if needed)
      if (getThreadCounts) {
        auto it = threadCounts.find(info.pid);
        if (it != threadCounts.end()) {
          info.thread_count = it->second;
        } else {
          info.thread_count = 0;
        }
      } else {
        info.thread_count = 0;  // Not needed, set to 0
      }

      // Try to open process with minimum required access
      HANDLE hProcess = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                                    pe32.th32ProcessID);

      if (hProcess != nullptr) {
        info.username = getUserName(hProcess);

        // Get process times
        FILETIME ftCreate, ftExit, ftKernel, ftUser;
        if (GetProcessTimes(hProcess, &ftCreate, &ftExit, &ftKernel, &ftUser)) {
          info.create_time = ftCreate;
          info.kernel_time = ftKernel;
          info.user_time = ftUser;
          info.total_cpu_time =
              fileTimeToULong(ftKernel) + fileTimeToULong(ftUser);
        }

        // Get memory info
        getProcessMemoryInfo(hProcess, info);

        // Get priority
        info.priority = GetPriorityClass(hProcess);

        // Get command line
        HMODULE hNtDll = GetModuleHandleW(L"ntdll.dll");
        if (hNtDll) {
          pNtQueryInformationProcess NtQueryInformationProcess =
              (pNtQueryInformationProcess)GetProcAddress(
                  hNtDll, "NtQueryInformationProcess");

          if (NtQueryInformationProcess) {
            UNICODE_STRING* cmdLine = nullptr;
            ULONG retLen = 0;
            NTSTATUS status = NtQueryInformationProcess(
                hProcess, ProcessCommandLineInformation, &cmdLine,
                sizeof(UNICODE_STRING*), &retLen);

            if (status == 0 && cmdLine && cmdLine->Buffer) {
              info.command_line.assign(cmdLine->Buffer,
                                       cmdLine->Length / sizeof(WCHAR));
              info.command_line_utf8 = wstring_to_utf8(info.command_line);
            }
          }
        }

        CloseHandle(hProcess);
      }

      processes.push_back(info);
    } while (Process32NextW(hSnapshot, &pe32));

    CloseHandle(hSnapshot);
    return processes;
  }

  auto getPreviousSnapshot() const -> const std::vector<ProcessInfo>& {
    return previous_snapshot_;
  }

  auto updateSnapshot(const std::vector<ProcessInfo>& snapshot) -> void {
    previous_snapshot_ = current_snapshot_;
    current_snapshot_ = snapshot;
  }

  auto calculateCPUUsage(std::vector<ProcessInfo>& processes,
                         const CPUSnapshot& sys_prev,
                         const CPUSnapshot& sys_curr, size_t cpu_count)
      -> void {
    // Calculate system total time delta
    ULONGLONG sys_prev_time = fileTimeToULong(sys_prev.kernel_time) +
                              fileTimeToULong(sys_prev.user_time);
    ULONGLONG sys_curr_time = fileTimeToULong(sys_curr.kernel_time) +
                              fileTimeToULong(sys_curr.user_time);
    ULONGLONG sys_time_delta = sys_curr_time - sys_prev_time;
    if (sys_time_delta == 0) {
      sys_time_delta = 1;  // Avoid division by zero
    }

    // Build PID -> previous info map
    std::unordered_map<DWORD, ProcessInfo> prev_map;
    for (const auto& proc : previous_snapshot_) {
      prev_map[proc.pid] = proc;
    }

    // Calculate CPU usage for each process
    for (auto& proc : processes) {
      auto it = prev_map.find(proc.pid);
      if (it != prev_map.end()) {
        const ProcessInfo& prev = it->second;
        ULONGLONG proc_time_delta = proc.total_cpu_time - prev.total_cpu_time;
        proc.cpu_percent = (static_cast<float>(proc_time_delta) * 100.0f /
                            static_cast<float>(sys_time_delta)) *
                           static_cast<float>(cpu_count);
      }
    }
  }

 private:
  std::vector<ProcessInfo> current_snapshot_;
  std::vector<ProcessInfo> previous_snapshot_;
};

// ======================================================
// System Monitor
// ======================================================

class SystemMonitor {
 public:
  auto getSystemStats() -> SystemStats {
    SystemStats stats;

    // Get memory status
    MEMORYSTATUSEX memInfo;
    memInfo.dwLength = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&memInfo)) {
      stats.total_memory = memInfo.ullTotalPhys;
      stats.available_memory = memInfo.ullAvailPhys;
      stats.used_memory = stats.total_memory - stats.available_memory;
      stats.memory_usage = memInfo.dwMemoryLoad;
    }

    // Get system times
    FILETIME ftIdle, ftKernel, ftUser;
    if (GetSystemTimes(&ftIdle, &ftKernel, &ftUser)) {
      stats.total_idle_time = fileTimeToULong(ftIdle);
    }

    // Get system uptime
    ULONGLONG uptime = GetTickCount64();
    stats.total_uptime = uptime;

    return stats;
  }

  auto getCPUSnapshot() -> CPUSnapshot {
    CPUSnapshot snapshot;
    GetSystemTimes(&snapshot.idle_time, &snapshot.kernel_time,
                   &snapshot.user_time);
    return snapshot;
  }

  auto calculateSystemCPUUsage(const CPUSnapshot& prev, const CPUSnapshot& curr)
      -> float {
    ULONGLONG prev_idle = fileTimeToULong(prev.idle_time);
    ULONGLONG prev_kernel = fileTimeToULong(prev.kernel_time);
    ULONGLONG prev_user = fileTimeToULong(prev.user_time);

    ULONGLONG curr_idle = fileTimeToULong(curr.idle_time);
    ULONGLONG curr_kernel = fileTimeToULong(curr.kernel_time);
    ULONGLONG curr_user = fileTimeToULong(curr.user_time);

    ULONGLONG total_prev = prev_kernel + prev_user;
    ULONGLONG total_curr = curr_kernel + curr_user;

    ULONGLONG total_delta = total_curr - total_prev;
    ULONGLONG idle_delta = curr_idle - prev_idle;

    if (total_delta == 0) {
      return 0.0f;
    }

    return 100.0f * (1.0f - static_cast<float>(idle_delta) /
                                static_cast<float>(total_delta));
  }
};

// ======================================================
// Display Manager
// ======================================================

class DisplayManager {
 public:
  DisplayManager() {
    hConsole_ = GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(hConsole_, &originalConsoleInfo_);
  }

  ~DisplayManager() {
    SetConsoleTextAttribute(hConsole_, originalConsoleInfo_.wAttributes);
  }

  auto clearScreen() -> void {
    // Use ANSI escape sequence for better compatibility
    safePrint(
        "\033[2J\033[H\033[3J");  // Clear entire screen and move cursor to top
  }

  auto setCursorPos(short x, short y) -> void {
    COORD coord = {x, y};
    SetConsoleCursorPosition(hConsole_, coord);
  }

  auto setColor(WORD color) -> void {
    SetConsoleTextAttribute(hConsole_, color);
  }

  auto resetColor() -> void {
    SetConsoleTextAttribute(hConsole_, originalConsoleInfo_.wAttributes);
  }

  auto printHeader(const SystemStats& stats, const std::string& hostname,
                   int update_interval) -> void {
    setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    safePrint("top - ");
    resetColor();

    // Print time
    time_t now = time(nullptr);
    char time_buf[64];
    std::tm local_tm;
    localtime_s(&local_tm, &now);
    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", &local_tm);
    safePrint(time_buf);
    safePrint(" up ");

    // Print uptime
    int uptime_seconds = static_cast<int>(stats.total_uptime / 1000);
    int days = uptime_seconds / 86400;
    int hours = (uptime_seconds % 86400) / 3600;
    int minutes = (uptime_seconds % 3600) / 60;
    if (days > 0) {
      char buf[64];
      snprintf(buf, sizeof(buf), "%d days, %d:%02d", days, hours, minutes);
      safePrint(buf);
    } else {
      char buf[32];
      snprintf(buf, sizeof(buf), "%d:%02d", hours, minutes);
      safePrint(buf);
    }

    safePrint(",  ");

    // Print user count
    safePrint("1 user");  // Simplified
    safePrint(",  ");

    // Print load average (simulated)
    char load_buf[128];
    snprintf(load_buf, sizeof(load_buf), "%.2f, %.2f, %.2f",
             stats.cpu_usage / 100.0f, stats.cpu_usage / 110.0f,
             stats.cpu_usage / 120.0f);
    safePrint("load average: ");
    safePrint(load_buf);
    safePrint("\n");

    // Print task summary
    char task_buf[128];
    snprintf(task_buf, sizeof(task_buf),
             "%4d total,   %4d running, %4d sleeping,   0 stopped,   0 zombie",
             stats.total_processes, stats.running_processes,
             stats.total_processes - stats.running_processes);
    safePrint("Tasks: ");
    safePrint(task_buf);
    safePrint("\n");

    // Print CPU usage
    char cpu_buf[256];
    snprintf(cpu_buf, sizeof(cpu_buf),
             "%5.1f us, %5.1f sy, %5.1f ni, %5.1f id, %5.1f wa, %5.1f hi, "
             "%5.1f si, %5.1f st",
             stats.cpu_usage * 0.6f, stats.cpu_usage * 0.2f, 0.0f,
             100.0f - stats.cpu_usage, 0.0f, 0.0f, 0.0f, 0.0f);
    safePrint("%Cpu(s): ");
    safePrint(cpu_buf);
    safePrint("\n");

    // Print memory usage
    double mem_total_gb = stats.total_memory / (1024.0 * 1024.0 * 1024.0);
    double mem_used_gb = stats.used_memory / (1024.0 * 1024.0 * 1024.0);
    double mem_free_gb = stats.available_memory / (1024.0 * 1024.0 * 1024.0);

    char mem_buf[256];
    snprintf(mem_buf, sizeof(mem_buf),
             "%8.1f total, %8.1f free, %8.1f used, %8.1f cache",
             mem_total_gb * 1024.0, mem_free_gb * 1024.0, mem_used_gb * 1024.0,
             0.0);
    safePrint("MiB Mem : ");
    safePrint(mem_buf);
    safePrint("\n");

    // Print swap info (Windows doesn't have traditional swap)
    char swap_buf[256];
    snprintf(swap_buf, sizeof(swap_buf),
             "%8.1f total, %8.1f free, %8.1f used. %6.1f avail Mem", 0.0, 0.0,
             0.0, mem_free_gb * 1024.0);
    safePrint("MiB Swap: ");
    safePrint(swap_buf);
    safePrint("\n");

    safePrint("\n");

    safePrint("\n");
  }

  // Print process list
  auto printProcessList(const std::vector<ProcessInfo>& processes,
                        const SystemStats& stats, size_t max_display,
                        const TopConfig& cfg) -> void {
    // Get current cursor position for white background
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    GetConsoleScreenBufferInfo(hConsole_, &csbi);
    short startLine = csbi.dwCursorPosition.Y;

    // Print header text first
    setColor(FOREGROUND_GREEN | FOREGROUND_INTENSITY);
    if (cfg.show_threads) {
      safePrint(
          "  PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM   TIME+  "
          "     THREADS  COMMAND\n");
    } else {
      safePrint(
          "  PID USER      PR  NI    VIRT    RES    SHR S  %CPU  %MEM   TIME+  "
          "     COMMAND\n");
    }

    // Get cursor position after printing header
    GetConsoleScreenBufferInfo(hConsole_, &csbi);
    COORD endPos = csbi.dwCursorPosition;

    // Move cursor to start of header line
    COORD lineStart = {0, startLine};
    SetConsoleCursorPosition(hConsole_, lineStart);

    DWORD dummy{};

    // Set white background for the entire header line
    FillConsoleOutputAttribute(
        hConsole_, BACKGROUND_BLUE | BACKGROUND_GREEN | BACKGROUND_RED,
        csbi.dwSize.X, lineStart, std::addressof(dummy));

    // Move cursor back to after header
    SetConsoleCursorPosition(hConsole_, endPos);

    resetColor();

    // Filter processes
    std::vector<ProcessInfo> filtered_processes;
    for (const auto& proc : processes) {
      // User filter
      if (!cfg.user_filter.empty()) {
        if (proc.username != cfg.user_filter) {
          continue;
        }
      }

      // Idle filter
      if (cfg.ignore_idle && proc.cpu_percent < 0.1f) {
        continue;
      }

      filtered_processes.push_back(proc);
    }

    size_t display_count = std::min(filtered_processes.size(), max_display);

    for (size_t i = 0; i < display_count; ++i) {
      const ProcessInfo& proc = filtered_processes[i];

      // Highlight high CPU usage
      if (proc.cpu_percent > 50.0f) {
        setColor(FOREGROUND_RED | FOREGROUND_INTENSITY);
      } else if (proc.cpu_percent > 20.0f) {
        setColor(FOREGROUND_INTENSITY);
      } else {
        resetColor();
      }

      char buf[256];

      // Print PID (5 chars, right-aligned)
      snprintf(buf, sizeof(buf), "%5d ", proc.pid);
      safePrint(buf);

      // Print user (9 chars, left-aligned)
      std::string user = proc.username;
      if (user.length() > 9) {
        user = user.substr(0, 9);
      }
      snprintf(buf, sizeof(buf), "%-9s ", user.c_str());
      safePrint(buf);

      // Print priority (2 chars, right-aligned)
      int priority = proc.priority;
      if (priority == 0x00000020) {
        safePrint("RT ");  // Real-time
      } else if (priority == 0x00004000) {
        safePrint("Hi ");  // High
      } else if (priority == 0x00000080) {
        safePrint("NO ");  // Normal
      } else if (priority == 0x00000040) {
        safePrint("ID ");  // Idle
      } else {
        int prio_val = (priority >> 13) & 0x07;
        snprintf(buf, sizeof(buf), "%2d ", prio_val);
        safePrint(buf);
      }

      // Print nice value (3 chars, right-aligned)
      snprintf(buf, sizeof(buf), "%3d ", 0);
      safePrint(buf);

      // Print memory info (VIRT: 7 chars, RES: 6 chars, SHR: 6 chars)
      double virt_mb = proc.virtual_memory_bytes / (1024.0 * 1024.0);
      double res_mb = proc.memory_bytes / (1024.0 * 1024.0);
      double shr_mb = 0.0;  // Windows doesn't have shared memory concept
      snprintf(buf, sizeof(buf), "%7.1f %6.1f %6.1f ", virt_mb, res_mb, shr_mb);
      safePrint(buf);

      // Print status (1 char)
      safePrint("S ");

      // Print CPU and memory percentage (5 chars each, right-aligned)
      float mem_percent = (static_cast<float>(proc.memory_bytes) /
                           static_cast<float>(stats.total_memory)) *
                          100.0f;
      snprintf(buf, sizeof(buf), "%5.1f %5.1f ", proc.cpu_percent, mem_percent);
      safePrint(buf);

      // Print time (11 chars, right-aligned)
      ULONGLONG total_time = proc.total_cpu_time / 10000;
      int hours = total_time / 3600000;
      int minutes = (total_time % 3600000) / 60000;
      int seconds = (total_time % 60000) / 1000;
      int centiseconds = total_time % 100;
      snprintf(buf, sizeof(buf), "%3d:%02d:%02d.%02d  ", hours, minutes,
               seconds, centiseconds);
      safePrint(buf);

      // Print thread count if enabled
      if (cfg.show_threads) {
        snprintf(buf, sizeof(buf), "%7d  ", proc.thread_count);
        safePrint(buf);
      }

      // Print command name or full command line
      std::string cmd_to_display;
      if (cfg.show_full_command && !proc.command_line_utf8.empty()) {
        cmd_to_display = proc.command_line_utf8;
      } else {
        cmd_to_display = proc.name_utf8;
      }

      // Truncate if too long
      CONSOLE_SCREEN_BUFFER_INFO currCsbi;
      GetConsoleScreenBufferInfo(hConsole_, &currCsbi);
      int currentLineLength = currCsbi.dwCursorPosition.X;
      int remainingWidth = csbi.dwSize.X - currentLineLength - 1;

      if (remainingWidth > 0 &&
          cmd_to_display.length() > static_cast<size_t>(remainingWidth)) {
        cmd_to_display = cmd_to_display.substr(0, remainingWidth);
      }

      safePrint(cmd_to_display);
      safePrint("\n");
    }

    resetColor();
  }

 private:
  HANDLE hConsole_;
  CONSOLE_SCREEN_BUFFER_INFO originalConsoleInfo_;
};

// ======================================================
// Sort Functions
// ======================================================

auto sortByField(std::vector<ProcessInfo>& processes, SortField field) -> void {
  switch (field) {
    case SortField::CPU:
      std::sort(processes.begin(), processes.end(),
                [](const ProcessInfo& a, const ProcessInfo& b) {
                  return a.cpu_percent > b.cpu_percent;
                });
      break;
    case SortField::MEM:
      std::sort(processes.begin(), processes.end(),
                [](const ProcessInfo& a, const ProcessInfo& b) {
                  return a.memory_bytes > b.memory_bytes;
                });
      break;
    case SortField::TIME:
      std::sort(processes.begin(), processes.end(),
                [](const ProcessInfo& a, const ProcessInfo& b) {
                  return a.total_cpu_time > b.total_cpu_time;
                });
      break;
    case SortField::PID:
      std::sort(processes.begin(), processes.end(),
                [](const ProcessInfo& a, const ProcessInfo& b) {
                  return a.pid < b.pid;
                });
      break;
    case SortField::NAME:
      std::sort(processes.begin(), processes.end(),
                [](const ProcessInfo& a, const ProcessInfo& b) {
                  return a.name_utf8 < b.name_utf8;
                });
      break;
  }
}

// ======================================================
// Pipeline Components
// ======================================================
namespace top_pipeline {
namespace cp = core::pipeline;

// 1. Parse configuration
auto parse_config(const CommandContext<TOP_OPTIONS.size()>& ctx)
    -> cp::Result<TopConfig> {
  TopConfig cfg;

  cfg.batch_mode =
      ctx.get<bool>("--batch", false) || ctx.get<bool>("-b", false);

  std::string delay_str = ctx.get<std::string>("--delay", "");
  if (delay_str.empty()) delay_str = ctx.get<std::string>("-d", "");
  if (!delay_str.empty()) {
    try {
      cfg.delay = std::stoi(delay_str);
    } catch (...) {
    }
    if (cfg.delay < 1) cfg.delay = 1;
  }

  std::string iter_str = ctx.get<std::string>("--iterations", "");
  if (iter_str.empty()) iter_str = ctx.get<std::string>("-n", "");
  if (!iter_str.empty()) {
    try {
      cfg.max_iterations = std::stoi(iter_str);
    } catch (...) {
    }
  }

  std::string sort_str = ctx.get<std::string>("--field-sort", "");
  if (sort_str.empty()) sort_str = ctx.get<std::string>("-o", "");
  if (!sort_str.empty()) {
    if (sort_str == "CPU")
      cfg.sort_field = SortField::CPU;
    else if (sort_str == "MEM")
      cfg.sort_field = SortField::MEM;
    else if (sort_str == "TIME")
      cfg.sort_field = SortField::TIME;
    else if (sort_str == "PID")
      cfg.sort_field = SortField::PID;
    else if (sort_str == "NAME")
      cfg.sort_field = SortField::NAME;
  }

  return cfg;
}

// 2. Check for help or version
auto check_help_version(const CommandContext<TOP_OPTIONS.size()>& ctx)
    -> cp::Result<bool> {
  if (ctx.get<bool>("--help", false) || ctx.get<bool>("-h", false)) {
    safePrint("Usage: top [options]\n");
    safePrint("  -b, --batch        Batch mode\n");
    safePrint("  -d, --delay DELAY  Update interval (default: 3s)\n");
    safePrint("  -n, --iterations N Exit after N iterations\n");
    safePrint("  -o, --field-sort F Sort by CPU|MEM|TIME|PID|NAME\n");
    safePrint("  -h, --help         Show help\n");
    safePrint("  -v, --version      Show version\n");
    return true;  // Should exit
  }

  if (ctx.get<bool>("--version", false) || ctx.get<bool>("-v", false)) {
    safePrint("top (WinuxCmd) 0.1.0\n");
    return true;  // Should exit
  }

  return false;  // Continue
}

// 3. Run top main loop
auto run_top(TopConfig& cfg) -> cp::Result<bool> {
  ProcessEnumerator enumerator;
  SystemMonitor monitor;
  DisplayManager display;

  CPUSnapshot prev_cpu = monitor.getCPUSnapshot();
  auto processes = enumerator.enumerate(cfg.show_threads);
  enumerator.updateSnapshot(processes);
  Sleep(500);
  CPUSnapshot curr_cpu = monitor.getCPUSnapshot();

  SystemStats stats = monitor.getSystemStats();
  enumerator.calculateCPUUsage(processes, prev_cpu, curr_cpu,
                               static_cast<size_t>(stats.cpu_count));
  sortByField(processes, cfg.sort_field);
  stats.cpu_usage = monitor.calculateSystemCPUUsage(prev_cpu, curr_cpu);
  stats.total_processes = static_cast<DWORD>(processes.size());
  stats.running_processes = 1;

  char hostname[256] = "localhost";
  gethostname(hostname, sizeof(hostname));

  int iteration = 0;
  bool running = true;

  while (running &&
         (cfg.max_iterations < 0 || iteration < cfg.max_iterations)) {
    iteration++;

    if (!cfg.batch_mode) {
      display.clearScreen();
      display.setCursorPos(0, 0);
    }

    display.printHeader(stats, hostname, cfg.delay);
    display.printProcessList(processes, stats, 50, cfg);

    if (!cfg.batch_mode) {
      for (int i = 0; i < cfg.delay * 10 && running; ++i) {
        if (_kbhit()) {
          int ch = _getch();
          ch = toupper(ch);

          switch (ch) {
            case 'Q':
            case 27:  // ESC
              running = false;
              break;

            case 'P':
              cfg.sort_field = SortField::CPU;
              sortByField(processes, cfg.sort_field);
              break;

            case 'M':
              cfg.sort_field = SortField::MEM;
              sortByField(processes, cfg.sort_field);
              break;

            case 'T':
              cfg.sort_field = SortField::TIME;
              sortByField(processes, cfg.sort_field);
              break;

            case 'N':
              cfg.sort_field = SortField::PID;
              sortByField(processes, cfg.sort_field);
              break;

            case 'C':
              cfg.show_full_command = !cfg.show_full_command;
              break;

            case 'H':
              cfg.show_threads = !cfg.show_threads;
              break;

            case 'I':
              cfg.ignore_idle = !cfg.ignore_idle;
              break;

            case 'S':
            case 'D': {
              safePrint("\nEnter new delay (seconds): ");
              char input[32];
              if (fgets(input, sizeof(input), stdin)) {
                int new_delay = atoi(input);
                if (new_delay > 0) {
                  cfg.delay = new_delay;
                }
              }
              break;
            }

            case 'K': {
              safePrint("\nEnter PID to kill: ");
              char input[32];
              if (fgets(input, sizeof(input), stdin)) {
                DWORD pid = atoi(input);
                HANDLE hProcess = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                if (hProcess) {
                  if (TerminateProcess(hProcess, 0)) {
                    safePrint("Process terminated successfully.\n");
                  } else {
                    safePrint("Failed to terminate process.\n");
                  }
                  CloseHandle(hProcess);
                } else {
                  safePrint("Failed to open process.\n");
                }
              }
              Sleep(2000);  // Give user time to read the message
              break;
            }

            case 'R': {
              safePrint("\nEnter PID to renice: ");
              char input[32];
              if (fgets(input, sizeof(input), stdin)) {
                DWORD pid = atoi(input);
                safePrint("Enter priority (0-31, lower is higher): ");
                if (fgets(input, sizeof(input), stdin)) {
                  int priority = atoi(input);
                  HANDLE hProcess =
                      OpenProcess(PROCESS_SET_INFORMATION, FALSE, pid);
                  if (hProcess) {
                    if (SetPriorityClass(hProcess, priority)) {
                      safePrint("Priority changed successfully.\n");
                    } else {
                      safePrint("Failed to change priority.\n");
                    }
                    CloseHandle(hProcess);
                  } else {
                    safePrint("Failed to open process.\n");
                  }
                }
              }
              Sleep(2000);
              break;
            }

            case 'U': {
              safePrint("\nEnter username to filter (empty to clear): ");
              char input[256];
              if (fgets(input, sizeof(input), stdin)) {
                // Remove newline
                input[strcspn(input, "\n")] = '\0';
                cfg.user_filter = input;
              }
              break;
            }
          }

          // If an interactive command was entered, refresh the display
          // immediately
          if (ch != 'Q' && ch != 27) {
            display.clearScreen();
            display.setCursorPos(0, 0);
            display.printHeader(stats, hostname, cfg.delay);
            display.printProcessList(processes, stats, 50, cfg);
          }
        }
        Sleep(100);
      }
    } else {
      Sleep(cfg.delay * 1000);
    }

    if (running) {
      prev_cpu = curr_cpu;
      curr_cpu = monitor.getCPUSnapshot();
      processes = enumerator.enumerate(cfg.show_threads);
      enumerator.calculateCPUUsage(processes, prev_cpu, curr_cpu,
                                   static_cast<size_t>(stats.cpu_count));
      sortByField(processes, cfg.sort_field);
      stats = monitor.getSystemStats();
      stats.cpu_usage = monitor.calculateSystemCPUUsage(prev_cpu, curr_cpu);
      stats.total_processes = static_cast<DWORD>(processes.size());
      enumerator.updateSnapshot(processes);
    }
  }

  if (!cfg.batch_mode) safePrint("\n");
  return true;
}

// 4. Main pipeline
template <size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<bool> {
  return check_help_version(ctx).and_then(
      [&](bool should_exit) -> cp::Result<bool> {
        if (should_exit) return true;
        return parse_config(ctx).and_then(
            [](TopConfig cfg) { return run_top(cfg); });
      });
}

}  // namespace top_pipeline

// ======================================================
// Main Pipeline
// ======================================================

REGISTER_COMMAND(
    top,
    /* cmd_name */ "top",
    /* cmd_synopsis */ "display Linux processes",
    /* cmd_desc */
    "The top program provides a dynamic real-time view of a running system.\n"
    "It can display system summary information as well as a list of processes\n"
    "or threads currently being managed by the Linux kernel.\n"
    "\n"
    "The types of system summary information shown and the types, order and\n"
    "size of information displayed for processes are all user configurable\n"
    "and that configuration can be made persistent across restarts.\n"
    "\n"
    "The program provides a limited interactive interface for process\n"
    "manipulation as well as a much more extensive interface for personal\n"
    "configuration - encompassing every aspect of its operation. And while\n"
    "top is referred to throughout this document, you are free to name the\n"
    "program anything you wish. That new name, possibly an alias, will then\n"
    "be reflected on top's display and used when reading and writing a\n"
    "configuration file.\n",
    /* examples */
    "  top                      Show all processes\n"
    "  top -d 5                 Refresh every 5 seconds\n"
    "  top -n 10                Exit after 10 iterations\n"
    "  top -p 1234,5678         Monitor specific PIDs",
    /* see_also */ "ps(1), kill(1), nice(1)",
    /* author */ "caomengxuan666",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */
    TOP_OPTIONS) {
  using namespace top_pipeline;
  using namespace core::pipeline;

  auto result = process_command(ctx);
  if (!result) {
    report_error(result, L"top");
    return 1;
  }

  return *result ? 0 : 1;
}
