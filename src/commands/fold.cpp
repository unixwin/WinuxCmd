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
 *  - File: fold.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for fold.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr FOLD_OPTIONS = std::array{
    OPTION("-b", "--bytes", "count bytes rather than columns", BOOL_TYPE),
    OPTION("-s", "--spaces", "break at spaces", BOOL_TYPE),
    OPTION("-w", "--width", "use WIDTH columns instead of 80", STRING_TYPE)
    // -c, --columns (not implemented - same as bytes)
};

namespace fold_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool count_bytes = false;
  bool break_at_spaces = false;
  int width = 80;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<FOLD_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.count_bytes =
      ctx.get<bool>("--bytes", false) || ctx.get<bool>("-b", false);
  cfg.break_at_spaces =
      ctx.get<bool>("--spaces", false) || ctx.get<bool>("-s", false);

  auto width_opt = ctx.get<std::string>("--width", "");
  if (width_opt.empty()) {
    width_opt = ctx.get<std::string>("-w", "");
  }
  if (!width_opt.empty()) {
    try {
      cfg.width = std::stoi(width_opt);
      if (cfg.width <= 0) {
        return std::unexpected("width must be positive");
      }
    } catch (...) {
      return std::unexpected("invalid width");
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

// Fold a single line
auto fold_line(const std::string& line, int width, bool count_bytes,
               bool break_at_spaces) -> std::string {
  if (line.empty()) {
    return "\n";
  }

  std::string result;
  size_t pos = 0;

  while (pos < line.size()) {
    int current_length = 0;
    size_t start_pos = pos;
    size_t last_space_pos = std::string::npos;

    // Find where to break
    while (pos < line.size()) {
      char c = line[pos];
      int char_width =
          count_bytes ? 1 : 1;  // Simplified: assume 1 column per char

      if (current_length + char_width > width) {
        // Need to break
        if (break_at_spaces && last_space_pos != std::string::npos &&
            last_space_pos > start_pos) {
          // Break at last space
          result.append(line.substr(start_pos, last_space_pos - start_pos));
          result += "\n";
          pos = last_space_pos + 1;  // Skip the space
        } else {
          // Break at current position
          result.append(line.substr(start_pos, pos - start_pos));
          result += "\n";
          // Don't increment pos, we'll process this character in next line
        }
        current_length = 0;
        start_pos = pos;
        last_space_pos = std::string::npos;
      } else {
        if (c == ' ') {
          last_space_pos = pos;
        }
        current_length += char_width;
        pos++;
      }
    }

    // Add remaining text
    if (pos > start_pos) {
      result.append(line.substr(start_pos));
    }

    // If original line ended with newline, add it (but check if result already
    // has one)
    if (!line.empty() && line.back() == '\n') {
      if (!result.empty() && result.back() != '\n') {
        result += "\n";
      }
    }
  }

  return result;
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;

  for (const auto& file : cfg.files) {
    if (file == "-") {
      // Read from stdin
      std::string line;
      while (std::getline(std::cin, line)) {
        line += "\n";  // Preserve line ending
        auto folded =
            fold_line(line, cfg.width, cfg.count_bytes, cfg.break_at_spaces);
        safePrint(folded);
      }
    } else {
      // Read from file
      std::ifstream f(file, std::ios::binary);
      if (!f) {
        auto err = std::string("cannot open '") + file + "' for reading";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"fold");
        all_ok = false;
        continue;
      }

      bool first_line = true;
      std::string line;
      while (std::getline(f, line)) {
        // Skip UTF-8 BOM if present at the beginning of the first line
        if (first_line && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
          line = line.substr(3);
        }
        first_line = false;
        line += "\n";  // Preserve line ending
        auto folded =
            fold_line(line, cfg.width, cfg.count_bytes, cfg.break_at_spaces);
        safePrint(folded);
      }

      if (f.fail() && !f.eof()) {
        cp::Result<int> result = std::unexpected("error reading from file");
        cp::report_error(result, L"fold");
        all_ok = false;
        continue;
      }
    }
  }

  return all_ok ? 0 : 1;
}

}  // namespace fold_pipeline

REGISTER_COMMAND(fold, "fold", "fold [OPTION]... [FILE]...",
                 "Wrap input lines to fit specified width.\n"
                 "\n"
                 "Write each FILE to standard output, wrapping input lines to "
                 "fit in width columns.\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "Note: This implementation supports basic folding.\n"
                 "Advanced features like multi-byte character width "
                 "calculation are not implemented.",
                 "  fold file.txt\n"
                 "  fold -w 60 file.txt\n"
                 "  fold -s file.txt\n"
                 "  fold -b file.txt\n"
                 "  echo 'very long line' | fold -w 10",
                 "fmt(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FOLD_OPTIONS) {
  using namespace fold_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"fold");
    return 1;
  }

  return run(*cfg_result);
}
