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
 *  - File: unexpand.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for unexpand.
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

auto constexpr UNEXPAND_OPTIONS = std::array{
    OPTION("-t", "--tabs", "specify tab stop positions (default: 8)",
           STRING_TYPE),
    OPTION("-a", "--all", "convert all spaces, not just leading ones",
           BOOL_TYPE)
    // --help (not implemented)
    // --version (not implemented)
};

namespace unexpand_pipeline {
namespace cp = core::pipeline;

struct Config {
  int tab_width = 8;
  bool all_spaces = false;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<UNEXPAND_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.all_spaces = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);

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

// Convert spaces to tabs in a single line
auto unexpand_line(const std::string& line, int tab_width, bool all_spaces)
    -> std::string {
  std::string result;
  result.reserve(line.size());
  size_t pos = 0;

  while (pos < line.size()) {
    if (line[pos] == ' ') {
      // Count consecutive spaces
      size_t space_count = 0;
      while (pos < line.size() && line[pos] == ' ') {
        space_count++;
        pos++;
      }

      // Convert spaces to tabs if possible
      // Only convert if we're at the beginning of line (not all_spaces mode)
      // or if we're in all_spaces mode
      bool convert_to_tabs = all_spaces || result.empty();

      if (convert_to_tabs && space_count >= 2) {
        // Try to convert to tabs
        int tabs = 0;
        int remaining_spaces = space_count;

        // Calculate how many tabs we can use
        while (remaining_spaces >= tab_width) {
          tabs++;
          remaining_spaces -= tab_width;
        }

        // Only use tabs if we can save at least 1 space
        if (tabs > 0 && (tabs * tab_width) > remaining_spaces) {
          result.append(tabs, '\t');
          result.append(remaining_spaces, ' ');
        } else {
          result.append(space_count, ' ');
        }
      } else {
        result.append(space_count, ' ');
      }
    } else {
      result += line[pos];
      pos++;
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
        cp::report_error(result, L"unexpand");
        all_ok = false;
        continue;
      }
      content.assign(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
      if (f.fail() && !f.eof()) {
        cp::Result<int> result = std::unexpected("error reading from file");
        cp::report_error(result, L"unexpand");
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
        result += unexpand_line(line, cfg.tab_width, cfg.all_spaces);
        break;
      } else {
        line = content.substr(line_start,
                              line_end - line_start + 1);  // Include newline
        result += unexpand_line(line, cfg.tab_width, cfg.all_spaces);
        line_start = line_end + 1;
      }
    }

    safePrint(result);
  }

  return all_ok ? 0 : 1;
}

}  // namespace unexpand_pipeline

REGISTER_COMMAND(
    unexpand, "unexpand", "unexpand [OPTION]... [FILE]...",
    "Convert spaces to tabs.\n"
    "\n"
    "Convert spaces to tabs. By default, only convert leading spaces to tabs.\n"
    "Use -a to convert all spaces.\n"
    "\n"
    "Note: This implementation supports basic space-to-tab conversion.\n"
    "Advanced features like comma-separated tab positions are not implemented.",
    "  unexpand file.txt\n"
    "  unexpand -t 4 file.txt\n"
    "  unexpand -a file.txt\n"
    "  echo 'hello world' | unexpand",
    "expand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", UNEXPAND_OPTIONS) {
  using namespace unexpand_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"unexpand");
    return 1;
  }

  return run(*cfg_result);
}
