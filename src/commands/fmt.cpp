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
 *  - File: fmt.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for fmt.
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

auto constexpr FMT_OPTIONS = std::array{
    OPTION("-c", "--crown-margin",
           "preserve indentation of the first two lines", BOOL_TYPE),
    OPTION("-p", "--prefix", "reformat only lines beginning with STRING",
           STRING_TYPE),
    OPTION("-s", "--split-only", "split long lines, but do not refill",
           BOOL_TYPE),
    OPTION("-t", "--tagged-paragraph", "expect indentation in first 2 lines",
           BOOL_TYPE),
    OPTION("-u", "--uniform-spacing",
           "one space between words, two after sentences", BOOL_TYPE),
    OPTION("-w", "--width", "maximum line width (default 75)", STRING_TYPE),
    OPTION("-g", "--goal", "goal width (default 93% of width)", STRING_TYPE)};

namespace fmt_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool crown_margin = false;
  bool split_only = false;
  bool tagged_paragraph = false;
  bool uniform_spacing = false;
  int width = 75;
  int goal = 0;  // Will be calculated as 93% of width
  std::string prefix;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<FMT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.crown_margin =
      ctx.get<bool>("--crown-margin", false) || ctx.get<bool>("-c", false);
  cfg.split_only =
      ctx.get<bool>("--split-only", false) || ctx.get<bool>("-s", false);
  cfg.tagged_paragraph =
      ctx.get<bool>("--tagged-paragraph", false) || ctx.get<bool>("-t", false);
  cfg.uniform_spacing =
      ctx.get<bool>("--uniform-spacing", false) || ctx.get<bool>("-u", false);

  auto prefix_opt = ctx.get<std::string>("--prefix", "");
  if (prefix_opt.empty()) {
    prefix_opt = ctx.get<std::string>("-p", "");
  }
  cfg.prefix = prefix_opt;

  auto width_opt = ctx.get<std::string>("--width", "");
  if (width_opt.empty()) {
    width_opt = ctx.get<std::string>("-w", "");
  }
  if (!width_opt.empty()) {
    try {
      cfg.width = std::stoi(width_opt);
      if (cfg.width < 10) {
        return std::unexpected("width must be at least 10");
      }
    } catch (...) {
      return std::unexpected("invalid width value");
    }
  }

  auto goal_opt = ctx.get<std::string>("--goal", "");
  if (goal_opt.empty()) {
    goal_opt = ctx.get<std::string>("-g", "");
  }
  if (!goal_opt.empty()) {
    try {
      cfg.goal = std::stoi(goal_opt);
    } catch (...) {
      return std::unexpected("invalid goal value");
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

auto read_input(const std::string& filename) -> cp::Result<std::string> {
  std::string content;

  if (filename == "-") {
    // Read from stdin line by line (like cat and fold)
    std::string line;
    while (std::getline(std::cin, line)) {
      content += line + "\n";
    }
  } else {
    // Read from file
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }
    // Read file content
    content.assign(std::istreambuf_iterator<char>(f),
                   std::istreambuf_iterator<char>());
    if (f.fail() && !f.eof()) {
      return std::unexpected("error reading from file");
    }
    // Skip UTF-8 BOM if present at the beginning
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
      content = content.substr(3);
    }
  }

  return content;
}

auto run(const Config& cfg) -> int {
  int effective_goal = (cfg.goal > 0) ? cfg.goal : (cfg.width * 93 / 100);

  for (const auto& file : cfg.files) {
    auto content_result = read_input(file);
    if (!content_result) {
      cp::report_error(content_result, L"fmt");
      return 1;
    }

    const std::string& content = *content_result;

    if (cfg.split_only) {
      // Just split long lines, don't reflow
      std::string line;
      for (char c : content) {
        if (c == '\n') {
          safePrintLn(line);
          line.clear();
        } else if (c != '\r') {
          if (!line.empty() && static_cast<int>(line.length()) >= cfg.width) {
            safePrintLn(line);
            line.clear();
          }
          line += c;
        }
      }
      if (!line.empty()) {
        safePrintLn(line);
      }
    } else {
      // Simple paragraph formatting
      std::string word;
      std::string line;
      int line_len = 0;

      for (char c : content) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
          if (!word.empty()) {
            if (line.empty()) {
              line = word;
              line_len = static_cast<int>(word.length());
            } else {
              if (line_len + 1 + static_cast<int>(word.length()) > cfg.width) {
                safePrintLn(line);
                line = word;
                line_len = static_cast<int>(word.length());
              } else {
                line += ' ';
                line += word;
                line_len += 1 + static_cast<int>(word.length());
              }
            }
            word.clear();
          }
        } else {
          word += c;
        }
      }

      // Handle last word
      if (!word.empty()) {
        if (line.empty()) {
          line = word;
        } else {
          line += ' ';
          line += word;
        }
      }

      // Output last line
      if (!line.empty()) {
        safePrintLn(line);
      }
    }
  }

  return 0;
}

}  // namespace fmt_pipeline

REGISTER_COMMAND(
    fmt, "fmt", "fmt [OPTION]... [FILE]...",
    "Reformat paragraphs.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Note: This is a simplified implementation. Advanced features\n"
    "like crown margin and tagged paragraphs are not fully supported.",
    "  fmt file.txt\n"
    "  fmt -w 60 file.txt\n"
    "  fmt -s file.txt",
    "fold(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", FMT_OPTIONS) {
  using namespace fmt_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"fmt");
    return 1;
  }

  return run(*cfg_result);
}
