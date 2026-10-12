// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Windows process timeout with signal and job control semantics.
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
           BOOL_TYPE),
    // [GNU]
    OPTION("-l", "", "list supported signals")};

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

auto timeout_signal_name(int signal) -> std::string {
  switch (signal) {
    case 0:
      return "0";
    case 1:
      return "HUP";
    case 2:
      return "INT";
    case 3:
      return "QUIT";
    case 6:
      return "ABRT";
    case 9:
      return "KILL";
    case 14:
      return "ALRM";
    case 15:
      return "TERM";
    default:
      return std::to_string(signal);
  }
}

auto parse_signal(const std::string& signal) -> cp::Result<int> {
  if (signal.empty()) return std::unexpected("'': invalid signal");

  int value = 0;
  auto [ptr, ec] =
      std::from_chars(signal.data(), signal.data() + signal.size(), value);
  if (ec == std::errc() && ptr == signal.data() + signal.size() && value >= 0) {
    return value;
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

  return std::unexpected("'" + signal + "': invalid signal");
}

auto parse_duration(const std::string& duration) -> cp::Result<int64_t> {
  // [GNU] DURATION is parsed like strtod in the C locale (hexadecimal floats
  // such as "0x1d" = 29, "inf", a leading '+', and leading whitespace are all
  // valid), followed by at most one lower-case suffix: 's' (default), 'm',
  // 'h', or 'd' (uutils #7671, #7678). NaN and negative values are rejected.
  const char* text = duration.c_str();
  char* end = nullptr;
  double value = std::strtod(text, &end);

  int64_t multiplier = 1;
  bool valid = end != text && !(value < 0) && !std::isnan(value);
  if (valid && *end != '\0') {
    if (*(end + 1) != '\0') {
      valid = false;
    } else {
      switch (*end) {
        case 's':
          break;
        case 'm':
          multiplier = 60;
          break;
        case 'h':
          multiplier = 3600;
          break;
        case 'd':
          multiplier = 86400;
          break;
        default:
          valid = false;
          break;
      }
    }
  }
  if (!valid) {
    return std::unexpected("invalid time interval '" + duration + "'");
  }

  double ms = value * static_cast<double>(multiplier) * 1000.0;
  // A non-finite or huge interval never fires, like GNU's unbound timeout.
  if (!std::isfinite(ms) ||
      ms >= static_cast<double>(std::numeric_limits<int64_t>::max()) ||
      ms >= 4294967294.0) {  // one less than the INFINITE marker
    return int64_t{0};
  }
  return static_cast<int64_t>(std::ceil(ms));  // Convert to milliseconds
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
    // Resolve the child with GNU execvp semantics (PATH-only; no
    // application-directory, cwd, or System32 fallback) so a name colliding
    // with a Windows image (timeout, sort, find, ...) resolves from the
    // caller's PATH. Batch scripts are routed through the command
    // interpreter, which CreateProcessW cannot launch directly.
    winux::childproc::ChildLaunchRequest timeout_launch_request;
    timeout_launch_request.command = cfg.command;
    for (const auto& arg : cfg.args) {
      timeout_launch_request.args.push_back(arg);
    }
    auto launch = winux::childproc::plan_child_launch(timeout_launch_request);
    if (launch.status != winux::childproc::ChildResolveStatus::Found) {
      const DWORD resolve_error =
          winux::childproc::child_status_to_win32_error(launch.status);
      safeErrorPrintLn("timeout: failed to run command '" + cfg.command +
                       "': " + win32_posix_error_text(resolve_error));
      return winux::childproc::child_spawn_exit_status(resolve_error);
    }
    std::wstring& wcmd_line = launch.command_line;

    // Create process
    STARTUPINFOW si = {sizeof(si)};
    PROCESS_INFORMATION pi;
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    si.hStdOutput = GetStdHandle(STD_OUTPUT_HANDLE);
    si.hStdError = GetStdHandle(STD_ERROR_HANDLE);

    UniqueHandle job;
    DWORD creation_flags = 0;
    if (HANDLE raw_job = CreateJobObjectW(nullptr, nullptr);
        raw_job != nullptr) {
      JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
      limits.BasicLimitInformation.LimitFlags =
          JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
      if (SetInformationJobObject(raw_job, JobObjectExtendedLimitInformation,
                                  &limits, sizeof(limits))) {
        job.reset(raw_job);
        creation_flags = CREATE_SUSPENDED;
      } else {
        CloseHandle(raw_job);
      }
    }

    if (!CreateProcessW(launch.application_name.has_value()
                            ? launch.application_name->c_str()
                            : nullptr,
                        wcmd_line.data(),  // Command line
                        NULL,              // Process handle not inheritable
                        NULL,              // Thread handle not inheritable
                        TRUE,              // Set handle inheritance to TRUE
                        creation_flags,
                        NULL,  // Use parent's environment block
                        NULL,  // Use parent's starting directory
                        &si,   // Pointer to STARTUPINFO structure
                        &pi    // Pointer to PROCESS_INFORMATION structure
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

    if (job) {
      if (!AssignProcessToJobObject(job.get(), pi.hProcess) ||
          ResumeThread(pi.hThread) == static_cast<DWORD>(-1)) {
        TerminateProcess(pi.hProcess, 125);
        WaitForSingleObject(pi.hProcess, INFINITE);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return 125;
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
      bool sent_initial_termination = false;
      if (cfg.verbose) {
        safeErrorPrintLn("timeout: sending signal " +
                         timeout_signal_name(cfg.signal) + " to command '" +
                         cfg.command + "'");
      }

      // GNU signal 0 is a liveness probe, not a terminating signal. Keep the
      // old Windows approximation for plain -s0 timeouts, but when --kill-after
      // is present defer termination until the KILL phase so the exit status
      // tracks the forced kill path.
      if (cfg.signal != 0 || cfg.kill_after_ms <= 0) {
        // On Windows we do not have GNU signal delivery semantics, but
        // --foreground must not disable timeout enforcement.
        TerminateProcess(pi.hProcess, timeout_status);
        sent_initial_termination = true;
      }

      // --kill-after: if specified, wait additional time then force kill
      if (cfg.kill_after_ms > 0) {
        DWORD kill_wait = static_cast<DWORD>(cfg.kill_after_ms);
        DWORD kill_result = WaitForSingleObject(pi.hProcess, kill_wait);
        if (kill_result == WAIT_TIMEOUT) {
          // Force kill after kill-after duration
          if (cfg.verbose) {
            safeErrorPrintLn("timeout: sending signal KILL to command '" +
                             cfg.command + "'");
          }
          timeout_status = 128 + 9;
          TerminateProcess(pi.hProcess, timeout_status);
        } else if (kill_result == WAIT_OBJECT_0 && !sent_initial_termination &&
                   cfg.signal == 0 && cfg.preserve_status) {
          DWORD exit_code = 0;
          if (GetExitCodeProcess(pi.hProcess, &exit_code)) {
            timeout_status = static_cast<int>(exit_code);
          }
        }
      }

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
    "The command is executed as a child process and terminated when the\n"
    "time limit expires.",
    "  timeout 5s command\n"
    "  timeout 1m command args\n"
    "  timeout -k 10s 30s command",
    "kill(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TIMEOUT_OPTIONS) {
  using namespace timeout_pipeline;

  // -l: list supported signals
  if (ctx.get<bool>("-l", false)) {
    safePrintLn("  0 (reserved)");
    safePrintLn("  1 HUP");
    safePrintLn("  2 INT");
    safePrintLn("  3 QUIT");
    safePrintLn("  6 ABRT");
    safePrintLn("  9 KILL");
    safePrintLn(" 14 ALRM");
    safePrintLn(" 15 TERM");
    return 0;
  }

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("timeout: missing operand");
      safeErrorPrintLn("Try 'timeout --help' for more information.");
      return 125;
    }
    if (cfg_result.error() == "missing command") {
      safeErrorPrintLn("timeout: missing command");
      safeErrorPrintLn("Try 'timeout --help' for more information.");
      return 125;
    }
    if (cfg_result.error().contains("invalid signal") ||
        cfg_result.error().contains("invalid time interval")) {
      safeErrorPrintLn("timeout: " + std::string(cfg_result.error()));
      return 125;
    }
    cp::report_error(cfg_result, L"timeout");
    return 1;
  }

  return run(*cfg_result);
}
