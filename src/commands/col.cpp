// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [EXT]
    OPTION("-b", "", "do not output backspaces"),
    // [EXT]
    OPTION("-f", "", "permit forward half line feeds"),
    // [EXT]
    OPTION("-p", "", "pass unknown control sequences"),
    // [EXT]
    OPTION("-x", "", "output spaces instead of tabs"),
    // [EXT]
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
    int val = 0;
    auto [ptr, ec] = std::from_chars(lines_opt.data(),
                                     lines_opt.data() + lines_opt.size(), val);
    if (ec != std::errc() || ptr != lines_opt.data() + lines_opt.size() ||
        val < 1) {
      return std::unexpected("invalid line count");
    }
    cfg.buffer_lines = static_cast<size_t>(val);
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
        if (current_col > 0) {
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

  // Output processed lines. A trailing empty buffer is an artifact of an
  // input newline, not an extra output line.
  size_t output_count = lines.size();
  if (output_count > 1 && lines.back().empty()) {
    --output_count;
  }
  for (size_t i = 0; i < output_count; ++i) {
    safePrintLn(lines[i]);
  }

  return 0;
}

}  // namespace col_pipeline

REGISTER_COMMAND(
    col, "col", "col [OPTION]...",
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
