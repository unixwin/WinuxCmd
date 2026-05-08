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
 *  - File: expand.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for expand.
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

auto constexpr EXPAND_OPTIONS = std::array{
    OPTION("-t", "--tabs", "specify tab stop positions (default: 8)",
           STRING_TYPE),
    OPTION("-i", "--initial", "only convert tabs at the beginning of lines",
           BOOL_TYPE)};

namespace expand_pipeline {
namespace cp = core::pipeline;

struct Config {
  int tab_width = 8;
  bool initial_only = false;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<EXPAND_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.initial_only =
      ctx.get<bool>("--initial", false) || ctx.get<bool>("-i", false);

  auto tabs_opt = ctx.get<std::string>("--tabs", "");
  if (tabs_opt.empty()) {
    tabs_opt = ctx.get<std::string>("-t", "");
  }

  if (!tabs_opt.empty()) {
    // Parse tab width (can be comma-separated list, but we only support single
    // value)
    try {
      cfg.tab_width = std::stoi(tabs_opt);
      if (cfg.tab_width <= 0) {
        return std::unexpected("tab width must be positive");
      }
    } catch (...) {
      return std::unexpected("invalid tab width");
    }
  }

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

// Expand tabs to spaces in a single line
auto expand_line(const std::string& line, int tab_width, bool initial_only)
    -> std::string {
  std::string result;
  result.reserve(line.size() * 2);
  size_t column = 0;

  for (char c : line) {
    if (c == '\t') {
      // Calculate spaces needed to reach next tab stop
      int spaces_needed = tab_width - (column % tab_width);
      result.append(spaces_needed, ' ');
      column += spaces_needed;
    } else {
      result += c;
      if (c == '\n') {
        column = 0;
      } else if (!initial_only || column == 0) {
        // Only count column if not in initial_only mode or if we're still at
        // start
        column++;
      }
    }
  }

  return result;
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;

  for (const auto& file : cfg.files) {
    std::string content;

    if (file == "-") {
      // Read from stdin
      content.assign(std::istreambuf_iterator<char>(std::cin),
                     std::istreambuf_iterator<char>());
    } else {
      // Read from file
      std::ifstream f(file, std::ios::binary);
      if (!f) {
        auto err = std::string("cannot open '") + file + "' for reading";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"expand");
        all_ok = false;
        continue;
      }
      content.assign(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
      if (f.fail() && !f.eof()) {
        cp::Result<int> result = std::unexpected("error reading from file");
        cp::report_error(result, L"expand");
        all_ok = false;
        continue;
      }
      // Skip UTF-8 BOM if present at the beginning
      if (content.size() >= 3 &&
          static_cast<unsigned char>(content[0]) == 0xEF &&
          static_cast<unsigned char>(content[1]) == 0xBB &&
          static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
      }
    }

    // Process line by line to maintain line breaks
    std::string result;
    size_t line_start = 0;
    while (line_start < content.size()) {
      size_t line_end = content.find('\n', line_start);
      std::string line;

      if (line_end == std::string::npos) {
        line = content.substr(line_start);
        result += expand_line(line, cfg.tab_width, cfg.initial_only);
        break;
      } else {
        line = content.substr(line_start,
                              line_end - line_start + 1);  // Include newline
        result += expand_line(line, cfg.tab_width, cfg.initial_only);
        line_start = line_end + 1;
      }
    }

    safePrint(result);
  }

  return all_ok ? 0 : 1;
}

}  // namespace expand_pipeline

REGISTER_COMMAND(
    expand, "expand", "expand [OPTION]... [FILE]...",
    "Convert tabs to spaces.\n"
    "\n"
    "Convert each tab character to one or more spaces.\n"
    "By default, tabs are converted to 8 spaces.\n"
    "\n"
    "Note: This implementation supports basic tab expansion.\n"
    "Advanced features like comma-separated tab positions are not implemented.",
    "  expand file.txt\n"
    "  expand -t 4 file.txt\n"
    "  expand -i file.txt\n"
    "  echo -e 'hello\tworld' | expand",
    "unexpand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", EXPAND_OPTIONS) {
  using namespace expand_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"expand");
    return 1;
  }

  return run(*cfg_result);
}
