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
    // [GNU] util-linux col.c long options (col.c longopts table)
    OPTION("-b", "--no-backspaces", "do not output backspaces"),
    // [GNU]
    OPTION("-f", "--fine", "permit forward half line feeds"),
    // [GNU]
    OPTION("-p", "--pass", "pass unknown control sequences"),
    // [GNU] -h, --tabs: compress spaces into tabs (the default)
    OPTION("-h", "--tabs", "output tabs instead of multiple spaces"),
    // [GNU] -x, --spaces: do not compress spaces into tabs
    OPTION("-x", "--spaces", "output spaces instead of tabs"),
    // [GNU] -l, --lines: buffer at least NUM lines in memory. The oracle
    // default is 32 lines (BUFFER_MARGIN * 2 half-lines, col.c:88,651).
    OPTION("-l", "--lines", "buffer at least NUM lines (default 32)",
           STRING_TYPE)};

namespace col_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool no_backspaces = false;
  bool forward_half = false;
  bool pass_unknown = false;
  bool compress_spaces = true;
  size_t buffer_lines = 32;
};

auto build_config(const CommandContext<COL_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.no_backspaces =
      ctx.get<bool>("-b", false) || ctx.get<bool>("--no-backspaces", false);
  cfg.forward_half =
      ctx.get<bool>("-f", false) || ctx.get<bool>("--fine", false);
  cfg.pass_unknown =
      ctx.get<bool>("-p", false) || ctx.get<bool>("--pass", false);

  // [GNU] -h/--tabs and -x/--spaces are mutually exclusive (col.c
  // err_exclusive_options); -x turns the default space->tab compression
  // off (col.c:571), -h turns it on (col.c:558).
  const bool tabs_flag =
      ctx.get<bool>("-h", false) || ctx.get<bool>("--tabs", false);
  const bool spaces_flag =
      ctx.get<bool>("-x", false) || ctx.get<bool>("--spaces", false);
  if (tabs_flag && spaces_flag) {
    return std::unexpected("--tabs and --spaces are mutually exclusive");
  }
  cfg.compress_spaces = !spaces_flag;

  auto lines_opt = ctx.get<std::string>("-l", "");
  if (lines_opt.empty()) {
    lines_opt = ctx.get<std::string>("--lines", "");
  }
  if (!lines_opt.empty()) {
    unsigned long val = 0;
    auto [ptr, ec] = std::from_chars(lines_opt.data(),
                                     lines_opt.data() + lines_opt.size(), val);
    if (ec != std::errc() || ptr != lines_opt.data() + lines_opt.size()) {
      return std::unexpected("bad -l argument");
    }
    cfg.buffer_lines = static_cast<size_t>(val);
  }

  return cfg;
}

// [GNU] Space->tab compression, following col.c:288-303 exactly: for a
// gap of more than one space between the last emitted column and the
// next character, emit (next/8 - last/8) tabs and pad the remainder
// (next & 7) with spaces. Trailing gaps are dropped, as col drops
// trailing blanks when a line is flushed.
auto compress_line(const std::string& line) -> std::string {
  std::string out;
  size_t col_pos = 0;
  size_t i = 0;
  while (i < line.size()) {
    if (line[i] == ' ') {
      size_t run_start = i;
      while (i < line.size() && line[i] == ' ') {
        ++i;
      }
      if (i >= line.size()) {
        break;  // trailing blanks are not emitted
      }
      const size_t target = col_pos + (i - run_start);
      size_t nspace = target - col_pos;
      if (nspace > 1) {
        const size_t ntabs = target / 8 - col_pos / 8;
        if (ntabs > 0) {
          out.append(ntabs, '\t');
          nspace = target & 7;
        }
      }
      out.append(nspace, ' ');
      col_pos = target;
      continue;
    }
    if (line[i] == '\t') {
      out.push_back('\t');
      col_pos += 8 - (col_pos % 8);
      ++i;
      continue;
    }
    out.push_back(line[i]);
    ++col_pos;
    ++i;
  }
  return out;
}

auto run(const Config& cfg) -> int {
  file_io::set_stdin_binary();
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

      case '\t':  // Tab: advance to the next tab stop. The gap is
                  // materialized at output time (tabs when compression
                  // is on, spaces with -x/--spaces), as in col.c, where
                  // input tabs only move the column position.
        current_col += 8 - (current_col % 8);
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
    safePrintLn(cfg.compress_spaces ? compress_line(lines[i]) : lines[i]);
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
    "  -b, --no-backspaces    do not output backspaces\n"
    "  -f, --fine             permit forward half line feeds\n"
    "  -h, --tabs             output tabs instead of multiple spaces\n"
    "  -p, --pass             pass unknown control sequences\n"
    "  -x, --spaces           output spaces instead of tabs\n"
    "  -l, --lines NUM        buffer at least NUM lines (default 32)",
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
