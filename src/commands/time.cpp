// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

constexpr auto TIME_OPTIONS =
    // [GNU] -p, --posix (GNU time spells the long form --portability)
    std::array{OPTION("-p", "--posix",
                      "use the portable output format (POSIX 1003.2 "
                      "style: real, user and sys, in seconds)")};

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
  cfg.posix = ctx.get<bool>("-p", false) || ctx.get<bool>("--posix", false);

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

[[nodiscard]] auto run(const Config& cfg) -> int {
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

  const double wall_seconds =
      static_cast<double>(finish.QuadPart - start.QuadPart) /
      static_cast<double>(frequency.QuadPart);
  const double user_seconds = filetime_to_seconds(user);
  const double sys_seconds = filetime_to_seconds(kernel);

  safeErrorPrintLn("real " + format_seconds(wall_seconds, cfg.posix));
  safeErrorPrintLn("user " + format_seconds(user_seconds, cfg.posix));
  safeErrorPrintLn("sys " + format_seconds(sys_seconds, cfg.posix));

  return static_cast<int>(exit_code);
}
}  // namespace time_pipeline

REGISTER_COMMAND(
    time_cmd, "time", "time [OPTION]... COMMAND [ARG]...",
    "Run COMMAND, then print system resource usage.\n"
    "\n"
    "The command is executed as a child process; its exit status is returned\n"
    "unchanged.  After the command finishes, timing information is written\n"
    "to standard error:\n"
    "\n"
    "  real <elapsed wall clock time in seconds>\n"
    "  user <user cpu time in seconds>\n"
    "  sys  <system cpu time in seconds>\n"
    "\n"
    "With -p, print the same three lines in the POSIX 1003.2 style required\n"
    "by GNU time's --portability option.\n"
    "\n"
    "Times are printed in seconds with three decimal places; the default\n"
    "format appends 's' to each value, the -p format does not.\n"
    "\n"
    "The exit status is that of COMMAND if it was invoked; otherwise 127\n"
    "if COMMAND could not be found, 126 if it could be found but could\n"
    "not be invoked, and 125 if time itself failed.\n"
    "\n"
    "[DIFFERS] GNU Time options that select the output format or destination\n"
    "(-f/--format, -o/--output, -a/--append, -v/--verbose, -q/--quiet) are\n"
    "not implemented in this Windows port.",
    "  time true\n"
    "  time -p cmd.exe /c exit 7",
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
