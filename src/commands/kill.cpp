/*
 *  Copyright  2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: kill.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

/// @contributors:
///   - @contributor1 arookieofc 2128194521@qq.com
///   - @contributor2 <email2@example.com>
///   - @contributor3 <email3@example.com>
/// @Description: Implementation for kill.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright  2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

/**
 * @brief KILL command options definition
 *
 * This array defines all the options supported by the kill command.
 * Each option is described with its short form, long form, and description.
 *
 * @par Options:
 *
 * - @a -s, @a --signal: Specify the signal to send [IMPLEMENTED]
 * - @a -l, @a --list: List signal names [IMPLEMENTED]
 * - @a -L, @a --table: List signal names in a table [IMPLEMENTED]
 * - @a -9: Send SIGKILL (force kill) [IMPLEMENTED]
 * - @a -15: Send SIGTERM (graceful termination) [IMPLEMENTED]
 */

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr KILL_OPTIONS = std::array{
    OPTION("-s", "--signal", "specify the signal to send", STRING_TYPE),
    OPTION("-l", "--list", "list signal names"),
    OPTION("-L", "--table", "list signal names in a table"),
    OPTION("-9", "", "send SIGKILL (force kill)"),
    OPTION("-15", "", "send SIGTERM (graceful termination)")};

// ======================================================
// Signal definitions (Unix-like signals mapped to Windows)
// ======================================================
namespace kill_constants {
struct SignalInfo {
  int number;
  std::string_view name;
  std::string_view description;
};

// Common Unix signals
constexpr std::array<SignalInfo, 15> SIGNALS = {{
    {1, "HUP", "Hangup"},
    {2, "INT", "Interrupt"},
    {3, "QUIT", "Quit"},
    {6, "ABRT", "Abort"},
    {9, "KILL", "Kill (cannot be caught or ignored)"},
    {11, "SEGV", "Segmentation fault"},
    {13, "PIPE", "Broken pipe"},
    {14, "ALRM", "Alarm clock"},
    {15, "TERM", "Termination"},
    {17, "STOP", "Stop (cannot be caught or ignored)"},
    {18, "TSTP", "Terminal stop"},
    {19, "CONT", "Continue"},
    {20, "CHLD", "Child status changed"},
    {21, "TTIN", "Background read from tty"},
    {22, "TTOU", "Background write to tty"},
}};

// Get signal number by name
auto get_signal_by_name(const std::string& name) -> std::optional<int> {
  std::string upper_name = name;
  std::transform(upper_name.begin(), upper_name.end(), upper_name.begin(),
                 ::toupper);

  // Remove SIG prefix if present
  if (upper_name.starts_with("SIG")) {
    upper_name = upper_name.substr(3);
  }

  for (const auto& sig : SIGNALS) {
    if (sig.name == upper_name) {
      return sig.number;
    }
  }
  return std::nullopt;
}

// Get signal info by number
auto get_signal_info(int signal_number) -> std::optional<SignalInfo> {
  for (const auto& sig : SIGNALS) {
    if (sig.number == signal_number) {
      return sig;
    }
  }
  return std::nullopt;
}

}  // namespace kill_constants

// ======================================================
// Pipeline components
// ======================================================
namespace kill_pipeline {
namespace cp = core::pipeline;

// ----------------------------------------------
// 1. List signals
// ----------------------------------------------
auto list_signals(bool table_format) -> cp::Result<bool> {
  if (table_format) {
    safePrint("Signal  Name    Description\n");
    safePrint("------  ------  -----------\n");
    for (const auto& sig : kill_constants::SIGNALS) {
      char buffer[256];
      snprintf(buffer, sizeof(buffer), "%-7d %-7.*s %.*s\n", sig.number,
               static_cast<int>(sig.name.size()), sig.name.data(),
               static_cast<int>(sig.description.size()),
               sig.description.data());
      safePrint(buffer);
    }
  } else {
    for (const auto& sig : kill_constants::SIGNALS) {
      safePrint(sig.name);
      safePrint(" ");
    }
    safePrint("\n");
  }
  return true;
}

// ----------------------------------------------
// 2. Parse signal argument
// ----------------------------------------------
auto parse_signal(const std::string& signal_arg) -> cp::Result<int> {
  // Try to parse as number first
  try {
    int signal_num = std::stoi(signal_arg);
    if (signal_num < 0 || signal_num > 64) {
      return std::unexpected("invalid signal number");
    }
    return signal_num;
  } catch (...) {
    // Try to parse as signal name
    auto signal_num = kill_constants::get_signal_by_name(signal_arg);
    if (!signal_num) {
      return std::unexpected("unknown signal: " + signal_arg);
    }
    return *signal_num;
  }
}

// ----------------------------------------------
// 3. Terminate process
// ----------------------------------------------
auto terminate_process(DWORD pid, int signal, bool verbose)
    -> cp::Result<bool> {
  // Open process with necessary access rights
  DWORD access_rights = PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION;
  HANDLE hProcess = OpenProcess(access_rights, FALSE, pid);

  if (hProcess == NULL) {
    DWORD error = GetLastError();
    if (error == ERROR_ACCESS_DENIED) {
      return std::unexpected("permission denied");
    } else if (error == ERROR_INVALID_PARAMETER) {
      return std::unexpected("no such process");
    } else {
      return std::unexpected("cannot open process");
    }
  }

  // Check if process is still running
  DWORD exitCode;
  if (GetExitCodeProcess(hProcess, &exitCode)) {
    if (exitCode != STILL_ACTIVE) {
      CloseHandle(hProcess);
      return std::unexpected("process already terminated");
    }
  }

  bool success = false;

  // Handle different signals
  if (signal == 0) {
    // Signal 0: just check if process exists
    success = true;
  } else if (signal == 9) {
    // SIGKILL: force termination
    success = TerminateProcess(hProcess, 1) != 0;
  } else if (signal == 15 || signal == 2) {
    // SIGTERM or SIGINT: graceful termination
    // Try to find and close main window first
    struct EnumData {
      DWORD targetPid;
      HWND foundHwnd;
    };

    EnumData enumData = {pid, NULL};

    EnumWindows(
        [](HWND hwnd, LPARAM lParam) -> BOOL {
          EnumData* pData = reinterpret_cast<EnumData*>(lParam);
          DWORD windowPid;
          GetWindowThreadProcessId(hwnd, &windowPid);
          if (windowPid == pData->targetPid && IsWindowVisible(hwnd)) {
            pData->foundHwnd = hwnd;
            return FALSE;  // Stop enumeration
          }
          return TRUE;  // Continue enumeration
        },
        reinterpret_cast<LPARAM>(&enumData));

    if (enumData.foundHwnd != NULL) {
      // Send WM_CLOSE message for graceful shutdown
      PostMessage(enumData.foundHwnd, WM_CLOSE, 0, 0);

      // Wait up to 5 seconds for process to exit
      DWORD wait_result = WaitForSingleObject(hProcess, 5000);
      if (wait_result == WAIT_OBJECT_0) {
        success = true;
      } else {
        // If still running, force terminate
        success = TerminateProcess(hProcess, 1) != 0;
      }
    } else {
      // No window found, force terminate
      success = TerminateProcess(hProcess, 1) != 0;
    }
  } else {
    // Other signals: just terminate
    success = TerminateProcess(hProcess, 1) != 0;
  }

  CloseHandle(hProcess);

  if (!success) {
    return std::unexpected("failed to terminate process");
  }

  if (verbose) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "Terminated process %lu\n", pid);
    safePrint(buffer);
  }

  return true;
}

// ----------------------------------------------
// 4. Parse PID list
// ----------------------------------------------
auto parse_pids(const std::vector<std::string>& pid_args)
    -> cp::Result<std::vector<DWORD>> {
  std::vector<DWORD> pids;

  for (const auto& pid_str : pid_args) {
    try {
      long long pid_value = std::stoll(pid_str);
      if (pid_value <= 0 || pid_value > UINT32_MAX) {
        return std::unexpected("invalid PID: " + pid_str);
      }
      pids.push_back(static_cast<DWORD>(pid_value));
    } catch (...) {
      return std::unexpected("invalid PID: " + pid_str);
    }
  }

  if (pids.empty()) {
    return std::unexpected("no process ID specified");
  }

  return pids;
}

// ----------------------------------------------
// 5. Process command
// ----------------------------------------------
template <size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<bool> {
  bool list = ctx.get<bool>("--list", false);
  list |= ctx.get<bool>("-l", false);
  bool table = ctx.get<bool>("--table", false);
  table |= ctx.get<bool>("-L", false);

  // Handle list/table options
  if (list || table) {
    return list_signals(table);
  }

  // Parse signal
  int signal = 15;  // Default: SIGTERM

  if (ctx.get<bool>("-9", false)) {
    signal = 9;
  } else if (ctx.get<bool>("-15", false)) {
    signal = 15;
  } else {
    std::string signal_str = ctx.get<std::string>("--signal", "");
    if (signal_str.empty()) {
      signal_str = ctx.get<std::string>("-s", "");
    }

    if (!signal_str.empty()) {
      auto signal_result = parse_signal(signal_str);
      if (!signal_result) {
        return std::unexpected(signal_result.error());
      }
      signal = *signal_result;
    }
  }

  // Parse PIDs from positional arguments
  std::vector<std::string> pid_args;
  for (auto arg : ctx.positionals) {
    pid_args.push_back(std::string(arg));
  }

  auto pids_result = parse_pids(pid_args);
  if (!pids_result) {
    return std::unexpected(pids_result.error());
  }

  auto pids = *pids_result;
  bool all_success = true;

  // Terminate each process
  for (DWORD pid : pids) {
    auto result = terminate_process(pid, signal, false);
    if (!result) {
      safeErrorPrint("kill: (");
      safeErrorPrint(std::to_string(pid));
      safeErrorPrint(") - ");
      safeErrorPrint(result.error());
      safeErrorPrint("\n");
      all_success = false;
    }
  }

  return all_success;
}

}  // namespace kill_pipeline

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    kill,
    /* name */
    "kill",

    /* synopsis */
    "send a signal to a process",

    /* description */
    "Send signals to processes, or list signals.\n"
    "\n"
    "The default signal for kill is TERM. Use -l or -L to list available "
    "signals.\n"
    "Particularly useful signals include HUP, INT, KILL, STOP, CONT, and 0.\n"
    "Alternate signals may be specified in three ways: -9, -SIGKILL or "
    "-KILL.\n"
    "\n"
    "Note: On Windows, most signals are mapped to process termination,\n"
    "except SIGTERM which attempts graceful shutdown first.",

    /* examples */
    "  kill 1234                Kill process 1234 with SIGTERM\n"
    "  kill -9 1234             Force kill process 1234\n"
    "  kill -s KILL 1234        Same as kill -9 1234\n"
    "  kill -l                  List all signal names\n"
    "  kill -L                  List signals in a table format",

    /* see also */
    "ps(1), pkill(1), killall(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright  2026 WinuxCmd",

    /* options */
    KILL_OPTIONS) {
  using namespace kill_pipeline;
  using namespace core::pipeline;

  auto result = process_command(ctx);
  if (!result) {
    report_error(result, L"kill");
    return 1;
  }

  return *result ? 0 : 1;
}
