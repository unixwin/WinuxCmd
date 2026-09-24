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
    // [GNU] -p, --posix
    std::array{OPTION("-p", "--posix", "report timing in POSIX format")};

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

struct LaunchCommand {
  std::optional<std::wstring> application_name;
  std::wstring command_line;
};

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

[[nodiscard]] auto build_command_line(std::string_view command,
                                      std::span<const std::string> args)
    -> std::wstring {
  std::wstring command_line =
      quote_windows_command_arg(utf8_to_wstring(std::string(command)));
  for (const auto& arg : args) {
    append_windows_command_arg(command_line, utf8_to_wstring(arg));
  }
  return command_line;
}

[[nodiscard]] auto search_path(std::wstring_view file,
                               std::wstring_view extension = {})
    -> std::optional<std::wstring> {
  const std::wstring file_storage(file);
  const std::wstring extension_storage(extension);
  DWORD capacity = MAX_PATH;
  for (;;) {
    std::wstring buffer(capacity, L'\0');
    const DWORD length = SearchPathW(
        nullptr, file_storage.c_str(),
        extension_storage.empty() ? nullptr : extension_storage.c_str(),
        capacity, buffer.data(), nullptr);
    if (length == 0) return std::nullopt;
    if (length < capacity) {
      buffer.resize(length);
      return buffer;
    }
    if (length >= 32768) return std::nullopt;
    capacity = length + 1;
  }
}

[[nodiscard]] auto find_batch_wrapper(std::wstring_view command)
    -> std::optional<std::wstring> {
  const std::filesystem::path command_path{command};
  std::wstring extension = command_path.extension().wstring();
  std::ranges::transform(extension, extension.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(std::towlower(ch));
  });

  if (extension == L".cmd" || extension == L".bat") {
    return search_path(command);
  }
  if (!extension.empty()) return std::nullopt;

  for (const auto* candidate_extension : {L".cmd", L".bat"}) {
    if (auto path = search_path(command, candidate_extension)) return path;
  }
  return std::nullopt;
}

[[nodiscard]] auto build_launch_command(std::string_view command,
                                        std::span<const std::string> args)
    -> LaunchCommand {
  const std::wstring wcommand = utf8_to_wstring(std::string(command));
  if (auto batch_path = find_batch_wrapper(wcommand)) {
    wchar_t comspec[MAX_PATH]{};
    const DWORD length = GetEnvironmentVariableW(
        L"COMSPEC", comspec, static_cast<DWORD>(std::size(comspec)));
    std::wstring shell = L"cmd.exe";
    if (length > 0 && length < std::size(comspec)) {
      shell.assign(comspec, length);
    }

    std::wstring script_line = quote_windows_command_arg(*batch_path);
    for (const auto& arg : args) {
      append_windows_command_arg(script_line, utf8_to_wstring(arg));
    }
    return {std::move(shell), L"/d /s /c \"" + script_line + L"\""};
  }

  return {std::nullopt, build_command_line(command, args)};
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

  auto launch = build_launch_command(cfg.command, cfg.args);

  STARTUPINFOW si{sizeof(si)};
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
  si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

  PROCESS_INFORMATION pi{};
  std::wstring command_line = launch.command_line;
  const BOOL ok = CreateProcessW(
      launch.application_name ? launch.application_name->c_str() : nullptr,
      command_line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &si,
      &pi);
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
    "Run a command and report how long it took.\n"
    "\n"
    "The command is executed as a child process and the child exit status is\n"
    "returned unchanged. Timing data is written to standard error.\n"
    "\n"
    "With -p, print POSIX-compatible timing lines.",
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
