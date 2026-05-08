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
 *  - File: less.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for less.
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

auto constexpr LESS_OPTIONS = std::array{
    OPTION("-e", "--quit-at-eof", "quit after second EOF", BOOL_TYPE),
    OPTION("-E", "--QUIT-AT-EOF", "quit after first EOF", BOOL_TYPE),
    OPTION("-F", "--quit-if-one-screen",
           "quit if entire file fits on first screen", BOOL_TYPE),
    OPTION("-i", "--ignore-case",
           "ignore case in searches that do not contain uppercase", BOOL_TYPE),
    OPTION("-I", "--IGNORE-CASE", "ignore case in all searches", BOOL_TYPE),
    OPTION("-n", "--line-numbers", "display line number at start of each line",
           BOOL_TYPE),
    OPTION("-N", "--LINE-NUMBERS", "display line number at start of each line",
           BOOL_TYPE),
    OPTION("-S", "--chop-long-lines", "chop long lines (do not wrap)",
           BOOL_TYPE),
    OPTION("-q", "--quiet", "silence the terminal bell", BOOL_TYPE),
    OPTION("-Q", "--QUIET", "never ring the terminal bell", BOOL_TYPE),
    OPTION("-r", "--raw-control-chars", "display \"raw\" control characters",
           BOOL_TYPE),
    OPTION("-R", "--RAW-CONTROL-CHARS",
           "display \"raw\" control characters and ANSI colors", BOOL_TYPE)};

namespace less_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool quit_at_eof = false;        // -e
  bool quit_first_eof = false;     // -E
  bool quit_one_screen = false;    // -F
  bool ignore_case = false;        // -i
  bool ignore_case_all = false;    // -I
  bool show_line_numbers = false;  // -n, -N
  bool chop_long_lines = false;    // -S
  bool quiet = false;              // -q
  bool never_bell = false;         // -Q
  bool raw_control = false;        // -r
  bool raw_control_color = false;  // -R
  SmallVector<std::string, 16> files;
};

auto build_config(const CommandContext<LESS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.quit_at_eof =
      ctx.get<bool>("--quit-at-eof", false) || ctx.get<bool>("-e", false);
  cfg.quit_first_eof =
      ctx.get<bool>("--QUIT-AT-EOF", false) || ctx.get<bool>("-E", false);
  cfg.quit_one_screen = ctx.get<bool>("--quit-if-one-screen", false) ||
                        ctx.get<bool>("-F", false);
  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-i", false);
  cfg.ignore_case_all =
      ctx.get<bool>("--IGNORE-CASE", false) || ctx.get<bool>("-I", false);
  cfg.show_line_numbers =
      ctx.get<bool>("--line-numbers", false) || ctx.get<bool>("-n", false) ||
      ctx.get<bool>("--LINE-NUMBERS", false) || ctx.get<bool>("-N", false);
  cfg.chop_long_lines =
      ctx.get<bool>("--chop-long-lines", false) || ctx.get<bool>("-S", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-q", false);
  cfg.never_bell =
      ctx.get<bool>("--QUIET", false) || ctx.get<bool>("-Q", false);
  cfg.raw_control =
      ctx.get<bool>("--raw-control-chars", false) || ctx.get<bool>("-r", false);
  cfg.raw_control_color =
      ctx.get<bool>("--RAW-CONTROL-CHARS", false) || ctx.get<bool>("-R", false);

  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    cfg.files.push_back(file_arg);
  }

  if (cfg.files.empty()) {
    cfg.files.push_back("-");
  }

  return cfg;
}

auto read_file_content(const std::string& filename) -> cp::Result<std::string> {
  std::string content;

  if (filename == "-" || filename.empty()) {
    // Read from stdin
    content.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      return std::unexpected("error reading from standard input");
    }
  } else {
    // Read from file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }
    content.assign(std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>());
    if (file.fail() && !file.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  return content;
}

// Simple pager implementation - displays content page by page
auto simple_pager(const Config& cfg, const std::string& content) -> int {
  if (!isOutputConsole()) {
    // Not a terminal, just output everything
    safePrint(content);
    return 0;
  }

  // Split into lines
  SmallVector<std::string, 4096> lines;
  size_t start = 0;
  while (start < content.size()) {
    size_t end = content.find('\n', start);
    if (end == std::string::npos) {
      lines.push_back(content.substr(start));
      break;
    }
    lines.push_back(content.substr(start, end - start));
    start = end + 1;
  }

  // Get terminal size
  int term_height = getTerminalWidth();
  CONSOLE_SCREEN_BUFFER_INFO csbi;
  HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
  if (GetConsoleScreenBufferInfo(hConsole, &csbi)) {
    term_height = csbi.srWindow.Bottom - csbi.srWindow.Top + 1;
  }

  int page_size = term_height - 1;  // Leave one line for prompt

  // Check if content fits on one screen
  if (cfg.quit_one_screen && lines.size() <= static_cast<size_t>(page_size)) {
    for (const auto& line : lines) {
      safePrintLn(line);
    }
    return 0;
  }

  // Display pages
  size_t current_line = 0;
  int eof_count = 0;

  while (true) {
    // Clear screen and display current page
    // Note: This is a simplified implementation
    for (int i = 0; i < page_size && current_line < lines.size(); ++i) {
      if (cfg.show_line_numbers) {
        safePrint(current_line + 1);
        safePrint(": ");
      }
      safePrintLn(lines[current_line]);
      current_line++;
    }

    // Check if at end of file
    if (current_line >= lines.size()) {
      eof_count++;
      if (cfg.quit_first_eof || (cfg.quit_at_eof && eof_count >= 2)) {
        return 0;
      }
    }

    // Simple prompt (non-interactive for now)
    safePrintLn("--Press Enter to continue, q to quit--");

    // For simplicity, just read one character
    // A full implementation would use proper console input handling
    char c = '\0';
    if (std::cin.get(c)) {
      if (c == 'q' || c == 'Q') {
        return 0;
      }
      // Any other key continues
    } else {
      break;
    }

    // Reset eof count if we scroll back up (simplified)
    if (current_line < lines.size()) {
      eof_count = 0;
    }
  }

  return 0;
}

auto run(const Config& cfg) -> int {
  for (const auto& file : cfg.files) {
    auto content_result = read_file_content(file);
    if (!content_result) {
      cp::report_error(content_result, L"less");
      return 1;
    }

    int result = simple_pager(cfg, *content_result);
    if (result != 0) {
      return result;
    }
  }

  return 0;
}

}  // namespace less_pipeline

REGISTER_COMMAND(
    less, "less", "less [OPTION]... [FILE]...",
    "A pager for viewing files, similar to more but with more features.\n"
    "Allows backward movement in the file as well as forward movement.\n"
    "\n"
    "This is a simplified implementation of less with core features.",
    "  less file.txt\n"
    "  less -N file.txt          # Show line numbers\n"
    "  less -E file.txt          # Quit at end of file\n"
    "  less -F file.txt          # Quit if fits on one screen",
    "more(1), most(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", LESS_OPTIONS) {
  using namespace less_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"less");
    return 1;
  }

  return run(*cfg_result);
}
