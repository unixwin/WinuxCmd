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
 *  - File: timeout.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for timeout.
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

auto constexpr TIMEOUT_OPTIONS = std::array{
    OPTION("-s", "--signal", "specify the signal to be sent on timeout",
           STRING_TYPE),
    OPTION("-k", "--kill-after",
           "also send a KILL signal if COMMAND still running", STRING_TYPE),
    OPTION("-f", "--foreground",
           "when not running timeout directly from a shell prompt", BOOL_TYPE),
    OPTION("-p", "--preserve-status", "exit with the same status as COMMAND",
           BOOL_TYPE),
    OPTION("-v", "--verbose", "diagnose to stderr any signal sent on timeout",
           BOOL_TYPE)};

namespace timeout_pipeline {
namespace cp = core::pipeline;

struct Config {
  int64_t duration_ms = 0;
  int64_t kill_after_ms = 0;
  int signal = 15;  // SIGTERM
  bool foreground = false;
  bool preserve_status = false;
  bool verbose = false;
  std::string command;
  SmallVector<std::string, 64> args;
};

auto parse_signal(const std::string& signal) -> cp::Result<int> {
  if (signal.empty()) return std::unexpected("invalid signal");

  try {
    size_t idx = 0;
    int value = std::stoi(signal, &idx);
    if (idx == signal.size() && value >= 0) return value;
  } catch (...) {
  }

  std::string name;
  name.reserve(signal.size());
  for (unsigned char ch : signal) {
    name.push_back(static_cast<char>(std::toupper(ch)));
  }
  if (name.starts_with("SIG")) {
    name = name.substr(3);
  }

  static const std::unordered_map<std::string, int> signals = {
      {"HUP", 1},  {"INT", 2},   {"QUIT", 3}, {"ABRT", 6},
      {"KILL", 9}, {"ALRM", 14}, {"TERM", 15}};

  if (auto it = signals.find(name); it != signals.end()) {
    return it->second;
  }

  return std::unexpected("invalid signal");
}

auto parse_duration(const std::string& duration) -> cp::Result<int64_t> {
  try {
    // Support: N, Ns, Nm, Nh, Nd
    std::string s = duration;

    if (s.empty()) {
      return std::unexpected("invalid duration");
    }

    int64_t multiplier = 1;
    if (s.size() > 1) {
      char suffix = s.back();

      switch (suffix) {
        case 's':
        case 'S':
          multiplier = 1;
          s = s.substr(0, s.size() - 1);
          break;
        case 'm':
        case 'M':
          multiplier = 60;
          s = s.substr(0, s.size() - 1);
          break;
        case 'h':
        case 'H':
          multiplier = 3600;
          s = s.substr(0, s.size() - 1);
          break;
        case 'd':
        case 'D':
          multiplier = 86400;
          s = s.substr(0, s.size() - 1);
          break;
      }
    }

    size_t parsed = 0;
    double value = std::stod(s, &parsed);
    if (parsed != s.size() || value < 0) {
      return std::unexpected("invalid duration format");
    }
    return static_cast<int64_t>(value * multiplier *
                                1000);  // Convert to milliseconds
  } catch (...) {
    return std::unexpected("invalid duration format");
  }
}

auto build_config(const CommandContext<TIMEOUT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  auto signal_opt = ctx.get<std::string>("--signal", "");
  if (signal_opt.empty()) {
    signal_opt = ctx.get<std::string>("-s", "");
  }
  if (!signal_opt.empty()) {
    auto signal_result = parse_signal(signal_opt);
    if (!signal_result) {
      return std::unexpected(signal_result.error());
    }
    cfg.signal = *signal_result;
  }

  auto kill_opt = ctx.get<std::string>("--kill-after", "");
  if (kill_opt.empty()) {
    kill_opt = ctx.get<std::string>("-k", "");
  }
  if (!kill_opt.empty()) {
    auto duration_result = parse_duration(kill_opt);
    if (!duration_result) {
      return std::unexpected(duration_result.error());
    }
    cfg.kill_after_ms = *duration_result;
  }

  cfg.foreground =
      ctx.get<bool>("-f", false) || ctx.get<bool>("--foreground", false);
  cfg.preserve_status =
      ctx.get<bool>("-p", false) || ctx.get<bool>("--preserve-status", false);
  cfg.verbose = ctx.get<bool>("-v", false) || ctx.get<bool>("--verbose", false);

  // Get duration and command from positionals
  if (ctx.positionals.empty()) {
    return std::unexpected("missing operand");
  }

  auto duration_result = parse_duration(std::string(ctx.positionals[0]));
  if (!duration_result) {
    return std::unexpected(duration_result.error());
  }
  cfg.duration_ms = *duration_result;

  if (ctx.positionals.size() > 1) {
    cfg.command = std::string(ctx.positionals[1]);
    for (size_t i = 2; i < ctx.positionals.size(); ++i) {
      cfg.args.push_back(std::string(ctx.positionals[i]));
    }
  } else {
    return std::unexpected("missing command");
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  if (!cfg.command.empty()) {
    // Build command line
    std::string cmd_line = cfg.command;
    for (const auto& arg : cfg.args) {
      cmd_line += " " + arg;
    }

    // Convert to wide string for Windows API
    std::wstring wcmd_line = utf8_to_wstring(cmd_line);

    // Create process
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    if (!CreateProcessW(NULL,           // No module name
                        &wcmd_line[0],  // Command line
                        NULL,           // Process handle not inheritable
                        NULL,           // Thread handle not inheritable
                        TRUE,           // Set handle inheritance to TRUE
                        0,              // No creation flags
                        NULL,           // Use parent's environment block
                        NULL,           // Use parent's starting directory
                        &si,            // Pointer to STARTUPINFO structure
                        &pi  // Pointer to PROCESS_INFORMATION structure
                        )) {
      DWORD error = GetLastError();
      switch (error) {
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
          return 127;
        default:
          return 126;
      }
    }

    // Wait for process to finish or timeout
    DWORD timeout_ms =
        cfg.duration_ms == 0 ? INFINITE : static_cast<DWORD>(cfg.duration_ms);
    DWORD wait_result = WaitForSingleObject(pi.hProcess, timeout_ms);

    if (wait_result == WAIT_TIMEOUT) {
      // Process timed out, kill it
      int timeout_status =
          cfg.preserve_status ? std::min(255, 128 + cfg.signal) : 124;
      if (cfg.verbose) {
        safeErrorPrintLn("timeout: sending signal " +
                         std::to_string(cfg.signal) + " to command '" +
                         cfg.command + "'");
      }
      TerminateProcess(pi.hProcess, timeout_status);
      WaitForSingleObject(pi.hProcess, INFINITE);
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      return timeout_status;
    }

    if (wait_result != WAIT_OBJECT_0) {
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      return 125;
    }

    // Process finished normally, get exit code
    DWORD exit_code;
    if (!GetExitCodeProcess(pi.hProcess, &exit_code)) {
      CloseHandle(pi.hProcess);
      CloseHandle(pi.hThread);
      return 125;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return static_cast<int>(exit_code);
  }

  return 0;
}

}  // namespace timeout_pipeline

REGISTER_COMMAND(
    timeout, "timeout", "timeout [OPTION] DURATION COMMAND [ARG]...",
    "Start COMMAND, and kill it if still running after DURATION.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "DURATION is a floating point number with an optional suffix:\n"
    "'s' for seconds (default), 'm' for minutes, 'h' for hours or 'd' for "
    "days.\n"
    "\n"
    "Note: This is a simplified implementation. It doesn't actually\n"
    "execute the command. It's provided for API compatibility only.\n"
    "Full implementation would require process management on Windows.",
    "  timeout 5s command\n"
    "  timeout 1m command args\n"
    "  timeout -k 10s 30s command",
    "kill(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TIMEOUT_OPTIONS) {
  using namespace timeout_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"timeout");
    return 1;
  }

  return run(*cfg_result);
}
