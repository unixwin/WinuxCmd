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
    // [GNU] -a lifts the same-uid restriction on name operands
    // (kill.c:510-541); -p prints pids instead of signaling and
    // --verbose reports each signal as it is sent (kill.c kill_verbose).
    OPTION("-a", "--all",
           "do not restrict the name-to-pid conversion to the same uid"),
    // [GNU]
    OPTION("-p", "--pid", "print the process id"),
    // [GNU]
    OPTION("", "--verbose", "print the pid and signal being sent"),
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
  // [GNU] -l conversion prints the signal NAME in both directions
  // (kill.c print_signal_name, kill.c:106-121): a number maps to its
  // name and a name maps back to its canonical name.  Numeric input
  // follows arg_to_signum with maskbit=1 (kill.c:176-191): a value at
  // or above NSIG with bit 128 set is a wait status and loses the 128,
  // so "kill -l 129" prints HUP.
  if (!token.empty() && token.front() >= '0' && token.front() <= '9') {
    auto number = parse_decimal(token);
    if (!number) return std::nullopt;
    int value = *number;
    if (value >= kMaxSignal + 1 && (value & 128) != 0) value -= 128;
    if (value < 0 || value > kMaxSignal) return std::nullopt;
    return signal_number_to_name(value);
  }

  auto signal = get_signal_by_name(token);
  if (!signal || *signal < 0 || *signal > kMaxSignal) {
    return std::nullopt;
  }
  return signal_number_to_name(*signal);
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
    // [GNU] a failed -l conversion is "unknown signal: %s" (kill.c:290).
    return std::unexpected("unknown signal: " + std::string(signal_arg));
  }
  safePrint(*converted);
  safePrint("\n");
  return true;
}

// [GNU] err_nosig (kill.c:169-174): "unknown signal %s; valid signals:"
// followed by the signal list.  The list payload uses this command's own
// plain listing shape (see list_signals).
auto valid_signals_text() -> std::string {
  std::string out;
  int chars = 0;
  for (const auto& sig : kill_constants::SIGNALS) {
    out.append(sig.name);
    out.push_back(' ');
    chars += static_cast<int>(sig.name.size()) + 1;
    if (chars >= 72) {
      out.push_back('\n');
      chars = 0;
    }
  }
  out += "RT<N> RTMIN+<N> RTMAX-<N>";
  return out;
}

auto unknown_signal_error(std::string_view name) -> std::string {
  return "unknown signal " + std::string(name) + "; valid signals:\n" +
         valid_signals_text();
}

// ----------------------------------------------
// 2. Parse signal argument
// ----------------------------------------------
auto parse_signal(const std::string& signal_arg) -> cp::Result<int> {
  // [GNU] a bad -s/-n signal specification goes through err_nosig
  // (kill.c:169-174, called from the -s parse at kill.c:329).
  if (auto signal_num = kill_constants::parse_decimal(signal_arg)) {
    if (*signal_num < 0 || *signal_num > kill_constants::kMaxSignal) {
      return std::unexpected(unknown_signal_error(signal_arg));
    }
    return *signal_num;
  }

  auto signal_num = kill_constants::get_signal_by_name(signal_arg);
  if (!signal_num) {
    return std::unexpected(unknown_signal_error(signal_arg));
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

// ----------------------------------------------
// 3. Terminate process
// ----------------------------------------------
auto terminate_process(DWORD pid, int signal) -> cp::Result<bool> {
  // Open process with necessary access rights
  DWORD access_rights =
      signal == 0
          ? PROCESS_QUERY_LIMITED_INFORMATION
          : (PROCESS_TERMINATE | PROCESS_QUERY_INFORMATION | SYNCHRONIZE);
  HANDLE hProcess = OpenProcess(access_rights, FALSE, pid);

  if (hProcess == NULL) {
    DWORD error = GetLastError();
    // [GNU] failure tails use the strerror wording: the warn shape is
    // "sending signal to <arg> failed: <strerror>" (kill.c:453), so a
    // missing process reads ESRCH and a protected one EPERM.
    if (error == ERROR_ACCESS_DENIED) {
      return std::unexpected("Operation not permitted");
    } else if (error == ERROR_INVALID_PARAMETER) {
      return std::unexpected("No such process");
    } else {
      return std::unexpected("cannot open process");
    }
  }

  // Check if process is still running
  DWORD exitCode;
  if (GetExitCodeProcess(hProcess, &exitCode)) {
    if (exitCode != STILL_ACTIVE) {
      CloseHandle(hProcess);
      return std::unexpected("No such process");
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

  return true;
}

// ----------------------------------------------
// 4. Parse PID list
// ----------------------------------------------
struct KillTarget {
  DWORD pid;
  std::string operand;  // original operand text, for diagnostics
};

struct ResolvedTargets {
  std::vector<KillTarget> targets;
  // [GNU] the oracle counts attempted and failed operands (kill.c ct /
  // nerrs) to derive its 0 / 1 / 64 exit status; a name operand that
  // matches nothing is counted in both before any process is signaled.
  int attempted = 0;
  int failed = 0;
};

auto token_user_sid_bytes(HANDLE token) -> std::vector<unsigned char> {
  DWORD size = 0;
  GetTokenInformation(token, TokenUser, nullptr, 0, &size);
  if (size == 0) return {};
  std::vector<unsigned char> buffer(size);
  if (!GetTokenInformation(token, TokenUser, buffer.data(), size, &size)) {
    return {};
  }
  return buffer;
}

// [GNU] name operands match only the caller's own processes unless -a
// was given (kill.c:510-541 compares the /proc entry owner's uid).
auto process_owned_by_current_user(DWORD pid) -> bool {
  static const std::vector<unsigned char> current_sid = [] {
    std::vector<unsigned char> sid;
    HANDLE token = nullptr;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
      sid = token_user_sid_bytes(token);
      CloseHandle(token);
    }
    return sid;
  }();
  if (current_sid.empty()) return false;

  HANDLE proc = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
  if (proc == nullptr) return false;
  bool owned = false;
  HANDLE token = nullptr;
  if (OpenProcessToken(proc, TOKEN_QUERY, &token)) {
    auto sid = token_user_sid_bytes(token);
    if (!sid.empty()) {
      const auto* mine =
          reinterpret_cast<const TOKEN_USER*>(current_sid.data());
      const auto* theirs = reinterpret_cast<const TOKEN_USER*>(sid.data());
      owned = EqualSid(mine->User.Sid, theirs->User.Sid) != FALSE;
    }
    CloseHandle(token);
  }
  CloseHandle(proc);
  return owned;
}

auto process_name_matches(const std::wstring& exe_name,
                          const std::wstring& wanted, bool exact) -> bool {
  if (_wcsicmp(exe_name.c_str(), wanted.c_str()) == 0) return true;
  if (exact) return false;
  // Windows image names carry the extension; the oracle matches the
  // process name itself, so accept the bare name for "name.exe" too.
  return _wcsicmp(exe_name.c_str(), (wanted + L".exe").c_str()) == 0;
}

auto resolve_targets(const std::vector<std::string>& pid_args, bool all_users,
                     bool exact) -> ResolvedTargets {
  ResolvedTargets resolved;
  std::optional<std::vector<Win32ProcessInfo>> processes;

  for (const auto& arg : pid_args) {
    uint64_t pid_value = 0;
    auto [ptr, ec] = std::from_chars(arg.data(), arg.data() + arg.size(),
                                     pid_value);
    if (ec == std::errc() && ptr == arg.data() + arg.size() &&
        pid_value <= UINT32_MAX) {
      resolved.targets.push_back(
          KillTarget{static_cast<DWORD>(pid_value), arg});
      continue;
    }

    // [GNU] a non-numeric operand is a process name (kill.c:510-541):
    // every process with a matching image name becomes a target.
    if (!processes) processes = enumerate_win32_processes(false);
    const std::wstring wanted = utf8_to_wstring(arg);
    bool found = false;
    for (const auto& proc : *processes) {
      if (!process_name_matches(proc.name, wanted, exact)) continue;
      if (!all_users && !process_owned_by_current_user(proc.pid)) continue;
      resolved.targets.push_back(KillTarget{proc.pid, arg});
      found = true;
    }
    if (!found) {
      // [GNU] warnx shape (kill.c:540).
      safeErrorPrintLn("kill: cannot find process \"" + arg + "\"");
      ++resolved.attempted;
      ++resolved.failed;
    }
  }

  return resolved;
}

// ----------------------------------------------
// 5. Process command
// ----------------------------------------------
template <size_t N>
auto process_command(const CommandContext<N>& ctx) -> cp::Result<int> {
  bool list = ctx.has("--list") || ctx.has("-l");
  bool table = ctx.get<bool>("--table", false);
  table |= ctx.get<bool>("-L", false);
  table |= ctx.get<bool>("-t", false);

  // Handle list/table options
  if (list || table) {
    if (list && !table) {
      // [GNU] -l takes at most one conversion argument (kill.c:283-292):
      // it arrives as the option value ("--list=129", "-l=129"), as the
      // single remaining operand ("kill -l 129"), or as a dash spec
      // ("kill -l -9", which arg_to_signum rejects as a name).
      std::vector<std::string> candidates;
      for (const auto& occurrence : ctx.string_occurrences({"-l", "--list"})) {
        if (!occurrence.value.empty()) candidates.push_back(occurrence.value);
      }
      if (candidates.empty()) {
        for (auto arg : ctx.positionals) candidates.emplace_back(arg);
      }
      if (ctx.count({"-NUM"}) > 0) {
        candidates.push_back("-" + std::to_string(ctx.get<int>("-NUM", 0)));
      }
      if (candidates.size() > 1) {
        return std::unexpected("too many arguments");
      }
      if (candidates.size() == 1) {
        auto converted = convert_and_print_signal(candidates.front());
        if (!converted) return std::unexpected(converted.error());
        return 0;
      }
    }
    auto listed = list_signals(table);
    if (!listed) return std::unexpected(listed.error());
    return 0;
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
        // [GNU] an unknown dash spec (kill.c:377-378).
        return std::unexpected("invalid signal name or number: " + spec);
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

  // [GNU] options but no operands is an error (kill.c:384).
  if (pid_args.empty()) {
    return std::unexpected("not enough arguments");
  }

  const bool print_pid = ctx.has("-p") || ctx.has("--pid");
  const bool verbose = ctx.has("--verbose");
  // [GNU] --pid and --signal are mutually exclusive (kill.c:314,326,381).
  if (print_pid && signal_explicit) {
    return std::unexpected("--pid and --signal are mutually exclusive");
  }

  auto resolved = resolve_targets(pid_args, ctx.has("-a") || ctx.has("--all"),
                                  ctx.has("-x") || ctx.has("--exact"));
  int attempted = resolved.attempted;
  int failed = resolved.failed;

  // Terminate each process
  for (const auto& target : resolved.targets) {
    ++attempted;
    // [GNU] kill_verbose reports before signaling (kill.c:435-439).
    if (verbose) {
      safePrintLn("sending signal " + std::to_string(signal) + " to pid " +
                  std::to_string(target.pid));
    }
    if (print_pid) {
      safePrintLn(std::to_string(target.pid));
      continue;
    }
    auto result = terminate_process(target.pid, signal);
    if (!result) {
      // [GNU] warn shape: "sending signal to <arg> failed" (kill.c:453).
      safeErrorPrintLn("kill: sending signal to " + target.operand +
                       " failed: " + result.error());
      ++failed;
    }
  }

  // [GNU] exit status (kill.c:553-558): 0 when every operand succeeded,
  // 1 when every operand failed, KILL_EXIT_SOMEOK (64) for a partial
  // success.
  if (failed == 0) return 0;
  if (failed == attempted) return 1;
  return 64;
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
    "except SIGTERM and SIGINT, which attempt graceful shutdown first.",

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

  // [GNU] process_command already derives the oracle exit status
  // (0 / 1 / KILL_EXIT_SOMEOK 64, kill.c:553-558).
  return *result;
}
