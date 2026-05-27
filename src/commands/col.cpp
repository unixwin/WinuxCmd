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
 *  - File: col.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for col.
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

auto constexpr COL_OPTIONS = std::array{
    OPTION("-b", "", "do not output backspaces"),
    OPTION("-f", "", "permit forward half line feeds"),
    OPTION("-p", "", "pass unknown control sequences"),
    OPTION("-x", "", "output spaces instead of tabs"),
    OPTION("-l", "", "buffer at least NUM lines (default 128)", STRING_TYPE)};

namespace col_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool no_backspaces = false;
  bool forward_half = false;
  bool pass_unknown = false;
  bool spaces_for_tabs = false;
  size_t buffer_lines = 128;
};

auto build_config(const CommandContext<COL_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.no_backspaces = ctx.get<bool>("-b", false);
  cfg.forward_half = ctx.get<bool>("-f", false);
  cfg.pass_unknown = ctx.get<bool>("-p", false);
  cfg.spaces_for_tabs = ctx.get<bool>("-x", false);

  auto lines_opt = ctx.get<std::string>("-l", "");
  if (!lines_opt.empty()) {
    try {
      int val = std::stoi(lines_opt);
      if (val < 1) return std::unexpected("invalid line count");
      cfg.buffer_lines = static_cast<size_t>(val);
    } catch (...) {
      return std::unexpected("invalid line count");
    }
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  // Read all input
  std::string input;
  char buf[4096];
  while (std::cin.read(buf, sizeof(buf))) {
    input.append(buf, std::cin.gcount());
  }
  if (std::cin.gcount() > 0) {
    input.append(buf, std::cin.gcount());
  }

  // Process the input
  // col processes reverse line feeds and backspaces
  std::vector<std::string> lines;
  lines.push_back("");
  size_t current_col = 0;

  for (size_t i = 0; i < input.size(); ++i) {
    unsigned char ch = static_cast<unsigned char>(input[i]);

    switch (ch) {
      case '\b':  // Backspace
        if (cfg.no_backspaces) {
          // Skip backspace
        } else if (current_col > 0) {
          current_col--;
        }
        break;

      case '\n':  // Newline
        lines.push_back("");
        current_col = 0;
        break;

      case '\r':  // Carriage return
        current_col = 0;
        break;

      case '\t':  // Tab
        if (cfg.spaces_for_tabs) {
          size_t spaces = 8 - (current_col % 8);
          lines.back().append(spaces, ' ');
          current_col += spaces;
        } else {
          lines.back().push_back('\t');
          current_col += 8 - (current_col % 8);
        }
        break;

      case '\v':  // Vertical tab (reverse line feed)
        if (cfg.forward_half && lines.size() > 1) {
          lines.pop_back();
          current_col = 0;
        }
        break;

      case '\f':  // Form feed
        lines.push_back("");
        current_col = 0;
        break;

      default:
        if (ch >= 32 && ch <= 126) {
          // Ensure line is long enough
          while (lines.back().size() < current_col) {
            lines.back().push_back(' ');
          }
          if (lines.back().size() == current_col) {
            lines.back().push_back(static_cast<char>(ch));
          } else {
            lines.back()[current_col] = static_cast<char>(ch);
          }
          current_col++;
        } else if (cfg.pass_unknown) {
          // Pass through unknown control sequences
          lines.back().push_back(static_cast<char>(ch));
        }
        break;
    }
  }

  // Output processed lines
  for (const auto& line : lines) {
    safePrintLn(line);
  }

  return 0;
}

}  // namespace col_pipeline

REGISTER_COMMAND(
    col, "col",
    "col [OPTION]...",
    "Filter reverse line feeds from standard input.\n"
    "\n"
    "col filters out reverse (and half-reverse) line feeds so that the output\n"
    "is in the correct order with only forward and half-forward line feeds.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -b    do not output backspaces\n"
    "  -f    permit forward half line feeds\n"
    "  -p    pass unknown control sequences\n"
    "  -x    output spaces instead of tabs\n"
    "  -l    buffer at least NUM lines (default 128)",
    "  col -b          filter backspaces\n"
    "  col -x          convert tabs to spaces\n"
    "  col -f          allow forward half line feeds",
    "expand(1), unexpand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    COL_OPTIONS) {
  using namespace col_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"col");
    return 1;
  }

  return run(*cfg_result);
}
