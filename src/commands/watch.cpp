// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for watch.
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

auto constexpr WATCH_OPTIONS = std::array{
    // [EXT] option
    OPTION("-n", "--interval", "seconds to wait between updates", INT_TYPE),
    // [EXT] option
    OPTION("-d", "--differences", "highlight changes between updates",
           BOOL_TYPE),
    // [EXT] option
    OPTION("-t", "--no-title", "turn off header showing interval and command",
           BOOL_TYPE),
    // [EXT] option
    OPTION("-b", "--beep", "beep if command has a non-zero exit", BOOL_TYPE),
    // [EXT] option
    OPTION("-c", "--count", "number of times to run the command (for testing)",
           INT_TYPE)};

namespace watch_pipeline {
namespace cp = core::pipeline;

struct Config {
  int interval = 2;
  bool differences = false;
  bool no_title = false;
  bool beep = false;
  int count = 0;  // 0 means infinite loop
  std::string command;
};

auto build_config(const CommandContext<WATCH_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.interval = ctx.get<int>("--interval", 2);
  if (cfg.interval < 0) {
    return std::unexpected("interval cannot be negative");
  }

  cfg.differences =
      ctx.get<bool>("--differences", false) || ctx.get<bool>("-d", false);
  cfg.no_title =
      ctx.get<bool>("--no-title", false) || ctx.get<bool>("-t", false);
  cfg.beep = ctx.get<bool>("--beep", false) || ctx.get<bool>("-b", false);
  cfg.count = ctx.get<int>("--count", 0);  // Default to infinite

  if (ctx.positionals.empty()) {
    return std::unexpected("missing command to watch");
  }

  // Build command string from positionals
  std::string cmd;
  for (size_t i = 0; i < ctx.positionals.size(); ++i) {
    if (i > 0) cmd += " ";
    cmd += ctx.positionals[i];
  }
  cfg.command = cmd;

  return cfg;
}

auto clear_screen() -> void {
  // Use Windows API to clear screen
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  COORD coordScreen = {0, 0};
  DWORD cCharsWritten;
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  DWORD dwConSize;

  if (!GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    // Fallback to system cls
    system("cls");
    return;
  }

  dwConSize = csbi.dwSize.X * csbi.dwSize.Y;
  FillConsoleOutputCharacter(hConsole, (TCHAR)' ', dwConSize, coordScreen,
                             &cCharsWritten);
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  FillConsoleOutputAttribute(hConsole, csbi.wAttributes, dwConSize, coordScreen,
                             &cCharsWritten);
  SetConsoleCursorPosition(hConsole, coordScreen);
}

/// Split a string into lines (preserving content, stripping trailing newline).
auto split_lines(const std::string& text) -> std::vector<std::string> {
  std::vector<std::string> lines;
  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  // If the original text ends with a newline, std::getline already consumed it;
  // if it does not end with a newline the last line is correct as-is.
  return lines;
}

/// Produce output with changed lines highlighted using ANSI escape codes.
/// Changed lines are wrapped in bold-underline (\033[1;4m ... \033[0m),
/// matching the behaviour of GNU watch -d.
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

auto run(const Config& cfg) -> int {
  std::string previous_output;
  int iteration = 0;
  int last_exit_code = 0;

  while (true) {
    // Check count limit
    if (cfg.count > 0 && iteration >= cfg.count) {
      break;
    }
    iteration++;

    // Clear screen
    clear_screen();

    // Print header if enabled
    if (!cfg.no_title) {
      // Get current time using Windows API
      SYSTEMTIME st;
      GetLocalTime(&st);
      char time_buf[64];
      sprintf_s(time_buf, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

      safePrint("Every ");
      safePrint(cfg.interval);
      if (cfg.interval == 1) {
        safePrint("s: ");
      } else {
        safePrint("s: ");
      }
      safePrint(cfg.command);
      safePrint("                ");
      safePrintLn(time_buf);
      safePrintLn("");  // Empty line after header
    }

    // Execute command and capture output
    std::string output;
    std::array<char, 128> buffer;
    std::unique_ptr<FILE, decltype(&_pclose)> pipe(
        _popen(cfg.command.c_str(), "r"), _pclose);

    if (!pipe) {
      cp::report_custom_error(L"watch", L"failed to execute command");
      return 1;
    }

    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
      output += buffer.data();
    }

    int exit_code = _pclose(pipe.release());
    last_exit_code = exit_code < 0 ? 1 : exit_code;

    // Print output, highlighting differences if -d is active
    if (cfg.differences) {
      if (previous_output.empty()) {
        // First iteration – highlight everything as new
        std::istringstream first_stream(output);
        std::string first_line;
        constexpr const char* HL_START = "\033[1;4m";
        constexpr const char* HL_END = "\033[0m";
        bool first = true;
        while (std::getline(first_stream, first_line)) {
          if (!first) {
            safePrint("\n");
          }
          safePrint(HL_START);
          safePrint(first_line);
          safePrint(HL_END);
          first = false;
        }
        safePrint("\n");
      } else {
        safePrint(highlight_differences(output, previous_output));
      }
    } else {
      safePrint(output);
    }

    // Beep on non-zero exit if enabled
    if (cfg.beep && exit_code != 0) {
      safePrint("\a");  // ASCII bell
    }

    // Save current output for next comparison
    if (cfg.differences) {
      previous_output = output;
    }

    // Wait for interval
    Sleep(cfg.interval * 1000);
  }

  return last_exit_code;
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
