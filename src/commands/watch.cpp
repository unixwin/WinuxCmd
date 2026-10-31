// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for watch, following procps 4.0.4
///     src/watch.c: fractional -n (WATCH_INTERVAL aware, clamped to
///     0.1s), -c is --color (the historical WinuxCmd count extension
///     moved to the long-only --count), upstream exit semantics (0 on
///     normal termination, the command's status under -e, 2 for watch
///     management errors), and the "Every %.1fs:" header with the
///     right-aligned "hostname: ctime" field.
/// @Version: 0.2.0
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

auto constexpr WATCH_OPTIONS = std::array{
    OPTION("-b", "--beep", "beep if command has a non-zero exit"),
    OPTION("-c", "--color", "interpret ANSI color sequences"),
    OPTION("-C", "--no-color", "do not print ANSI color escape sequences"),
    OPTION("-d", "--differences", "highlight changes between updates",
           OPTIONAL_STRING_TYPE),
    OPTION("-e", "--errexit", "exit if command has a non-zero exit"),
    OPTION("-g", "--chgexit", "exit when the output of command changes"),
    OPTION("-n", "--interval", "seconds to wait between updates", STRING_TYPE),
    OPTION("-p", "--precise", "run command at precise intervals"),
    OPTION("-q", "--equexit",
           "exit when the output does not change for the given number "
           "of cycles",
           STRING_TYPE),
    OPTION("-r", "--no-rerun", "do not rerun the command on a terminal resize"),
    OPTION("-t", "--no-title", "turn off header showing interval and command"),
    OPTION("-w", "--no-wrap", "turn off line wrapping"),
    OPTION("-x", "--exec",
           "execute the command directly instead of via the shell"),
    // WinuxCmd extension (upstream -c is --color): finite run count.
    OPTION("", "--count", "number of times to run the command", INT_TYPE)};

namespace watch_pipeline {
namespace cp = core::pipeline;

struct Config {
  double interval = 2.0;
  bool differences = false;
  bool differences_permanent = false;
  bool no_title = false;
  bool beep = false;
  bool color = false;
  bool no_color = false;
  bool errexit = false;
  bool chgexit = false;
  bool precise = false;
  bool exec = false;
  bool no_wrap = false;
  int equexit = 0;  // cycles; 0 = disabled
  int count = 0;    // 0 = run forever (--count extension)
  std::vector<std::string> argv;
  std::string command_display;
};

// procps watch.c accepts both '.' and ',' as the decimal separator
// and clamps the interval to a 0.1s minimum.
auto parse_interval(std::string_view text, double& out) -> bool {
  std::string cleaned(text);
  std::ranges::replace(cleaned, ',', '.');
  if (cleaned.empty()) return false;
  char* end = nullptr;
  const double value = std::strtod(cleaned.c_str(), &end);
  if (end == cleaned.c_str() || *end != '\0') return false;
  out = value < 0.1 ? 0.1 : value;
  return true;
}

auto build_config(const CommandContext<WATCH_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  // WATCH_INTERVAL is read first; -n overrides it (watch.c).
  if (const char* env = std::getenv("WATCH_INTERVAL")) {
    double parsed = 0;
    if (parse_interval(env, parsed)) cfg.interval = parsed;
  }
  if (ctx.has("-n") || ctx.has("--interval")) {
    const auto text = ctx.get<std::string>("--interval", "");
    if (!parse_interval(text, cfg.interval)) {
      return std::unexpected("invalid interval: '" + text + "'");
    }
  }

  // -d is OPTIONAL_STRING: presence is the signal (a bool read would
  // never see the string-typed occurrence).
  cfg.differences = ctx.has("-d") || ctx.has("--differences");
  if (cfg.differences) {
    // -d with any attached value selects permanent (cumulative)
    // highlighting in watch.c.
    cfg.differences_permanent =
        !ctx.get<std::string>("--differences", "").empty();
  }
  cfg.no_title =
      ctx.get<bool>("-t", false) || ctx.get<bool>("--no-title", false);
  cfg.beep = ctx.get<bool>("-b", false) || ctx.get<bool>("--beep", false);
  cfg.color = ctx.get<bool>("-c", false) || ctx.get<bool>("--color", false);
  cfg.no_color =
      ctx.get<bool>("-C", false) || ctx.get<bool>("--no-color", false);
  cfg.errexit = ctx.get<bool>("-e", false) || ctx.get<bool>("--errexit", false);
  cfg.chgexit = ctx.get<bool>("-g", false) || ctx.get<bool>("--chgexit", false);
  cfg.precise = ctx.get<bool>("-p", false) || ctx.get<bool>("--precise", false);
  cfg.exec = ctx.get<bool>("-x", false) || ctx.get<bool>("--exec", false);
  cfg.no_wrap = ctx.get<bool>("-w", false) || ctx.get<bool>("--no-wrap", false);
  // -r/--no-rerun needs no state: this watch never reruns the command
  // on a terminal resize in the first place.
  if (ctx.has("-q") || ctx.has("--equexit")) {
    const auto text = ctx.get<std::string>("--equexit", "");
    int cycles = 0;
    const auto [ptr, ec] =
        std::from_chars(text.data(), text.data() + text.size(), cycles);
    if (ec != std::errc() || ptr != text.data() + text.size() || cycles < 1) {
      return std::unexpected("invalid equexit cycle count: '" + text + "'");
    }
    cfg.equexit = cycles;
  }
  cfg.count = ctx.get<int>("--count", 0);
  if (cfg.count < 0) {
    return std::unexpected("count cannot be negative");
  }

  if (ctx.positionals.empty()) {
    return std::unexpected("missing command to watch");
  }
  for (auto arg : ctx.positionals) cfg.argv.emplace_back(arg);
  for (size_t i = 0; i < cfg.argv.size(); ++i) {
    if (i > 0) cfg.command_display += " ";
    cfg.command_display += cfg.argv[i];
  }
  return cfg;
}

auto console_width() -> size_t {
  CONSOLE_SCREEN_BUFFER_INFO info{};
  HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
  if (out != nullptr && out != INVALID_HANDLE_VALUE &&
      GetConsoleScreenBufferInfo(out, &info)) {
    const int width = info.srWindow.Right - info.srWindow.Left + 1;
    if (width > 0) return static_cast<size_t>(width);
  }
  return 80;
}

auto clear_screen() -> void {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (hConsole == nullptr || hConsole == INVALID_HANDLE_VALUE ||
      !GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    return;  // not a console: nothing to clear (no shell fallback)
  }
  const COORD coordScreen = {0, 0};
  const DWORD conSize = static_cast<DWORD>(csbi.dwSize.X) * csbi.dwSize.Y;
  DWORD written = 0;
  FillConsoleOutputCharacter(hConsole, ' ', conSize, coordScreen, &written);
  FillConsoleOutputAttribute(hConsole, csbi.wAttributes, conSize, coordScreen,
                             &written);
  SetConsoleCursorPosition(hConsole, coordScreen);
}

auto hostname_text() -> std::string {
  wchar_t buffer[MAX_COMPUTERNAME_LENGTH + 1]{};
  DWORD size = static_cast<DWORD>(std::size(buffer));
  if (!GetComputerNameW(buffer, &size)) return "localhost";
  return wstring_to_utf8(std::wstring(buffer, size));
}

// ctime(3) shape without the trailing newline: "Sat Oct 31 20:15:00 2026"
auto ctime_text() -> std::string {
  static constexpr const char* kDays[] = {"Sun", "Mon", "Tue", "Wed",
                                          "Thu", "Fri", "Sat"};
  static constexpr const char* kMonths[] = {"Jan", "Feb", "Mar", "Apr",
                                            "May", "Jun", "Jul", "Aug",
                                            "Sep", "Oct", "Nov", "Dec"};
  std::time_t now = std::time(nullptr);
  std::tm local{};
  localtime_s(&local, &now);
  char buffer[64];
  sprintf_s(buffer, "%s %s %2d %02d:%02d:%02d %d", kDays[local.tm_wday % 7],
            kMonths[local.tm_mon % 12], local.tm_mday, local.tm_hour,
            local.tm_min, local.tm_sec, local.tm_year + 1900);
  return buffer;
}

// watch.c output_header(): "Every %.1fs: <command>" on the left and
// "<hostname>: <ctime>" right-aligned to the screen width.
auto header_text(const Config& cfg) -> std::string {
  char interval[32];
  sprintf_s(interval, "%.1f", cfg.interval);
  const std::string left =
      std::string("Every ") + interval + "s: " + cfg.command_display;
  const std::string right = hostname_text() + ": " + ctime_text();
  const size_t width = console_width();
  std::string line;
  if (left.size() + 1 + right.size() <= width) {
    line = left;
    line.append(width - left.size() - right.size(), ' ');
    line += right;
  } else if (right.size() + 1 < width) {
    line = left.substr(0, width - right.size() - 1);
    line += " ";
    line += right;
  } else {
    line = left.substr(0, width);
  }
  return line;
}

/// Split a string into lines (preserving content, stripping trailing newline).
auto split_lines(const std::string& text) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

/// Produce output with changed lines highlighted using ANSI escape codes.
/// (GNU watch -d works per character on an ncurses screen; the line is
/// the established WinuxCmd granularity.)
auto highlight_differences(const std::string& current,
                           const std::string& previous) -> std::string {
  constexpr const char* HIGHLIGHT_START = "\033[1;4m";
  constexpr const char* HIGHLIGHT_END = "\033[0m";

  auto cur_lines = split_lines(current);
  auto prev_lines = split_lines(previous);

  std::string result;
  size_t max_count = std::max(cur_lines.size(), prev_lines.size());
  result.reserve(current.size() + max_count * 20);

  for (size_t i = 0; i < cur_lines.size(); ++i) {
    bool changed = (i >= prev_lines.size()) || (cur_lines[i] != prev_lines[i]);
    if (changed) {
      result += HIGHLIGHT_START;
      result += cur_lines[i];
      result += HIGHLIGHT_END;
    } else {
      result += cur_lines[i];
    }
    if (i + 1 < cur_lines.size()) {
      result += '\n';
    }
  }
  return result;
}

// Remove ANSI CSI sequences (watch --no-color approximation: upstream
// renders through ncurses without interpreting color; here the escape
// sequences are dropped from the command output).
auto strip_ansi(const std::string& text) -> std::string {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size();) {
    if (text[i] == '\x1b' && i + 1 < text.size() && text[i + 1] == '[') {
      size_t j = i + 2;
      while (j < text.size() && static_cast<unsigned char>(text[j]) >= 0x20 &&
             static_cast<unsigned char>(text[j]) <= 0x3f) {
        ++j;
      }
      if (j < text.size() && static_cast<unsigned char>(text[j]) >= 0x40 &&
          static_cast<unsigned char>(text[j]) <= 0x7e) {
        ++j;
      }
      i = j;
      continue;
    }
    if (text[i] == '\x1b' && i + 1 < text.size()) {
      i += 2;
      continue;
    }
    out += text[i];
    ++i;
  }
  return out;
}

// Truncate each line to the console width (watch -w/--no-wrap),
// backing off to a UTF-8 boundary so multibyte text is not split.
auto truncate_lines(const std::string& text, size_t width) -> std::string {
  std::string out;
  size_t pos = 0;
  while (pos < text.size()) {
    size_t eol = text.find('\n', pos);
    if (eol == std::string::npos) eol = text.size();
    size_t take = std::min(eol - pos, width);
    while (take > 0 && take < eol - pos &&
           (static_cast<unsigned char>(text[pos + take]) & 0xC0) == 0x80) {
      --take;
    }
    out.append(text, pos, take);
    if (eol < text.size()) out += '\n';
    pos = eol + 1;
  }
  return out;
}

struct RunOnce {
  std::string output;
  int status = 0;
  bool management_error = false;
};

auto run_via_shell(const Config& cfg) -> RunOnce {
  // The command string is handed to the command interpreter; rewriting
  // the first word to its PATH-resolved absolute form restores GNU
  // execvp resolution (see resolve_shell_command_first_word).
  const std::string resolved_command =
      winux::childproc::resolve_shell_command_first_word(cfg.command_display);
  std::string output;
  std::array<char, 128> buffer;
  std::unique_ptr<FILE, decltype(&_pclose)> pipe(
      _popen(resolved_command.c_str(), "r"), _pclose);
  if (!pipe) return RunOnce{"", 0, true};
  while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
    output += buffer.data();
  }
  const int status = _pclose(pipe.release());
  return RunOnce{output, status < 0 ? 1 : status, false};
}

auto run_via_exec(const Config& cfg) -> RunOnce {
  winux::childproc::ChildLaunchRequest request;
  request.command = cfg.argv.front();
  request.args.assign(cfg.argv.begin() + 1, cfg.argv.end());
  auto plan = winux::childproc::plan_child_launch(request);
  if (plan.status != winux::childproc::ChildResolveStatus::Found) {
    const auto error =
        winux::childproc::child_status_to_win32_error(plan.status);
    return RunOnce{"", winux::childproc::child_spawn_exit_status(error), false};
  }

  SECURITY_ATTRIBUTES sa{};
  sa.nLength = sizeof(sa);
  sa.bInheritHandle = TRUE;
  HANDLE read_end = nullptr;
  HANDLE write_end = nullptr;
  if (!CreatePipe(&read_end, &write_end, &sa, 0)) {
    return RunOnce{"", 0, true};
  }
  SetHandleInformation(read_end, HANDLE_FLAG_INHERIT, 0);

  STARTUPINFOW si{};
  si.cb = sizeof(si);
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = write_end;
  si.hStdError = write_end;
  si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
  PROCESS_INFORMATION pi{};
  std::wstring command_line = plan.command_line;
  const wchar_t* app =
      plan.application_name ? plan.application_name->c_str() : nullptr;
  const BOOL created =
      CreateProcessW(app, command_line.data(), nullptr, nullptr, TRUE, 0,
                     nullptr, nullptr, &si, &pi);
  CloseHandle(write_end);
  if (!created) {
    const DWORD error = GetLastError();
    CloseHandle(read_end);
    return RunOnce{"", winux::childproc::child_spawn_exit_status(error), false};
  }
  std::string output;
  std::array<char, 4096> buffer;
  DWORD read = 0;
  while (ReadFile(read_end, buffer.data(), static_cast<DWORD>(buffer.size()),
                  &read, nullptr) &&
         read > 0) {
    output.append(buffer.data(), read);
  }
  CloseHandle(read_end);
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD exit_code = 0;
  GetExitCodeProcess(pi.hProcess, &exit_code);
  CloseHandle(pi.hThread);
  CloseHandle(pi.hProcess);
  return RunOnce{output, static_cast<int>(exit_code), false};
}

auto run(const Config& cfg) -> int {
  if (cfg.color) {
    HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (out != nullptr && out != INVALID_HANDLE_VALUE &&
        GetConsoleMode(out, &mode)) {
      SetConsoleMode(out, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    }
  }

  std::string last_output;
  std::string diff_baseline;
  bool have_baseline = false;
  int unchanged_streak = 0;
  int iteration = 0;

  while (true) {
    const auto started = std::chrono::steady_clock::now();
    RunOnce once = cfg.exec ? run_via_exec(cfg) : run_via_shell(cfg);
    ++iteration;
    if (once.management_error) return 2;

    std::string output = once.output;
    if (cfg.no_color) output = strip_ansi(output);
    if (cfg.no_wrap) output = truncate_lines(output, console_width());

    clear_screen();
    if (!cfg.no_title) {
      safePrintLn(header_text(cfg));
      safePrintLn("");
    }
    if (cfg.differences && have_baseline) {
      safePrint(highlight_differences(output, diff_baseline));
    } else {
      safePrint(output);
    }
    if (cfg.beep && once.status != 0) {
      safePrint("\a");
    }

    // Exit-condition bookkeeping compares the raw command output.
    const bool changed = have_baseline && once.output != last_output;
    const bool unchanged = have_baseline && once.output == last_output;
    if (cfg.errexit && once.status != 0) return once.status;
    if (cfg.chgexit && changed) return 0;
    if (cfg.equexit > 0) {
      if (unchanged) {
        if (++unchanged_streak >= cfg.equexit) return 0;
      } else {
        unchanged_streak = 0;
      }
    }

    last_output = once.output;
    if (!have_baseline || !cfg.differences_permanent) {
      diff_baseline = once.output;
    }
    have_baseline = true;

    if (cfg.count > 0 && iteration >= cfg.count) return 0;

    double wait = cfg.interval;
    if (cfg.precise) {
      const double elapsed = std::chrono::duration<double>(
                                 std::chrono::steady_clock::now() - started)
                                 .count();
      wait = std::max(0.0, cfg.interval - elapsed);
    }
    Sleep(static_cast<DWORD>(wait * 1000.0 + 0.5));
  }
}

}  // namespace watch_pipeline

REGISTER_COMMAND(
    watch, "watch", "watch [OPTION]... COMMAND",
    "Execute a program periodically, showing output fullscreen.\n"
    "\n"
    "Runs COMMAND repeatedly, displaying its output and errors.\n"
    "This allows you to watch the program output change over time.",
    "  watch -n 1 'ls -l'\n"
    "  watch -d ls -l\n"
    "  watch -n 5 date",
    "ps(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", WATCH_OPTIONS) {
  using namespace watch_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"watch");
    return 1;
  }

  return run(*cfg_result);
}
