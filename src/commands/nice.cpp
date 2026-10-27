// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for nice command.
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

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr NICE_OPTIONS =
    // [DIFFERS]
    std::array{OPTION("-n", "--adjustment", "adjust increment", STRING_TYPE)};

namespace {
constexpr int kNiceNoOverflowBound = 50;

auto nice_command_status_from_create_error(DWORD error) -> int {
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      return 127;
    default:
      return 126;
  }
}

auto nice_windows_error_text(DWORD error) -> std::string {
  return win32_posix_error_text(error);
}

auto parse_adjustment_value(std::string_view raw) -> std::optional<int> {
  if (raw.empty()) {
    return std::nullopt;
  }

  int value = 0;
  auto [ptr, ec] = std::from_chars(raw.data(), raw.data() + raw.size(), value);
  if (ec == std::errc::result_out_of_range) {
    bool negative = raw.front() == '-';
    return negative ? -kNiceNoOverflowBound : kNiceNoOverflowBound;
  }
  if (ec != std::errc() || ptr != raw.data() + raw.size()) {
    return std::nullopt;
  }

  return value;
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    nice,
    /* cmd_name */ "nice",
    /* cmd_synopsis */ "nice [OPTION] [COMMAND [ARG]...]",
    /* cmd_desc */
    "Run a program with modified scheduling priority.\n"
    "Run COMMAND with an adjusted niceness, which affects process scheduling.\n"
    "With no COMMAND, print the current niceness. Niceness values range from\n"
    "-20 (most favorable) to 19 (least favorable). Default is 10.",
    /* examples */
    "  nice -n 5 tar -cf archive.tar /home\n"
    "  nice -n -10 make\n"
    "  nice",
    /* see_also */ "renice, nohup",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ NICE_OPTIONS) {
  int adjustment = 10;  // Default adjustment
  bool has_explicit_adjustment = false;

  // Parse adjustment option
  std::string parsed_adjustment = ctx.get<std::string>("-n", "");
  if (parsed_adjustment.empty()) {
    parsed_adjustment = ctx.get<std::string>("--adjustment", "");
  }
  if (!parsed_adjustment.empty()) {
    auto adjustment_value = parse_adjustment_value(parsed_adjustment);
    if (!adjustment_value) {
      safeErrorPrintLn("nice: invalid adjustment '" + parsed_adjustment + "'");
      safeErrorPrintLn("Try 'nice --help' for more information.");
      return 125;
    }
    adjustment = *adjustment_value;
    has_explicit_adjustment = true;
  }

  // If no command provided, print current priority
  if (ctx.positionals.empty()) {
    if (has_explicit_adjustment) {
      safeErrorPrintLn("nice: A command must be given with an adjustment.");
      safeErrorPrintLn("Try 'nice --help' for more information.");
      return 125;
    }
    safePrintLn(std::to_string(win32_current_niceness()));
    return 0;
  }

  // Execute command with adjusted priority. The child is resolved with
  // GNU execvp semantics (PATH-only; no application-directory, cwd, or
  // System32 fallback) so a name colliding with a Windows image (sort,
  // find, timeout, ...) resolves from the caller's PATH.
  winux::childproc::ChildLaunchRequest nice_launch_request;
  nice_launch_request.command = std::string(ctx.positionals[0]);
  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    nice_launch_request.args.emplace_back(ctx.positionals[i]);
  }
  auto launch = winux::childproc::plan_child_launch(nice_launch_request);
  if (launch.status != winux::childproc::ChildResolveStatus::Found) {
    const DWORD resolve_error =
        winux::childproc::child_status_to_win32_error(launch.status);
    safeErrorPrintLn("nice: failed to run command '" +
                     std::string(ctx.positionals[0]) +
                     "': " + nice_windows_error_text(resolve_error));
    return nice_command_status_from_create_error(resolve_error);
  }

  STARTUPINFOW si = {sizeof(si)};
  PROCESS_INFORMATION pi;

  // Determine priority class based on adjustment
  int target_niceness =
      std::clamp(win32_current_niceness() + adjustment, -20, 19);
  DWORD priority_class = win32_priority_class_for_niceness(target_niceness);

  // [GNU] The child is created suspended and the priority class is
  // applied with SetPriorityClass so that a failure is observable:
  // GNU nice prints "cannot set niceness: <reason>" and runs the
  // command anyway when the requested priority cannot be applied,
  // while passing the class as a CreateProcessW flag would have it
  // silently clamped with no way to notice (#1359).
  if (!CreateProcessW(launch.application_name->c_str(),
                      launch.command_line.data(), nullptr, nullptr, FALSE,
                      CREATE_SUSPENDED, nullptr, nullptr, &si, &pi)) {
    DWORD error = GetLastError();
    safeErrorPrintLn("nice: failed to run command '" +
                     std::string(ctx.positionals[0]) +
                     "': " + nice_windows_error_text(error));
    return nice_command_status_from_create_error(error);
  }

  bool priority_applied =
      SetPriorityClass(pi.hProcess, priority_class) != FALSE;
  DWORD priority_error = priority_applied ? ERROR_SUCCESS : GetLastError();
  if (priority_applied && GetPriorityClass(pi.hProcess) != priority_class) {
    // The requested class was silently clamped to a lower one: the
    // requested priority was not applied either.
    priority_applied = false;
    priority_error = ERROR_ACCESS_DENIED;
  }
  if (!priority_applied) {
    safeErrorPrintLn("nice: cannot set niceness: " +
                     nice_windows_error_text(priority_error));
  }

  if (ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
    DWORD error = GetLastError();
    TerminateProcess(pi.hProcess, 125);
    WaitForSingleObject(pi.hProcess, INFINITE);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    safeErrorPrintLn("nice: failed to run command '" +
                     std::string(ctx.positionals[0]) +
                     "': " + nice_windows_error_text(error));
    return nice_command_status_from_create_error(error);
  }

  // Wait for process to complete
  WaitForSingleObject(pi.hProcess, INFINITE);

  DWORD exit_code;
  GetExitCodeProcess(pi.hProcess, &exit_code);

  CloseHandle(pi.hProcess);
  CloseHandle(pi.hThread);

  return static_cast<int>(exit_code);
}
