// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - @contributor1 arookieofc <2128194521@qq.com>
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for ps.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright ©  2026 WinuxCmd
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "advapi32.lib")

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PS_OPTIONS = std::array{
    // [EXT]
    OPTION("-e", "", "select all processes (same as -A)"),
    // [EXT]
    OPTION("-A", "", "select all processes"),
    // [EXT]
    OPTION("-a", "",
           "select all processes except session leaders and not associated "
           "with a terminal"),
    // [EXT]
    OPTION("-f", "", "do full-format listing"),
    // [EXT]
    OPTION("-l", "", "long format"),
    // [EXT]
    OPTION("-u", "", "display user-oriented format"),
    // [EXT]
    OPTION("-p", "--pid", "select processes by process ID", STRING_TYPE),
    // [EXT]
    OPTION("-o", "--format", "select output fields", STRING_TYPE),
    // [EXT]
    OPTION("", "--user", "filter processes by user name", STRING_TYPE),
    // [EXT]
    OPTION("-x", "", "lift the BSD-style \"must have a tty\" restriction"),
    // [EXT]
    OPTION("-w", "", "wide output (do not truncate command lines)"),
    // [EXT]
    OPTION("", "--no-headers", "print no header line"),
    // [EXT]
    OPTION("", "--sort", "sort by column (e.g., +pid, -rss)", STRING_TYPE)};

namespace ps_pipeline {
namespace cp = core::pipeline;

struct ProcessInfo {
  DWORD pid;
  DWORD ppid;
  std::wstring name;
  std::wstring full_path;
  std::wstring user;
  std::wstring command_line;
  FILETIME create_time;
  FILETIME kernel_time;
  FILETIME user_time;
  SIZE_T working_set_size;
  SIZE_T private_bytes;
  int priority;
  int thread_count;
  DWORD session_id = 0;
  bool has_console = false;
};

struct Config {
  bool all_processes = false;
  bool full_format = false;
  bool long_format = false;
  bool user_format = false;
  bool wide_output = false;
  bool no_headers = false;
  bool select_associated = false;
  bool lift_tty_restriction = false;
  std::vector<DWORD> pid_filter;
  std::vector<std::string> format_fields;
  std::string user_filter;
  std::string sort_key;
};

// RAII wrapper for HANDLE
struct HandleCloser {
  typedef HANDLE pointer;
  void operator()(HANDLE h) {
    if (h && h != INVALID_HANDLE_VALUE) CloseHandle(h);
  }
};
using unique_handle = std::unique_ptr<HANDLE, HandleCloser>;

// Get user name from process handle
auto get_process_user(HANDLE h_process) -> std::wstring {
  HANDLE h_token = nullptr;

  if (!OpenProcessToken(h_process, TOKEN_QUERY, &h_token)) {
    return L"UNKNOWN";
  }

  // Use RAII for token handle
  unique_handle h_token_holder(h_token);

  DWORD size = 0;
  GetTokenInformation(h_token, TokenUser, nullptr, 0, &size);
  if (size == 0) {
    return L"UNKNOWN";
  }

  std::vector<BYTE> buffer(size);
  if (!GetTokenInformation(h_token, TokenUser, buffer.data(), size, &size)) {
    return L"UNKNOWN";
  }

  auto* token_user = reinterpret_cast<TOKEN_USER*>(buffer.data());
  wchar_t name[256] = {0};
  wchar_t domain[256] = {0};
  DWORD name_len = 256;
  DWORD domain_len = 256;
  SID_NAME_USE sid_type;

  if (LookupAccountSidW(nullptr, token_user->User.Sid, name, &name_len, domain,
                        &domain_len, &sid_type)) {
    return std::wstring(name);
  }

  return L"UNKNOWN";
}

// Get process command line with safer approach
auto get_process_command_line(DWORD pid) -> std::wstring {
  HANDLE h_proc = OpenProcess(
      PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, pid);
  unique_handle h_process(h_proc);

  if (!h_process.get() || h_process.get() == INVALID_HANDLE_VALUE) return L"";

  // Try to read command line from PEB using official structures
  PROCESS_BASIC_INFORMATION pbi = {};
  typedef NTSTATUS(WINAPI * NtQueryInformationProcessFunc)(HANDLE, DWORD, PVOID,
                                                           ULONG, PULONG);

  HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
  if (ntdll) {
    auto NtQueryInformationProcess =
        (NtQueryInformationProcessFunc)GetProcAddress(
            ntdll, "NtQueryInformationProcess");
    if (NtQueryInformationProcess) {
      ULONG len = 0;
      if (NtQueryInformationProcess(h_process.get(), 0, &pbi, sizeof(pbi),
                                    &len) == 0) {
        // Use safer approach with bounds checking
        SIZE_T read = 0;
        PVOID pparams_addr = nullptr;

        // Validate PEB address before reading
        if (pbi.PebBaseAddress) {
          // Read ProcessParameters pointer from PEB (offset 0x20)
          if (ReadProcessMemory(h_process.get(),
                                (PBYTE)pbi.PebBaseAddress + 0x20, &pparams_addr,
                                sizeof(PVOID), &read) &&
              read == sizeof(PVOID) && pparams_addr) {
            UNICODE_STRING cmd_line = {};
            // Read CommandLine from ProcessParameters (offset 0x70)
            if (ReadProcessMemory(h_process.get(), (PBYTE)pparams_addr + 0x70,
                                  &cmd_line, sizeof(cmd_line), &read) &&
                read == sizeof(cmd_line) && cmd_line.Length > 0 &&
                cmd_line.Buffer) {
              // Validate buffer bounds before reading
              if (cmd_line.Length <= 32768) {  // Reasonable limit
                size_t num_chars = cmd_line.Length / sizeof(wchar_t);
                std::vector<wchar_t> wbuf(num_chars + 1);
                if (ReadProcessMemory(h_process.get(), cmd_line.Buffer,
                                      wbuf.data(), cmd_line.Length, &read)) {
                  wbuf[num_chars] = L'\0';
                  return std::wstring(wbuf.data(), num_chars);
                }
              }
            }
          }
        }
      }
    }
  }

  // Fallback: return empty string if we can't read command line
  return L"";
}

// Get process full path
auto get_process_path(HANDLE h_process) -> std::wstring {
  wchar_t path[MAX_PATH * 2] = {0};
  DWORD size = MAX_PATH * 2;

  if (QueryFullProcessImageNameW(h_process, 0, path, &size)) {
    return std::wstring(path, size);
  }
  return L"";
}

// Get memory info
auto get_process_memory(HANDLE h_process) -> std::pair<SIZE_T, SIZE_T> {
  PROCESS_MEMORY_COUNTERS_EX pmc = {};
  if (GetProcessMemoryInfo(h_process, (PROCESS_MEMORY_COUNTERS*)&pmc,
                           sizeof(pmc))) {
    return {pmc.WorkingSetSize, pmc.PrivateUsage};
  }
  return {0, 0};
}

// Enumerate all processes
auto enumerate_processes() -> cp::Result<std::vector<ProcessInfo>> {
  std::vector<ProcessInfo> processes;
  processes.reserve(4096);  // Reserve for reasonable number of processes

  HANDLE h_snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
  if (h_snap == INVALID_HANDLE_VALUE) {
    return std::unexpected("Failed to create process snapshot");
  }

  PROCESSENTRY32W pe = {};
  pe.dwSize = sizeof(pe);

  if (!Process32FirstW(h_snap, &pe)) {
    CloseHandle(h_snap);
    return std::unexpected("Failed to get first process");
  }

  do {
    ProcessInfo info = {};
    info.pid = pe.th32ProcessID;
    info.ppid = pe.th32ParentProcessID;
    info.name = pe.szExeFile;
    info.priority = pe.pcPriClassBase;
    info.thread_count = pe.cntThreads;

    // Get session id and console status
    DWORD session_id = 0;
    if (ProcessIdToSessionId(info.pid, &session_id)) {
      info.session_id = session_id;
      info.has_console = (session_id != 0);
    }

    // Try to open process for more info
    HANDLE h_proc = OpenProcess(
        PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_VM_READ, FALSE, info.pid);
    unique_handle h_process(h_proc);

    if (h_process.get() && h_process.get() != INVALID_HANDLE_VALUE) {
      // Get user
      info.user = get_process_user(h_process.get());

      // Get full path
      info.full_path = get_process_path(h_process.get());

      // Get times
      FILETIME exit_time;
      GetProcessTimes(h_process.get(), &info.create_time, &exit_time,
                      &info.kernel_time, &info.user_time);

      // Get memory
      auto [ws, priv] = get_process_memory(h_process.get());
      info.working_set_size = ws;
      info.private_bytes = priv;

      // Handle automatically closed by unique_handle destructor
    }

    // Get command line (may fail for protected processes)
    if (info.pid != 0 && info.pid != 4) {  // Skip System Idle and System
      info.command_line = get_process_command_line(info.pid);
      if (info.command_line.empty()) {
        info.command_line = info.name;
      }
    } else {
      info.command_line = info.name;
    }

    processes.push_back(std::move(info));

  } while (Process32NextW(h_snap, &pe));

  CloseHandle(h_snap);
  return processes;
}

// Format time
auto format_time(const FILETIME& ft) -> std::wstring {
  ULARGE_INTEGER uli;
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;

  // Convert to seconds
  ULONGLONG total_time = uli.QuadPart / 10000000ULL;

  int hours = static_cast<int>(total_time / 3600);
  int minutes = static_cast<int>((total_time % 3600) / 60);
  int seconds = static_cast<int>(total_time % 60);

  wchar_t buffer[32];
  swprintf_s(buffer, L"%02d:%02d:%02d", hours, minutes, seconds);
  return buffer;
}

// Format memory size
auto format_memory(SIZE_T bytes) -> std::wstring {
  double kb = bytes / 1024.0;
  if (kb < 1024) {
    wchar_t buffer[32];
    swprintf_s(buffer, L"%.0fK", kb);
    return buffer;
  }

  double mb = kb / 1024.0;
  wchar_t buffer[32];
  swprintf_s(buffer, L"%.1fM", mb);
  return buffer;
}

auto trim_ascii(std::string_view text) -> std::string_view {
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.front()))) {
    text.remove_prefix(1);
  }
  while (!text.empty() &&
         std::isspace(static_cast<unsigned char>(text.back()))) {
    text.remove_suffix(1);
  }
  return text;
}

auto split_ps_list(std::string_view text) -> std::vector<std::string_view> {
  std::vector<std::string_view> values;
  size_t start = 0;
  for (size_t i = 0; i <= text.size(); ++i) {
    const bool at_end = i == text.size();
    const bool is_sep =
        !at_end &&
        (text[i] == ',' || std::isspace(static_cast<unsigned char>(text[i])));
    if (!at_end && !is_sep) continue;

    auto item = trim_ascii(text.substr(start, i - start));
    if (!item.empty()) values.push_back(item);
    start = i + 1;
  }
  return values;
}

auto parse_pid_value(std::string_view text) -> std::optional<DWORD> {
  text = trim_ascii(text);
  if (text.empty()) return std::nullopt;

  uint64_t value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size() ||
      value > std::numeric_limits<DWORD>::max()) {
    return std::nullopt;
  }
  return static_cast<DWORD>(value);
}

auto append_pid_filter(std::string_view raw, std::vector<DWORD>& pids)
    -> std::optional<std::string> {
  for (auto token : split_ps_list(raw)) {
    auto pid = parse_pid_value(token);
    if (!pid) return "invalid process ID: " + std::string(token);
    pids.push_back(*pid);
  }
  return std::nullopt;
}

auto normalize_format_field(std::string_view field) -> std::string {
  auto trimmed = trim_ascii(field);
  std::string normalized(trimmed);
  std::transform(
      normalized.begin(), normalized.end(), normalized.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return normalized;
}

auto is_supported_format_field(std::string_view field) -> bool {
  return field == "pid" || field == "ppid" || field == "comm" ||
         field == "args" || field == "user" || field == "etime" ||
         field == "rss" || field == "pmem" || field == "pcpu";
}

auto append_format_fields(std::string_view raw,
                          std::vector<std::string>& fields)
    -> std::optional<std::string> {
  for (auto token : split_ps_list(raw)) {
    auto field = normalize_format_field(token);
    if (!is_supported_format_field(field)) {
      return "unsupported format field: " + std::string(token);
    }
    fields.push_back(std::move(field));
  }
  return std::nullopt;
}

// Build config from context
auto build_config(const CommandContext<PS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.all_processes = ctx.get<bool>("-e", false) || ctx.get<bool>("-A", false);
  cfg.select_associated = ctx.get<bool>("-a", false);
  cfg.lift_tty_restriction = ctx.get<bool>("-x", false);

  cfg.full_format = ctx.get<bool>("-f", false);
  cfg.long_format = ctx.get<bool>("-l", false);
  cfg.user_format = ctx.get<bool>("-u", false);
  cfg.user_filter = ctx.get<std::string>("--user", "");
  cfg.wide_output = ctx.get<bool>("-w", false);
  cfg.no_headers = ctx.get<bool>("--no-headers", false);
  cfg.sort_key = ctx.get<std::string>("--sort", "");

  for (const auto& occurrence : ctx.string_occurrences({"-p", "--pid"})) {
    if (auto error = append_pid_filter(occurrence.value, cfg.pid_filter)) {
      return std::unexpected(*error);
    }
  }

  for (const auto& occurrence : ctx.string_occurrences({"-o", "--format"})) {
    if (auto error =
            append_format_fields(occurrence.value, cfg.format_fields)) {
      return std::unexpected(*error);
    }
  }

  return cfg;
}

// Sort processes
auto sort_processes(std::vector<ProcessInfo>& processes,
                    const std::string& sort_key) -> void {
  if (sort_key.empty() || sort_key == "pid" || sort_key == "+pid") {
    std::sort(processes.begin(), processes.end(),
              [](const auto& a, const auto& b) { return a.pid < b.pid; });
  } else if (sort_key == "-pid") {
    std::sort(processes.begin(), processes.end(),
              [](const auto& a, const auto& b) { return a.pid > b.pid; });
  } else if (sort_key == "mem" || sort_key == "+mem" || sort_key == "rss" ||
             sort_key == "+rss") {
    std::sort(processes.begin(), processes.end(),
              [](const auto& a, const auto& b) {
                return a.working_set_size < b.working_set_size;
              });
  } else if (sort_key == "-mem" || sort_key == "-rss") {
    std::sort(processes.begin(), processes.end(),
              [](const auto& a, const auto& b) {
                return a.working_set_size > b.working_set_size;
              });
  } else if (sort_key == "name" || sort_key == "+name") {
    std::sort(processes.begin(), processes.end(),
              [](const auto& a, const auto& b) { return a.name < b.name; });
  } else if (sort_key == "-name") {
    std::sort(processes.begin(), processes.end(),
              [](const auto& a, const auto& b) { return a.name > b.name; });
  }
}

// Print processes in simple format
auto print_simple(const std::vector<ProcessInfo>& processes, bool no_headers,
                  bool wide_output = true) -> void {
  if (!no_headers) {
    safePrintLn(L"  PID TTY          TIME CMD");
  }

  for (const auto& proc : processes) {
    // Simple format: PID, TTY, TIME, CMD
    std::wstring line;

    // PID (5 chars, right aligned)
    wchar_t pid_buf[16];
    swprintf_s(pid_buf, L"%5lu", proc.pid);
    line += pid_buf;
    line += L" ";

    // TTY (always ? on Windows)
    line += L"?        ";

    // TIME (CPU time)
    ULARGE_INTEGER kernel, user;
    kernel.LowPart = proc.kernel_time.dwLowDateTime;
    kernel.HighPart = proc.kernel_time.dwHighDateTime;
    user.LowPart = proc.user_time.dwLowDateTime;
    user.HighPart = proc.user_time.dwHighDateTime;

    ULONGLONG total_100ns = kernel.QuadPart + user.QuadPart;
    ULONGLONG total_sec = total_100ns / 10000000ULL;

    wchar_t time_buf[16];
    swprintf_s(time_buf, L"%02llu:%02llu:%02llu", total_sec / 3600,
               (total_sec % 3600) / 60, total_sec % 60);
    line += time_buf;
    line += L" ";

    // CMD (truncate if not wide output)
    if (!wide_output && proc.name.size() > 80) {
      line += proc.name.substr(0, 77) + L"...";
    } else {
      line += proc.name;
    }

    safePrintLn(line);
  }
}

// Print processes in full format
auto print_full(const std::vector<ProcessInfo>& processes, bool no_headers)
    -> void {
  if (!no_headers) {
    safePrintLn(L"UID        PID  PPID  C STIME TTY          TIME CMD");
  }

  for (const auto& proc : processes) {
    std::wstring line;

    // UID (truncate to 10 chars)
    std::wstring uid = proc.user;
    if (uid.length() > 10) uid = uid.substr(0, 10);
    line += uid;
    line.append(11 - uid.length(), L' ');

    // PID
    wchar_t buf[32];
    swprintf_s(buf, L"%5lu ", proc.pid);
    line += buf;

    // PPID
    swprintf_s(buf, L"%5lu ", proc.ppid);
    line += buf;

    // C (CPU utilization, placeholder)
    line += L" 0 ";

    // STIME (start time - simplified)
    SYSTEMTIME st;
    FileTimeToSystemTime(&proc.create_time, &st);
    swprintf_s(buf, L"%02d:%02d ", st.wHour, st.wMinute);
    line += buf;

    // TTY
    line += L"?        ";

    // TIME
    ULARGE_INTEGER kernel, user;
    kernel.LowPart = proc.kernel_time.dwLowDateTime;
    kernel.HighPart = proc.kernel_time.dwHighDateTime;
    user.LowPart = proc.user_time.dwLowDateTime;
    user.HighPart = proc.user_time.dwHighDateTime;

    ULONGLONG total_100ns = kernel.QuadPart + user.QuadPart;
    ULONGLONG total_sec = total_100ns / 10000000ULL;

    swprintf_s(buf, L"%02llu:%02llu:%02llu ", total_sec / 3600,
               (total_sec % 3600) / 60, total_sec % 60);
    line += buf;

    // CMD
    line += proc.command_line.empty() ? proc.name : proc.command_line;

    safePrintLn(line);
  }
}

// Print processes in user format
auto print_user(const std::vector<ProcessInfo>& processes, bool no_headers)
    -> void {
  if (!no_headers) {
    safePrintLn(
        L"USER       PID %CPU %MEM    VSZ   RSS TTY      STAT START   TIME "
        L"COMMAND");
  }

  for (const auto& proc : processes) {
    std::wstring line;

    // USER (truncate to 10 chars)
    std::wstring user = proc.user;
    if (user.length() > 10) user = user.substr(0, 10);
    line += user;
    line.append(11 - user.length(), L' ');

    // PID
    wchar_t buf[64];
    swprintf_s(buf, L"%5lu ", proc.pid);
    line += buf;

    // %CPU (placeholder)
    line += L" 0.0 ";

    // %MEM (placeholder)
    line += L" 0.0 ";

    // VSZ (virtual size in KB)
    swprintf_s(buf, L"%6llu ", proc.private_bytes / 1024);
    line += buf;

    // RSS (resident set size in KB)
    swprintf_s(buf, L"%5llu ", proc.working_set_size / 1024);
    line += buf;

    // TTY
    line += L"?        ";

    // STAT (status - simplified)
    line += L"R    ";

    // START
    SYSTEMTIME st;
    FileTimeToSystemTime(&proc.create_time, &st);
    swprintf_s(buf, L"%02d:%02d ", st.wHour, st.wMinute);
    line += buf;

    // TIME
    ULARGE_INTEGER kernel, user_t;
    kernel.LowPart = proc.kernel_time.dwLowDateTime;
    kernel.HighPart = proc.kernel_time.dwHighDateTime;
    user_t.LowPart = proc.user_time.dwLowDateTime;
    user_t.HighPart = proc.user_time.dwHighDateTime;

    ULONGLONG total_100ns = kernel.QuadPart + user_t.QuadPart;
    ULONGLONG total_sec = total_100ns / 10000000ULL;

    swprintf_s(buf, L"%5llu:%02llu ", total_sec / 60, total_sec % 60);
    line += buf;

    // COMMAND
    line += proc.command_line.empty() ? proc.name : proc.command_line;

    safePrintLn(line);
  }
}

auto filetime_to_uint64(const FILETIME& ft) -> ULONGLONG {
  ULARGE_INTEGER value;
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return value.QuadPart;
}

auto process_cpu_seconds(const ProcessInfo& proc) -> double {
  const ULONGLONG total_100ns =
      filetime_to_uint64(proc.kernel_time) + filetime_to_uint64(proc.user_time);
  return static_cast<double>(total_100ns) / 10000000.0;
}

auto process_elapsed_seconds(const ProcessInfo& proc) -> ULONGLONG {
  const ULONGLONG started = filetime_to_uint64(proc.create_time);
  if (started == 0) return 0;

  FILETIME now_ft;
  GetSystemTimeAsFileTime(&now_ft);
  const ULONGLONG now = filetime_to_uint64(now_ft);
  if (now <= started) return 0;
  return (now - started) / 10000000ULL;
}

auto format_etime(ULONGLONG seconds) -> std::wstring {
  const ULONGLONG days = seconds / 86400ULL;
  seconds %= 86400ULL;
  const ULONGLONG hours = seconds / 3600ULL;
  seconds %= 3600ULL;
  const ULONGLONG minutes = seconds / 60ULL;
  seconds %= 60ULL;

  wchar_t buffer[32];
  if (days > 0) {
    swprintf_s(buffer, L"%llu-%02llu:%02llu:%02llu", days, hours, minutes,
               seconds);
  } else if (hours > 0) {
    swprintf_s(buffer, L"%llu:%02llu:%02llu", hours, minutes, seconds);
  } else {
    swprintf_s(buffer, L"%02llu:%02llu", minutes, seconds);
  }
  return buffer;
}

auto total_physical_memory() -> unsigned long long {
  MEMORYSTATUSEX mem_status;
  mem_status.dwLength = sizeof(mem_status);
  if (!GlobalMemoryStatusEx(&mem_status)) return 0;
  return mem_status.ullTotalPhys;
}

auto format_percent(double value) -> std::wstring {
  wchar_t buffer[32];
  swprintf_s(buffer, L"%.1f", value);
  return buffer;
}

auto custom_field_header(std::string_view field) -> std::wstring {
  if (field == "pid") return L"PID";
  if (field == "ppid") return L"PPID";
  if (field == "comm") return L"COMMAND";
  if (field == "args") return L"COMMAND";
  if (field == "user") return L"USER";
  if (field == "etime") return L"ELAPSED";
  if (field == "rss") return L"RSS";
  if (field == "pmem") return L"%MEM";
  if (field == "pcpu") return L"%CPU";
  return utf8_to_wstring(std::string(field));
}

auto custom_field_value(const ProcessInfo& proc, std::string_view field,
                        unsigned long long total_memory) -> std::wstring {
  if (field == "pid") return std::to_wstring(proc.pid);
  if (field == "ppid") return std::to_wstring(proc.ppid);
  if (field == "comm") return proc.name;
  if (field == "args")
    return proc.command_line.empty() ? proc.name : proc.command_line;
  if (field == "user") return proc.user;
  if (field == "etime") return format_etime(process_elapsed_seconds(proc));
  if (field == "rss") return std::to_wstring(proc.working_set_size / 1024);
  if (field == "pmem") {
    const double percent = total_memory == 0
                               ? 0.0
                               : (static_cast<double>(proc.working_set_size) *
                                  100.0 / static_cast<double>(total_memory));
    return format_percent(percent);
  }
  if (field == "pcpu") {
    const auto elapsed = process_elapsed_seconds(proc);
    const double percent = elapsed == 0 ? 0.0
                                        : (process_cpu_seconds(proc) * 100.0 /
                                           static_cast<double>(elapsed));
    return format_percent(percent);
  }
  return L"";
}

auto join_columns(const std::vector<std::wstring>& columns) -> std::wstring {
  std::wstring line;
  for (size_t i = 0; i < columns.size(); ++i) {
    if (i != 0) line += L" ";
    line += columns[i];
  }
  return line;
}

auto print_custom(const std::vector<ProcessInfo>& processes,
                  const std::vector<std::string>& fields, bool no_headers)
    -> void {
  if (!no_headers) {
    std::vector<std::wstring> headers;
    headers.reserve(fields.size());
    for (const auto& field : fields)
      headers.push_back(custom_field_header(field));
    safePrintLn(join_columns(headers));
  }

  const auto total_memory = total_physical_memory();
  for (const auto& proc : processes) {
    std::vector<std::wstring> values;
    values.reserve(fields.size());
    for (const auto& field : fields) {
      values.push_back(custom_field_value(proc, field, total_memory));
    }
    safePrintLn(join_columns(values));
  }
}

// Main execution
auto run(const Config& cfg) -> int {
  auto result = enumerate_processes();
  if (!result) {
    cp::report_error(result, L"ps");
    return 1;
  }

  auto processes = *result;

  // Filter by user if specified
  if (!cfg.user_filter.empty()) {
    auto user_w = utf8_to_wstring(cfg.user_filter);
    auto it =
        std::remove_if(processes.begin(), processes.end(),
                       [&](const ProcessInfo& p) { return p.user != user_w; });
    processes.erase(it, processes.end());
  }

  // Filter by PID if specified
  if (!cfg.pid_filter.empty()) {
    auto it = std::remove_if(
        processes.begin(), processes.end(), [&](const ProcessInfo& p) {
          return std::ranges::find(cfg.pid_filter, p.pid) ==
                 cfg.pid_filter.end();
        });
    processes.erase(it, processes.end());
  }

  // Apply selection mode filtering
  if (!cfg.all_processes) {
    // Determine target user for default filtering
    std::wstring target_user;
    if (!cfg.user_filter.empty()) {
      target_user = utf8_to_wstring(cfg.user_filter);
    } else if (cfg.select_associated) {
      // -a without --user: show all users' associated processes
      target_user.clear();
    } else {
      // Default: show current user's processes
      target_user = get_process_user(GetCurrentProcess());
    }

    auto it = std::remove_if(
        processes.begin(), processes.end(), [&](const ProcessInfo& p) {
          // Exclude session leaders (ppid == 0, i.e., System Idle and System)
          if (cfg.select_associated && p.ppid == 0) {
            return true;
          }

          // Filter by terminal (has_console) unless -x lifts the restriction
          if (!cfg.lift_tty_restriction && !p.has_console) {
            return true;
          }

          // Filter by user if a target user is specified
          if (!target_user.empty() && p.user != target_user) {
            return true;
          }

          return false;
        });
    processes.erase(it, processes.end());
  }

  // Sort if requested
  if (!cfg.sort_key.empty()) {
    sort_processes(processes, cfg.sort_key);
  } else {
    // Default sort by PID
    sort_processes(processes, "pid");
  }

  // Print in requested format
  if (!cfg.format_fields.empty()) {
    print_custom(processes, cfg.format_fields, cfg.no_headers);
  } else if (cfg.full_format) {
    print_full(processes, cfg.no_headers);
  } else if (cfg.user_format || cfg.long_format) {
    print_user(processes, cfg.no_headers);
  } else {
    print_simple(processes, cfg.no_headers, cfg.wide_output);
  }

  return 0;
}

}  // namespace ps_pipeline

REGISTER_COMMAND(ps, "ps", "ps [options]",
                 "Report a snapshot of the current processes.\n"
                 "Shows information about running processes on Windows.",
                 "  ps\n"
                 "  ps -ef\n"
                 "  ps aux\n"
                 "  ps | grep explorer",
                 "top(1), kill(1), pgrep(1)", "WinuxCmd",
                 "Copyright  2026 WinuxCmd", PS_OPTIONS) {
  using namespace ps_pipeline;

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"ps");
    return 1;
  }

  return run(*cfg);
}
