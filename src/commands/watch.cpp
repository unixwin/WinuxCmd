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
 *  - File: watch.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
    OPTION("-n", "--interval", "seconds to wait between updates", INT_TYPE),
    OPTION("-d", "--differences", "highlight changes between updates",
           BOOL_TYPE),
    OPTION("-t", "--no-title", "turn off header showing interval and command",
           BOOL_TYPE),
    OPTION("-b", "--beep", "beep if command has a non-zero exit", BOOL_TYPE),
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

auto run(const Config& cfg) -> int {
  std::string previous_output;
  int iteration = 0;

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

    // Print output
    safePrint(output);

    // Beep on non-zero exit if enabled
    if (cfg.beep && exit_code != 0) {
      safePrint("\a");  // ASCII bell
    }

    // Wait for interval
    Sleep(cfg.interval * 1000);
  }

  return 0;
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
