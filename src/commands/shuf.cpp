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
 *  - File: shuf.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for shuf.
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

auto constexpr SHUF_OPTIONS = std::array{
    OPTION("-e", "--echo", "treat each ARG as an input line", BOOL_TYPE),
    OPTION("-i", "--input-range",
           "treat each number LO through HI as an input line", STRING_TYPE),
    OPTION("-n", "--head-count", "output at most COUNT lines", STRING_TYPE),
    OPTION("-r", "--repeat", "output lines can be repeated", BOOL_TYPE),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline",
           BOOL_TYPE)};

namespace shuf_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool echo_mode = false;
  int head_count = -1;  // -1 means all lines
  bool repeat = false;
  bool zero_terminated = false;
  std::string input_range;
  SmallVector<std::string, 64> files;
  SmallVector<std::string, 64> echo_args;
};

auto build_config(const CommandContext<SHUF_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.echo_mode = ctx.get<bool>("--echo", false) || ctx.get<bool>("-e", false);
  cfg.repeat = ctx.get<bool>("--repeat", false) || ctx.get<bool>("-r", false);
  cfg.zero_terminated =
      ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false);

  auto range_opt = ctx.get<std::string>("--input-range", "");
  if (range_opt.empty()) {
    range_opt = ctx.get<std::string>("-i", "");
  }
  cfg.input_range = range_opt;

  auto count_opt = ctx.get<std::string>("--head-count", "");
  if (count_opt.empty()) {
    count_opt = ctx.get<std::string>("-n", "");
  }
  if (!count_opt.empty()) {
    try {
      cfg.head_count = std::stoi(count_opt);
      if (cfg.head_count < 0) {
        return std::unexpected("invalid line count");
      }
    } catch (...) {
      return std::unexpected("invalid line count");
    }
  }

  if (cfg.echo_mode) {
    for (auto arg : ctx.positionals) {
      cfg.echo_args.push_back(std::string(arg));
    }
  } else {
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
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  SmallVector<std::string, 1024> lines;

  if (cfg.echo_mode) {
    // Echo mode: treat each ARG as an input line
    for (const auto& arg : cfg.echo_args) {
      lines.push_back(arg);
    }
  } else if (!cfg.input_range.empty()) {
    // Input range mode: generate numbers LO through HI
    try {
      size_t dash_pos = cfg.input_range.find('-');
      if (dash_pos == std::string::npos) {
        return 1;
      }

      int lo = std::stoi(cfg.input_range.substr(0, dash_pos));
      int hi = std::stoi(cfg.input_range.substr(dash_pos + 1));

      for (int i = lo; i <= hi; ++i) {
        lines.push_back(std::to_string(i));
      }
    } catch (...) {
      return 1;
    }
  } else {
    // File mode: read from files
    SmallVector<std::string, 64> files = cfg.files;
    if (files.empty()) {
      files.push_back("-");  // Read from stdin
    }

    for (const auto& file : files) {
      if (file == "-") {
        std::string line;
        while (std::getline(std::cin, line)) {
          lines.push_back(line);
        }
      } else {
        std::ifstream f(file, std::ios::binary);
        if (!f) {
          auto err = std::string("cannot open '") + file + "' for reading";
          cp::Result<int> result = std::unexpected(std::string_view(err));
          cp::report_error(result, L"shuf");
          return 1;
        }

        std::string line;
        while (std::getline(f, line)) {
          // Skip UTF-8 BOM if present at the beginning of the first line
          if (lines.empty() && line.size() >= 3 &&
              static_cast<unsigned char>(line[0]) == 0xEF &&
              static_cast<unsigned char>(line[1]) == 0xBB &&
              static_cast<unsigned char>(line[2]) == 0xBF) {
            line = line.substr(3);
          }
          lines.push_back(line);
        }

        if (f.fail() && !f.eof()) {
          cp::Result<int> result = std::unexpected("error reading from file");
          cp::report_error(result, L"shuf");
          return 1;
        }
      }
    }
  }

  if (lines.empty()) {
    return 0;
  }

  // Shuffle using Fisher-Yates algorithm
  std::random_device rd;
  std::mt19937 g(rd());

  for (int i = lines.size() - 1; i > 0; --i) {
    std::uniform_int_distribution<int> dist(0, i);
    int j = dist(g);
    std::swap(lines[i], lines[j]);
  }

  // Output shuffled lines
  int output_count =
      (cfg.head_count >= 0)
          ? std::min(cfg.head_count, static_cast<int>(lines.size()))
          : static_cast<int>(lines.size());

  for (int i = 0; i < output_count; ++i) {
    safePrint(lines[i]);
    if (cfg.zero_terminated) {
      safePrint("\0");
    } else {
      safePrintLn("");
    }
  }

  return 0;
}

}  // namespace shuf_pipeline

REGISTER_COMMAND(
    shuf, "shuf", "shuf [OPTION]... [FILE]",
    "Shuffle randomize lines.\n"
    "\n"
    "Write a random permutation of the input lines to standard output.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This implementation uses the Mersenne Twister PRNG\n"
    "from the C++ standard library.",
    "  shuf file.txt\n"
    "  shuf -n 5 file.txt\n"
    "  shuf -e a b c d e\n"
    "  shuf -i 1-10",
    "sort(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SHUF_OPTIONS) {
  using namespace shuf_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"shuf");
    return 1;
  }

  return run(*cfg_result);
}
