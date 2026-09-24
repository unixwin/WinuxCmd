// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
 * - @a -n: Specify the signal to send [IMPLEMENTED]
 * - @a -q, @a --queue: Queue an integer signal payload [DIFFERS: unsupported on
 * Windows]
 * - @a -l, @a --list: List or convert signal names [IMPLEMENTED]
 * - @a -L, @a --table, @a -t: List signal names in a table [IMPLEMENTED]
 * - @a -f, @a --force: Cygwin compatibility flag [WINDOWS NO-OP]
 * - @a -W, @a --winpid: Cygwin compatibility flag [WINDOWS NO-OP]
 * - @a -9: Send SIGKILL (force kill) [IMPLEMENTED]
 * - @a -15: Send SIGTERM (graceful termination) [IMPLEMENTED]
 * - @a -SIGNAME, @a -SIGSIGNAME: Obsolete signal-name form [IMPLEMENTED]
 */

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr KILL_OPTIONS = std::array{
    // [DIFFERS]
    OPTION("-s", "--signal", "specify the signal to send", STRING_TYPE),
    // [EXT]
    OPTION("-n", "", "specify the signal to send", STRING_TYPE),
    // [DIFFERS]
    OPTION("-q", "--queue",
           "queue an integer signal payload (unsupported on Windows)",
           STRING_TYPE),
    // [DIFFERS]
    OPTION("-l", "--list", "list signal names, or convert one signal",
           OPTIONAL_STRING_TYPE),
    // [DIFFERS]
    OPTION("-L", "--table", "list signal names in a table"),
    // [EXT]
    OPTION("-t", "", "list signal names in a table"),
    // [EXT]
    OPTION("-f", "--force",
           "force using Win32 termination if necessary; accepted no-op because "
           "WinuxCmd already uses Win32 process handles"),
    // [EXT]
    OPTION("-W", "--winpid",
           "treat pids as Windows PIDs; accepted no-op because WinuxCmd pids "
           "are already Windows PIDs"),
    // [EXT]
    OPTION("-NUM", "", "send signal number", INT_TYPE),
    // [EXT]
    OPTION("-0", "", "check whether a process exists"),
    // [EXT]
    OPTION("-9", "", "send SIGKILL (force kill)"),
    // [EXT]
    OPTION("-15", "", "send SIGTERM (graceful termination)"),
    // [GNU] Signal-name aliases are resolved by the shared signal lookup table.
    OPTION("-HUP", "", "send SIGHUP"),
    // [EXT]
    OPTION("-SIGHUP", "", "send SIGHUP"),
    // [EXT]
    OPTION("-INT", "", "send SIGINT"),
    // [EXT]
    OPTION("-SIGINT", "", "send SIGINT"),
    // [EXT]
    OPTION("-QUIT", "", "send SIGQUIT"),
    // [EXT]
    OPTION("-SIGQUIT", "", "send SIGQUIT"),
    // [EXT]
    OPTION("-ILL", "", "send SIGILL"),
    // [EXT]
    OPTION("-SIGILL", "", "send SIGILL"),
    // [EXT]
    OPTION("-TRAP", "", "send SIGTRAP"),
    // [EXT]
    OPTION("-SIGTRAP", "", "send SIGTRAP"),
    // [EXT]
    OPTION("-ABRT", "", "send SIGABRT"),
    // [EXT]
    OPTION("-SIGABRT", "", "send SIGABRT"),
    // [EXT]
    OPTION("-IOT", "", "send SIGIOT"),
    // [EXT]
    OPTION("-SIGIOT", "", "send SIGIOT"),
    // [EXT]
    OPTION("-EMT", "", "send SIGEMT"),
    // [EXT]
    OPTION("-SIGEMT", "", "send SIGEMT"),
    // [EXT]
    OPTION("-FPE", "", "send SIGFPE"),
    // [EXT]
    OPTION("-SIGFPE", "", "send SIGFPE"),
    // [EXT]
    OPTION("-KILL", "", "send SIGKILL"),
    // [EXT]
    OPTION("-SIGKILL", "", "send SIGKILL"),
    // [EXT]
    OPTION("-BUS", "", "send SIGBUS"),
    // [EXT]
    OPTION("-SIGBUS", "", "send SIGBUS"),
    // [EXT]
    OPTION("-SEGV", "", "send SIGSEGV"),
    // [EXT]
    OPTION("-SIGSEGV", "", "send SIGSEGV"),
    // [EXT]
    OPTION("-SYS", "", "send SIGSYS"),
    // [EXT]
    OPTION("-SIGSYS", "", "send SIGSYS"),
    // [EXT]
    OPTION("-PIPE", "", "send SIGPIPE"),
    // [EXT]
    OPTION("-SIGPIPE", "", "send SIGPIPE"),
    // [EXT]
    OPTION("-ALRM", "", "send SIGALRM"),
    // [EXT]
    OPTION("-SIGALRM", "", "send SIGALRM"),
    // [EXT]
    OPTION("-TERM", "", "send SIGTERM"),
    // [EXT]
    OPTION("-SIGTERM", "", "send SIGTERM"),
    // [EXT]
    OPTION("-URG", "", "send SIGURG"),
    // [EXT]
    OPTION("-SIGURG", "", "send SIGURG"),
    // [EXT]
    OPTION("-STOP", "", "send SIGSTOP"),
    // [EXT]
    OPTION("-SIGSTOP", "", "send SIGSTOP"),
    // [EXT]
    OPTION("-TSTP", "", "send SIGTSTP"),
    // [EXT]
    OPTION("-SIGTSTP", "", "send SIGTSTP"),
    // [EXT]
    OPTION("-CONT", "", "send SIGCONT"),
    // [EXT]
    OPTION("-SIGCONT", "", "send SIGCONT"),
    // [EXT]
    OPTION("-CHLD", "", "send SIGCHLD"),
    // [EXT]
    OPTION("-SIGCHLD", "", "send SIGCHLD"),
    // [EXT]
    OPTION("-CLD", "", "send SIGCLD"),
    // [EXT]
    OPTION("-SIGCLD", "", "send SIGCLD"),
    // [EXT]
    OPTION("-TTIN", "", "send SIGTTIN"),
    // [EXT]
    OPTION("-SIGTTIN", "", "send SIGTTIN"),
    // [EXT]
    OPTION("-TTOU", "", "send SIGTTOU"),
    // [EXT]
    OPTION("-SIGTTOU", "", "send SIGTTOU"),
    // [EXT]
    OPTION("-IO", "", "send SIGIO"),
    // [EXT]
    OPTION("-SIGIO", "", "send SIGIO"),
    // [EXT]
    OPTION("-POLL", "", "send SIGPOLL"),
    // [EXT]
    OPTION("-SIGPOLL", "", "send SIGPOLL"),
    // [EXT]
    OPTION("-XCPU", "", "send SIGXCPU"),
    // [EXT]
    OPTION("-SIGXCPU", "", "send SIGXCPU"),
    // [EXT]
    OPTION("-XFSZ", "", "send SIGXFSZ"),
    // [EXT]
    OPTION("-SIGXFSZ", "", "send SIGXFSZ"),
    // [EXT]
    OPTION("-VTALRM", "", "send SIGVTALRM"),
    // [EXT]
    OPTION("-SIGVTALRM", "", "send SIGVTALRM"),
    // [EXT]
    OPTION("-PROF", "", "send SIGPROF"),
    // [EXT]
    OPTION("-SIGPROF", "", "send SIGPROF"),
    // [EXT]
    OPTION("-WINCH", "", "send SIGWINCH"),
    // [EXT]
    OPTION("-SIGWINCH", "", "send SIGWINCH"),
    // [EXT]
    OPTION("-PWR", "", "send SIGPWR"),
    // [EXT]
    OPTION("-SIGPWR", "", "send SIGPWR"),
    // [EXT]
    OPTION("-LOST", "", "send SIGLOST"),
    // [EXT]
    OPTION("-SIGLOST", "", "send SIGLOST"),
    // [EXT]
    OPTION("-USR1", "", "send SIGUSR1"),
    // [EXT]
    OPTION("-SIGUSR1", "", "send SIGUSR1"),
    // [EXT]
    OPTION("-USR2", "", "send SIGUSR2"),
    // [EXT]
    OPTION("-SIGUSR2", "", "send SIGUSR2"),
    // [EXT]
    OPTION("-RTMIN", "", "send SIGRTMIN"),
    // [EXT]
    OPTION("-SIGRTMIN", "", "send SIGRTMIN"),
    // [EXT]
    OPTION("-RTMAX", "", "send SIGRTMAX"),
    // [EXT]
    OPTION("-SIGRTMAX", "", "send SIGRTMAX")};

// ======================================================
// Signal definitions (Unix-like signals mapped to Windows)
// ======================================================
namespace kill_constants {
constexpr int kRtMin = 32;
constexpr int kRtMax = 64;
constexpr int kMaxSignal = kRtMax;

struct SignalInfo {
  int number;
  std::string_view name;
  std::string_view description;
  bool primary;
};

// Cygwin's Windows signal numbering is the practical local reference for this
// command.  procps-ng and Cygwin both implement the same public conversion
// shape (-l HUP -> 1, -l 1 -> HUP), but their numeric tables are platform
// dependent.  Keep this table explicit so aliases and Windows-only numbering
// are auditable instead of hidden behind std::signal constants.
constexpr std::array<SignalInfo, 35> SIGNALS = {{
    {1, "HUP", "Hangup", true},
    {2, "INT", "Interrupt", true},
    {3, "QUIT", "Quit", true},
    {4, "ILL", "Illegal instruction", true},
    {5, "TRAP", "Trace/breakpoint trap", true},
    {6, "ABRT", "Abort", true},
    {6, "IOT", "Abort", false},
    {7, "EMT", "EMT instruction", true},
    {8, "FPE", "Floating point exception", true},
    {9, "KILL", "Kill (cannot be caught or ignored)", true},
    {10, "BUS", "Bus error", true},
    {11, "SEGV", "Segmentation fault", true},
    {12, "SYS", "Bad system call", true},
    {13, "PIPE", "Broken pipe", true},
    {14, "ALRM", "Alarm clock", true},
    {15, "TERM", "Termination", true},
    {16, "URG", "Urgent I/O condition", true},
    {17, "STOP", "Stop (cannot be caught or ignored)", true},
    {18, "TSTP", "Terminal stop", true},
    {19, "CONT", "Continue", true},
    {20, "CHLD", "Child status changed", true},
    {20, "CLD", "Child status changed", false},
    {21, "TTIN", "Background read from tty", true},
    {22, "TTOU", "Background write from tty", true},
    {23, "IO", "I/O possible", true},
    {23, "POLL", "I/O possible", false},
    {24, "XCPU", "CPU time limit exceeded", true},
    {25, "XFSZ", "File size limit exceeded", true},
    {26, "VTALRM", "Virtual timer expired", true},
    {27, "PROF", "Profiling timer expired", true},
    {28, "WINCH", "Window changed", true},
    {29, "PWR", "Power failure", true},
    {29, "LOST", "Resource lost", false},
    {30, "USR1", "User defined signal 1", true},
    {31, "USR2", "User defined signal 2", true},
}};

auto upper_ascii(std::string_view text) -> std::string {
  std::string upper;
  upper.reserve(text.size());
  for (unsigned char ch : text) {
    upper.push_back(static_cast<char>(std::toupper(ch)));
  }
  return upper;
}

auto parse_decimal(std::string_view text) -> std::optional<int> {
  if (text.empty()) return std::nullopt;
  int value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

auto parse_rt_signal(std::string_view name) -> std::optional<int> {
  if (name == "RTMIN") return kRtMin;
  if (name == "RTMAX") return kRtMax;

  if (name.starts_with("RTMIN+")) {
    auto offset = parse_decimal(name.substr(6));
    if (!offset || *offset < 0 || *offset > kRtMax - kRtMin) {
      return std::nullopt;
    }
    return kRtMin + *offset;
  }

  if (name.starts_with("RTMAX-")) {
    auto offset = parse_decimal(name.substr(6));
    if (!offset || *offset < 0 || *offset > kRtMax - kRtMin) {
      return std::nullopt;
    }
    return kRtMax - *offset;
  }

  if (name.starts_with("RT")) {
    auto offset = parse_decimal(name.substr(2));
    if (!offset || *offset < 0 || *offset > kRtMax - kRtMin) {
      return std::nullopt;
    }
    return kRtMin + *offset;
  }

  return std::nullopt;
}

// Get signal number by name.  Cygwin is case-sensitive, while procps-ng is
// case-insensitive; accepting lowercase is a GNU-compatible extension.
auto get_signal_by_name(std::string_view name) -> std::optional<int> {
  std::string upper_name = upper_ascii(name);
  if (upper_name.starts_with("SIG")) {
    upper_name = upper_name.substr(3);
  }

  if (auto rt = parse_rt_signal(upper_name)) {
    return rt;
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
    if (sig.number == signal_number && sig.primary) {
      return sig;
    }
  }
  return std::nullopt;
}

auto signal_number_to_name(int signal_number) -> std::optional<std::string> {
  if (signal_number == 0) return std::string("0");
  if (auto sig = get_signal_info(signal_number)) {
    return std::string(sig->name);
  }
  if (signal_number >= kRtMin && signal_number <= kRtMax) {
    return "RT" + std::to_string(signal_number - kRtMin);
  }
  return std::nullopt;
}

auto convert_signal_token(std::string_view token)
    -> std::optional<std::string> {
  if (auto number = parse_decimal(token)) {
    if (*number < 0 || *number > kMaxSignal) return std::nullopt;
    return signal_number_to_name(*number);
  }

  auto signal = get_signal_by_name(token);
  if (!signal || *signal < 0 || *signal > kMaxSignal) {
    return std::nullopt;
  }
  return std::to_string(*signal);
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
    int chars = 0;
    for (const auto& sig : kill_constants::SIGNALS) {
      char buffer[256];
      auto written =
          snprintf(buffer, sizeof(buffer), "%2d %-7.*s ", sig.number,
                   static_cast<int>(sig.name.size()), sig.name.data());
      if (written <= 0) continue;
      safePrint(buffer);
      chars += written;
      if (chars > 70) {
        safePrint("\n");
        chars = 0;
      }
    }
    safePrint("32 RTMIN   64 RTMAX\n");
  } else {
    int chars = 0;
    for (const auto& sig : kill_constants::SIGNALS) {
      std::string name(sig.name);
      safePrint(name);
      safePrint(" ");
      chars += static_cast<int>(name.size()) + 1;
      if (chars >= 72) {
        safePrint("\n");
        chars = 0;
      }
    }
    safePrint("RT<N> RTMIN+<N> RTMAX-<N>\n");
  }
  return true;
}

auto convert_and_print_signal(std::string_view signal_arg) -> cp::Result<bool> {
  auto converted = kill_constants::convert_signal_token(signal_arg);
  if (!converted) {
    // [GNU] operand2sig wording (uutils #14177)
    return std::unexpected("'" + std::string(signal_arg) + "': invalid signal");
  }
  safePrint(*converted);
  safePrint("\n");
  return true;
}

// ----------------------------------------------
// 2. Parse signal argument
// ----------------------------------------------
auto parse_signal(const std::string& signal_arg) -> cp::Result<int> {
  if (auto signal_num = kill_constants::parse_decimal(signal_arg)) {
    if (*signal_num < 0 || *signal_num > kill_constants::kMaxSignal) {
      // [GNU] operand2sig wording (uutils #14177)
      return std::unexpected("'" + signal_arg + "': invalid signal");
    }
    return *signal_num;
  }

  auto signal_num = kill_constants::get_signal_by_name(signal_arg);
  if (!signal_num) {
    return std::unexpected("'" + signal_arg + "': invalid signal");
  }
  return *signal_num;
}

auto parse_signal_option_name(std::string_view option_name)
    -> std::optional<int> {
  if (!option_name.starts_with("-") || option_name.size() <= 1) {
    return std::nullopt;
  }

  std::string_view raw = option_name.substr(1);
  if (auto signal_num = kill_constants::parse_decimal(raw)) {
    if (*signal_num >= 0 && *signal_num <= kill_constants::kMaxSignal) {
      return *signal_num;
    }
    return std::nullopt;
  }

  return kill_constants::get_signal_by_name(raw);
}

template <size_t N>
auto signal_from_option_occurrences(const CommandContext<N>& ctx)
    -> cp::Result<std::optional<int>> {
  std::optional<int> signal;
  if (!ctx.metas) return signal;

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (occurrence.index >= N) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];

    if (meta.short_name == "-s" || meta.long_name == "--signal" ||
        meta.short_name == "-n") {
      auto value = std::get_if<std::string>(&occurrence.value);
      if (!value) continue;

      // [GNU] a second signal spec is rejected, not silently overridden
      // (uutils #14177)
      if (signal) {
        return std::unexpected("'" + *value + "': multiple signals specified");
      }
      auto parsed = parse_signal(*value);
      if (!parsed) return std::unexpected(parsed.error());
      signal = *parsed;
      continue;
    }

    if (meta.short_name == "-NUM") {
      auto value = std::get_if<int>(&occurrence.value);
      if (value) {
        if (*value >= 0 && *value <= kill_constants::kMaxSignal) {
          signal = *value;
          continue;
        }
        // [GNU] operand2sig wording; the leading dash is stripped
        // (uutils #14177)
        return std::unexpected("'" + std::to_string(*value) +
                               "': invalid signal");
      }
      return std::unexpected("invalid signal number");
    }

    if (auto parsed = parse_signal_option_name(meta.short_name)) {
      // [GNU] a second signal spec is rejected, not silently overridden
      // (uutils #14177)
      if (signal) {
        return std::unexpected("'" + std::string(meta.short_name.substr(1)) +
                               "': multiple signals specified");
      }
      signal = *parsed;
    }
  }

  return signal;
}

template <size_t N>
auto validate_queue_option(const CommandContext<N>& ctx) -> cp::Result<bool> {
  // GNU kill uses sigqueue(3) for -q/--queue. Native Win32 has no matching
  // queued-signal payload, so fail explicitly instead of silently dropping it.
  for (const auto& occurrence : ctx.string_occurrences({"-q", "--queue"})) {
    auto parsed = kill_constants::parse_decimal(occurrence.value);
    if (!parsed) {
      return std::unexpected("queue value must be an integer: " +
                             occurrence.value);
    }
  }
  if (ctx.has("-q") || ctx.has("--queue")) {
    return std::unexpected(
        "queueing signal payloads is not supported on Windows");
  }
  return true;
}

template <size_t N>
auto list_conversion_argument(const CommandContext<N>& ctx)
    -> std::optional<std::string> {
  for (const auto& occurrence : ctx.string_occurrences({"-l", "--list"})) {
    if (!occurrence.value.empty()) {
      return occurrence.value;
    }
  }

  if (!ctx.positionals.empty()) {
    return std::string(ctx.positionals.front());
  }

  return std::nullopt;
}

// ----------------------------------------------
// 3. Terminate process
// ----------------------------------------------
auto terminate_process(DWORD pid, int signal, bool verbose)
    -> cp::Result<bool> {
  // Open process with necessary access rights
  DWORD access_rights =
      signal == 0
          ? PROCESS_QUERY_LIMITED_INFORMATION
          : (PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE);
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
    uint64_t pid_value = 0;
    auto [ptr, ec] = std::from_chars(
        pid_str.data(), pid_str.data() + pid_str.size(), pid_value);
    if (ec != std::errc() || ptr != pid_str.data() + pid_str.size() ||
        pid_value == 0 || pid_value > UINT32_MAX) {
      return std::unexpected("invalid PID: " + pid_str);
    }
    pids.push_back(static_cast<DWORD>(pid_value));
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
  bool list = ctx.has("--list") || ctx.has("-l");
  bool table = ctx.get<bool>("--table", false);
  table |= ctx.get<bool>("-L", false);
  table |= ctx.get<bool>("-t", false);

  // Handle list/table options
  if (list || table) {
    if (list && !table) {
      // [GNU] a -SPEC operand (consumed as the -NUM signal form) cannot be
      // combined with -l: operand2sig rejects the negative spec, so
      // "kill -l -9" is an error, not a signal listing (uutils #7066).
      if (ctx.count({"-NUM"}) > 0) {
        return std::unexpected("'-" + std::to_string(ctx.get<int>("-NUM", 0)) +
                               "': invalid signal");
      }
      if (auto conversion_arg = list_conversion_argument(ctx)) {
        return convert_and_print_signal(*conversion_arg);
      }
    }
    return list_signals(table);
  }

  auto queue_result = validate_queue_option(ctx);
  if (!queue_result) {
    return std::unexpected(queue_result.error());
  }

  // Parse signal
  int signal = 15;  // Default: SIGTERM

  auto signal_result = signal_from_option_occurrences(ctx);
  if (!signal_result) {
    return std::unexpected(signal_result.error());
  }
  bool signal_explicit = signal_result->has_value();
  if (*signal_result) {
    signal = **signal_result;
  }

  // Parse PIDs from positional arguments
  std::vector<std::string> pid_args;
  for (auto arg : ctx.positionals) {
    std::string text(arg);
    // [GNU] an unregistered -SPEC operand is a signal specification, so
    // -SIGSIGTERM is rejected as invalid instead of being parsed as an
    // option cluster (uutils #14177)
    if (text.size() > 1 && text[0] == '-' &&
        !kill_constants::parse_decimal(text.substr(1))) {
      std::string spec = text.substr(1);
      if (signal_explicit) {
        return std::unexpected("'" + spec + "': multiple signals specified");
      }
      auto parsed = kill_constants::get_signal_by_name(spec);
      if (!parsed) {
        return std::unexpected("'" + spec + "': invalid signal");
      }
      signal = *parsed;
      signal_explicit = true;
      continue;
    }
    // [GNU] operand2sig rejects a numeric -SPEC above the signal bound as a
    // signal, so -99 is not parsed as a PID (uutils #14177)
    if (text.size() > 1 && text[0] == '-') {
      auto numeric = kill_constants::parse_decimal(text.substr(1));
      if (numeric && (*numeric < 0 || *numeric > kill_constants::kMaxSignal)) {
        return std::unexpected("'" + text.substr(1) + "': invalid signal");
      }
    }
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
    "The default signal for kill is TERM. Use -l or -L/-t to list available "
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
    "  kill -n KILL 1234        Same as kill -9 1234\n"
    "  kill -s KILL 1234        Same as kill -9 1234\n"
    "  kill -KILL 1234          Same as kill -9 1234\n"
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
