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
 *  - File: more.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for more.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr MORE_OPTIONS = std::array{
    OPTION("-d", "", "display help instead of ring bell"),
    OPTION("-f", "", "count logical lines, not screen lines"),
    OPTION("-l", "", "pause after form feeds"),
    OPTION("-c", "", "clear screen before each page"),
    OPTION("-s", "", "squeeze multiple blank lines"),
    OPTION("-n", "", "number of lines per screenful", STRING_TYPE),
    OPTION("-p", "", "display file from top of screen", STRING_TYPE)};

namespace more_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool help_prompt = false;
  bool logical_lines = false;
  bool pause_form_feed = false;
  bool clear_screen = false;
  bool squeeze_blank = false;
  size_t lines_per_page = 24;
  std::string search_pattern;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<MORE_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.help_prompt = ctx.get<bool>("-d", false);
  cfg.logical_lines = ctx.get<bool>("-f", false);
  cfg.pause_form_feed = ctx.get<bool>("-l", false);
  cfg.clear_screen = ctx.get<bool>("-c", false);
  cfg.squeeze_blank = ctx.get<bool>("-s", false);

  auto lines_opt = ctx.get<std::string>("-n", "");
  if (!lines_opt.empty()) {
    try {
      int val = std::stoi(lines_opt);
      if (val < 1) return std::unexpected("invalid line count");
      cfg.lines_per_page = static_cast<size_t>(val);
    } catch (...) {
      return std::unexpected("invalid line count");
    }
  }

  auto pattern_opt = ctx.get<std::string>("-p", "");
  if (!pattern_opt.empty()) {
    cfg.search_pattern = pattern_opt;
  }

  for (const auto& pos : ctx.positionals) {
    cfg.files.push_back(std::string(pos));
  }

  return cfg;
}

auto get_console_height() -> size_t {
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi)) {
    return static_cast<size_t>(csbi.srWindow.Bottom - csbi.srWindow.Top);
  }
  return 24;  // Default
}

auto clear_console() -> void {
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  GetConsoleScreenBufferInfo(hConsole, &csbi);
  DWORD cells = csbi.dwSize.X * csbi.dwSize.Y;
  DWORD written;
  FillConsoleOutputCharacter(hConsole, ' ', cells, {0, 0}, &written);
  FillConsoleOutputAttribute(hConsole, csbi.wAttributes, cells, {0, 0},
                             &written);
  SetConsoleCursorPosition(hConsole, {0, 0});
}

auto display_file(const std::string& filename, const Config& cfg) -> int {
  std::vector<std::string> lines;

  if (filename.empty() || filename == "-") {
    std::string line;
    while (std::getline(std::cin, line)) {
      lines.push_back(line);
    }
  } else {
    std::ifstream file(filename);
    if (!file) {
      safeErrorPrint("more: ");
      safeErrorPrint(filename);
      safeErrorPrintLn(": No such file or directory");
      return 1;
    }
    std::string line;
    while (std::getline(file, line)) {
      lines.push_back(line);
    }
  }

  // Squeeze blank lines if requested
  if (cfg.squeeze_blank) {
    std::vector<std::string> squeezed;
    bool prev_blank = false;
    for (const auto& line : lines) {
      bool is_blank = line.empty();
      if (!is_blank || !prev_blank) {
        squeezed.push_back(line);
      }
      prev_blank = is_blank;
    }
    lines = std::move(squeezed);
  }

  // Get terminal height
  size_t page_height = cfg.lines_per_page;
  if (page_height == 0) {
    page_height = get_console_height();
  }

  // Find start line if pattern specified
  size_t start_line = 0;
  if (!cfg.search_pattern.empty()) {
    for (size_t i = 0; i < lines.size(); ++i) {
      if (lines[i].find(cfg.search_pattern) != std::string::npos) {
        start_line = i;
        break;
      }
    }
  }

  // Display pages
  size_t current_line = start_line;
  bool done = false;

  while (!done && current_line < lines.size()) {
    // Clear screen if requested
    if (cfg.clear_screen) {
      clear_console();
    }

    // Display one page
    size_t lines_shown = 0;
    for (size_t i = 0; i < page_height && current_line < lines.size();
         ++i, ++current_line) {
      safePrintLn(lines[current_line]);
      lines_shown++;
    }

    // Check if we're at the end
    if (current_line >= lines.size()) {
      break;
    }

    // Prompt for more
    if (cfg.help_prompt) {
      safePrint("--More--(%) [Press space to continue, 'q' to quit]");
    } else {
      safePrint("--More--");
    }

    // Wait for input
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode;
    GetConsoleMode(hInput, &mode);
    SetConsoleMode(hInput, mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT));

    INPUT_RECORD record;
    DWORD read;
    while (true) {
      ReadConsoleInput(hInput, &record, 1, &read);
      if (record.EventType == KEY_EVENT && record.Event.KeyEvent.bKeyDown) {
        char ch = record.Event.KeyEvent.uChar.AsciiChar;
        if (ch == 'q' || ch == 'Q') {
          done = true;
          break;
        } else if (ch == ' ' || ch == '\n' || ch == '\r') {
          break;
        }
      }
    }

    SetConsoleMode(hInput, mode);
    safePrintLn("");
  }

  return 0;
}

auto run(const Config& cfg) -> int {
  if (cfg.files.empty()) {
    return display_file("", cfg);
  }

  int result = 0;
  for (const auto& file : cfg.files) {
    if (display_file(file, cfg) != 0) {
      result = 1;
    }
  }
  return result;
}

}  // namespace more_pipeline

REGISTER_COMMAND(
    more, "more",
    "more [OPTION]... [FILE]...",
    "Display the contents of a file, one screenful at a time.\n"
    "\n"
    "The more command is a filter for paging through text one screen at a time.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -d          display help instead of ring bell\n"
    "  -f          count logical lines, not screen lines\n"
    "  -l          pause after form feeds\n"
    "  -c          clear screen before each page\n"
    "  -s          squeeze multiple blank lines\n"
    "  -n NUM      number of lines per screenful\n"
    "  -p PATTERN  display file from top of screen\n"
    "\n"
    "Interactive commands:\n"
    "  SPACE       display next screenful\n"
    "  ENTER       display next line\n"
    "  q           quit\n"
    "\n"
    "Note: On Windows, more uses the console for interactive paging.",
    "  more file.txt         display file one page at a time\n"
    "  more -s file.txt      squeeze blank lines\n"
    "  more -n 40 file.txt   use 40 lines per page\n"
    "  cat file | more       page through piped input",
    "less(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", MORE_OPTIONS) {
  using namespace more_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"more");
    return 1;
  }

  return run(*cfg_result);
}
