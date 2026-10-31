// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include <psapi.h>

#include "core/command_macros.h"
#include "pch/pch.h"

#pragma comment(lib, "psapi.lib")

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

constexpr auto TIME_OPTIONS = std::array{
    // [GNU] -p, --portability: report timing in POSIX format.
    // GNU time's long name for -p is --portability; --posix is
    // kept as a compatibility alias.
    OPTION("-p", "--portability",
           "use the portable output format (POSIX 1003.2 "
           "style: real, user and sys, in seconds)"),
    OPTION("", "--posix", "same as --portability"),
    // [GNU] -f, --format FORMAT
    OPTION("-f", "--format",
           "use FORMAT to format the report instead of the "
           "default",
           STRING_TYPE),
    // [GNU] -o, --output FILE
    OPTION("-o", "--output",
           "write the report to FILE instead of standard error", STRING_TYPE),
    // [GNU] -a, --append
    OPTION("-a", "--append",
           "with -o, append to the output file instead of "
           "overwriting it"),
    // [GNU] -v, --verbose
    OPTION("-v", "--verbose",
           "print a verbose report with all available fields"),
    // [GNU] -q, --quiet
    OPTION("-q", "--quiet",
           "do not report abnormal termination of the command")};

namespace time_pipeline {
namespace cp = core::pipeline;

struct HandleCloser {
  void operator()(HANDLE handle) const {
    if (handle && handle != INVALID_HANDLE_VALUE) {
      CloseHandle(handle);
    }
  }
};

using unique_handle =
    std::unique_ptr<std::remove_pointer_t<HANDLE>, HandleCloser>;

struct Config {
  bool posix = false;
  bool verbose = false;
  bool quiet = false;
  bool append = false;
  std::string format;
  bool has_format = false;
  std::string output;
  std::string command;
  std::vector<std::string> args;
};

[[nodiscard]] auto filetime_to_ticks(const FILETIME& ft) -> unsigned long long {
  ULARGE_INTEGER value{};
  value.LowPart = ft.dwLowDateTime;
  value.HighPart = ft.dwHighDateTime;
  return value.QuadPart;
}

[[nodiscard]] auto filetime_to_seconds(const FILETIME& ft) -> double {
  constexpr double kTicksPerSecond = 10000000.0;
  return static_cast<double>(filetime_to_ticks(ft)) / kTicksPerSecond;
}

[[nodiscard]] auto build_config(const CommandContext<TIME_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.posix = ctx.get<bool>("-p", false) ||
              ctx.get<bool>("--portability", false) ||
              ctx.get<bool>("--posix", false);
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);
  cfg.quiet = ctx.get<bool>("-q", false) || ctx.get<bool>("--quiet", false);
  cfg.append = ctx.get<bool>("-a", false) || ctx.get<bool>("--append", false);
  cfg.format = ctx.get<std::string>("-f", "");
  if (cfg.format.empty()) {
    cfg.format = ctx.get<std::string>("--format", "");
  }
  cfg.has_format = ctx.has("-f") || ctx.has("--format");
  cfg.output = ctx.get<std::string>("-o", "");
  if (cfg.output.empty()) {
    cfg.output = ctx.get<std::string>("--output", "");
  }

  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  cfg.command = std::string(ctx.positionals[0]);
  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    cfg.args.push_back(std::string(ctx.positionals[i]));
  }
  return cfg;
}

[[nodiscard]] auto format_seconds(double seconds, bool posix) -> std::string {
  const auto value = std::max(seconds, 0.0);
  if (posix) {
    return std::format("{:.3f}", value);
  }
  return std::format("{:.3f}s", value);
}

// Measurements for one timed run, feeding both the -f format expander and
// the -v verbose report. Fields the Windows process APIs cannot supply
// (per-process swap counts, context-switch counts, socket traffic) stay 0,
// matching what GNU time prints for unavailable counters on other systems.
struct TimingData {
  double real_seconds = 0.0;
  double user_seconds = 0.0;
  double sys_seconds = 0.0;
  unsigned long long max_rss_kb = 0;
  unsigned long long minor_faults = 0;
  unsigned long page_size = 0;
  DWORD exit_code = 0;
  std::string command_line;
};

// [GNU] %E elapsed form: [hours:]minutes:seconds with two decimals, e.g.
// "0:00.05" or "1:02:03.50".
[[nodiscard]] auto format_elapsed_hms(double seconds) -> std::string {
  const auto value = std::max(seconds, 0.0);
  const auto total_centis = static_cast<long long>(value * 100.0 + 0.5);
  const long long hours = total_centis / 360000;
  const long long minutes = (total_centis / 6000) % 60;
  const long long secs = (total_centis / 100) % 60;
  const long long centis = total_centis % 100;
  if (hours > 0) {
    return std::format("{}:{:02}:{:02}.{:02}", hours, minutes, secs, centis);
  }
  return std::format("{}:{:02}.{:02}", minutes, secs, centis);
}

// [GNU] Expand a -f format string. Supported resource specifiers: %e %E
// %U %S %P %M %K %D %p %X %Z %F %R %W %c %w %I %O %r %s %k %x %C and %%.
// Specifiers without a Windows data source expand to 0. Backslash escapes
// \n, \t and \\ are honored, as in GNU time.
[[nodiscard]] auto expand_format(const std::string& format,
                                 const TimingData& data) -> std::string {
  std::string out;
  const double cpu_percent =
      data.real_seconds > 0.0
          ? (data.user_seconds + data.sys_seconds) / data.real_seconds * 100.0
          : 0.0;
  for (size_t i = 0; i < format.size(); ++i) {
    char c = format[i];
    if (c == '\\' && i + 1 < format.size()) {
      char next = format[++i];
      if (next == 'n') {
        out.push_back('\n');
      } else if (next == 't') {
        out.push_back('\t');
      } else if (next == '\\') {
        out.push_back('\\');
      } else {
        out.push_back(next);
      }
      continue;
    }
    if (c != '%' || i + 1 >= format.size()) {
      out.push_back(c);
      continue;
    }
    char spec = format[++i];
    switch (spec) {
      case '%':
        out.push_back('%');
        break;
      case 'e':
        out += std::format("{:.2f}", std::max(data.real_seconds, 0.0));
        break;
      case 'E':
        out += format_elapsed_hms(data.real_seconds);
        break;
      case 'U':
        out += std::format("{:.2f}", std::max(data.user_seconds, 0.0));
        break;
      case 'S':
        out += std::format("{:.2f}", std::max(data.sys_seconds, 0.0));
        break;
      case 'P':
        out += std::format("{:.0f}%", cpu_percent);
        break;
      case 'M':
        out += std::to_string(data.max_rss_kb);
        break;
      case 'Z':
        out += std::to_string(data.page_size);
        break;
      case 'R':
        out += std::to_string(data.minor_faults);
        break;
      case 'x':
        out += std::to_string(data.exit_code);
        break;
      case 'C':
        out += data.command_line;
        break;
      case 'K':
      case 'D':
      case 'p':
      case 'X':
      case 'F':
      case 'W':
      case 'c':
      case 'w':
      case 'I':
      case 'O':
      case 'r':
      case 's':
      case 'k':
        out.push_back('0');
        break;
      default:
        out.push_back('%');
        out.push_back(spec);
        break;
    }
  }
  return out;
}

// [GNU] The canned -v/--verbose layout from GNU time(1): one labeled line
// per field. Fields with no Windows source print 0.
[[nodiscard]] auto verbose_report(const TimingData& data) -> std::string {
  const double cpu_percent =
      data.real_seconds > 0.0
          ? (data.user_seconds + data.sys_seconds) / data.real_seconds * 100.0
          : 0.0;
  std::string out;
  auto line = [&out](std::string_view label, const std::string& value) {
    out.push_back('\t');
    out += label;
    out += ": ";
    out += value;
    out.push_back('\n');
  };
  line("Command being timed", "\"" + data.command_line + "\"");
  line("User time (seconds)", std::format("{:.2f}", data.user_seconds));
  line("System time (seconds)", std::format("{:.2f}", data.sys_seconds));
  line("Percent of CPU this job got", std::format("{:.0f}%", cpu_percent));
  line("Elapsed (wall clock) time (h:mm:ss or m:ss)",
       format_elapsed_hms(data.real_seconds));
  line("Average shared text size (kbytes)", "0");
  line("Average unshared data size (kbytes)", "0");
  line("Average stack size (kbytes)", "0");
  line("Average total size (kbytes)", "0");
  line("Maximum resident set size (kbytes)", std::to_string(data.max_rss_kb));
  line("Average resident set size (kbytes)", "0");
  line("Major (requiring I/O) page faults", "0");
  line("Minor (reclaiming a frame) page faults",
       std::to_string(data.minor_faults));
  line("Voluntary context switches", "0");
  line("Involuntary context switches", "0");
  line("Swaps", "0");
  line("File system inputs", "0");
  line("File system outputs", "0");
  line("Socket messages sent", "0");
  line("Socket messages received", "0");
  line("Signals delivered", "0");
  line("Page size (bytes)", std::to_string(data.page_size));
  line("Exit status", std::to_string(data.exit_code));
  return out;
}

[[nodiscard]] auto default_report(const TimingData& data, bool posix)
    -> std::string {
  std::string out;
  out += "real " + format_seconds(data.real_seconds, posix) + "\n";
  out += "user " + format_seconds(data.user_seconds, posix) + "\n";
  out += "sys " + format_seconds(data.sys_seconds, posix) + "\n";
  return out;
}

// NTSTATUS-style termination (access violation and friends): the child
// died from an unhandled exception rather than exiting normally.
[[nodiscard]] auto terminated_abnormally(DWORD exit_code) -> bool {
  return (exit_code & 0xF0000000u) == 0xC0000000u;
}

[[nodiscard]] auto run(const Config& cfg) -> int {
  // [GNU] -o/--output: the report file is opened before the command runs;
  // if it cannot be opened, time fails without running anything.
  unique_handle output_file;
  if (!cfg.output.empty()) {
    const std::wstring woutput =
        utf8_to_wstring(native_path::normalize_api_operand(cfg.output));
    HANDLE raw = CreateFileW(woutput.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                             nullptr, cfg.append ? OPEN_ALWAYS : CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, nullptr);
    if (raw == INVALID_HANDLE_VALUE) {
      const DWORD error = GetLastError();
      safeErrorPrintLn("time: cannot open '" + cfg.output +
                       "' for writing: " + win32_posix_error_text(error));
      return 125;
    }
    output_file.reset(raw);
    if (cfg.append) {
      SetFilePointer(output_file.get(), 0, nullptr, FILE_END);
    }
  }

  LARGE_INTEGER frequency{};
  LARGE_INTEGER start{};
  LARGE_INTEGER finish{};
  if (!QueryPerformanceFrequency(&frequency) ||
      !QueryPerformanceCounter(&start)) {
    cp::report_custom_error(L"time", L"failed to read high-resolution clock");
    return 125;
  }

  // Resolve the child with GNU execvp semantics (PATH-only; no
  // application-directory, cwd, or System32 fallback) so a name colliding
  // with a Windows image resolves from the caller's PATH. Batch scripts are
  // routed through the command interpreter.
  winux::childproc::ChildLaunchRequest time_launch_request;
  time_launch_request.command = cfg.command;
  for (const auto& arg : cfg.args) {
    time_launch_request.args.push_back(arg);
  }
  auto launch = winux::childproc::plan_child_launch(time_launch_request);
  if (launch.status != winux::childproc::ChildResolveStatus::Found) {
    const DWORD resolve_error =
        winux::childproc::child_status_to_win32_error(launch.status);
    safeErrorPrintLn("time: failed to run command '" + cfg.command +
                     "': " + win32_posix_error_text(resolve_error));
    return winux::childproc::child_spawn_exit_status(resolve_error);
  }
  std::wstring command_line = std::move(launch.command_line);

  STARTUPINFOW si{sizeof(si)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION pi{};
  const BOOL ok =
      CreateProcessW(launch.application_name->c_str(), command_line.data(),
                     nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si, &pi);
  if (!ok) {
    const DWORD error = GetLastError();
    safeErrorPrintLn("time: failed to run command '" + cfg.command +
                     "': " + win32_posix_error_text(error));
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? 127
                                                                          : 126;
  }

  unique_handle process(pi.hProcess);
  unique_handle thread(pi.hThread);

  const DWORD wait_result = WaitForSingleObject(process.get(), INFINITE);
  if (wait_result != WAIT_OBJECT_0) {
    cp::report_custom_error(L"time", L"failed while waiting for command");
    return 125;
  }

  if (!QueryPerformanceCounter(&finish)) {
    cp::report_custom_error(L"time", L"failed to read high-resolution clock");
    return 125;
  }

  DWORD exit_code = 0;
  if (!GetExitCodeProcess(process.get(), &exit_code)) {
    cp::report_custom_error(L"time", L"failed to read command exit code");
    return 125;
  }

  FILETIME creation{};
  FILETIME exit{};
  FILETIME kernel{};
  FILETIME user{};
  if (!GetProcessTimes(process.get(), &creation, &exit, &kernel, &user)) {
    cp::report_custom_error(L"time", L"failed to read process times");
    return 125;
  }

  TimingData data;
  data.real_seconds = static_cast<double>(finish.QuadPart - start.QuadPart) /
                      static_cast<double>(frequency.QuadPart);
  data.user_seconds = filetime_to_seconds(user);
  data.sys_seconds = filetime_to_seconds(kernel);
  data.exit_code = exit_code;
  data.command_line = cfg.command;
  for (const auto& arg : cfg.args) {
    data.command_line += " ";
    data.command_line += arg;
  }

  SYSTEM_INFO system_info{};
  GetSystemInfo(&system_info);
  data.page_size = system_info.dwPageSize;

  PROCESS_MEMORY_COUNTERS memory{};
  memory.cb = sizeof(memory);
  if (GetProcessMemoryInfo(process.get(), &memory, sizeof(memory))) {
    data.max_rss_kb = memory.PeakWorkingSetSize / 1024;
    data.minor_faults = memory.PageFaultCount;
  }

  std::string report;
  if (cfg.has_format) {
    report = expand_format(cfg.format, data);
    report.push_back('\n');
  } else if (cfg.verbose) {
    report = verbose_report(data);
  } else {
    report = default_report(data, cfg.posix);
  }

  // [GNU] -q/--quiet suppresses the abnormal-termination report; without
  // it, a child that died from an unhandled exception is called out in the
  // report (the Windows counterpart of GNU's "terminated by signal" line).
  if (!cfg.quiet && terminated_abnormally(exit_code)) {
    report += std::format("Command terminated abnormally (status 0x{:08X})\n",
                          exit_code);
  }

  if (output_file) {
    DWORD written = 0;
    if (!WriteFile(output_file.get(), report.data(),
                   static_cast<DWORD>(report.size()), &written, nullptr) ||
        written != report.size()) {
      cp::report_custom_error(L"time", L"failed to write the report file");
      return 125;
    }
  } else {
    safeErrorPrint(report);
  }

  return static_cast<int>(exit_code);
}
}  // namespace time_pipeline

REGISTER_COMMAND(
    time_cmd, "time", "time [OPTION]... COMMAND [ARG]...",
    "Run COMMAND, then print system resource usage.\n"
    "\n"
    "The command is executed as a child process; its exit status is returned\n"
    "unchanged.  After the command finishes, a resource report is written\n"
    "to standard error, or to the file named by -o/--output:\n"
    "\n"
    "  real <elapsed wall clock time in seconds>\n"
    "  user <user cpu time in seconds>\n"
    "  sys  <system cpu time in seconds>\n"
    "\n"
    "With -p/--portability, the same three lines use the POSIX 1003.2\n"
    "style required by GNU time's --portability option.  Times are\n"
    "printed in seconds with three decimal places; the default format\n"
    "appends 's' to each value, the -p format does not.\n"
    "\n"
    "With -f/--format, FORMAT selects the output instead: the\n"
    "resource specifiers %e (real), %E (real, [h:]m:ss), %U (user), %S\n"
    "(system), %P (percent of CPU), %M (maximum resident set size in\n"
    "Kbytes), %Z (page size), %R (minor page faults), %x (exit status)\n"
    "and %C (command line) are expanded; specifiers whose counters have\n"
    "no Windows source (%K %D %p %X %F %W %c %w %I %O %r %s %k) expand\n"
    "to 0.  With -v/--verbose, a full field-by-field report is printed.\n"
    "\n"
    "The exit status is that of COMMAND if it was invoked; otherwise 127\n"
    "if COMMAND could not be found, 126 if it could be found but could\n"
    "not be invoked, and 125 if time itself failed.",
    "  time true\n"
    "  time -p cmd.exe /c exit 7\n"
    "  time -o report.txt -f \"elapsed %e, status %x\" dir",
    "bash(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TIME_OPTIONS) {
  using namespace time_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("time: missing operand");
      safeErrorPrintLn("Try 'time --help' for more information.");
      return 125;
    }
    cp::report_error(cfg_result, L"time");
    return 1;
  }

  return run(*cfg_result);
}
