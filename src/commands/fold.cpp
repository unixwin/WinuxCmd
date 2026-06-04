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
    OPTION("-c", "--characters", "count characters rather than columns",
           BOOL_TYPE),
    OPTION("-s", "--spaces", "break at spaces", BOOL_TYPE),
    OPTION("-w", "--width", "use WIDTH columns instead of 80", STRING_TYPE)};

namespace fold_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool count_bytes = false;
  bool count_characters = false;
  bool break_at_spaces = false;
  int width = 80;
  SmallVector<std::string, 64> files;
};

auto parse_width(std::string_view value) -> cp::Result<int> {
  int parsed = 0;
  auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (ec != std::errc() || ptr != value.data() + value.size() || parsed <= 0) {
    return std::unexpected("invalid width");
  }
  return parsed;
}

auto build_config(const CommandContext<FOLD_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.count_bytes =
      ctx.get<bool>("--bytes", false) || ctx.get<bool>("-b", false);
  cfg.count_characters =
      ctx.get<bool>("--characters", false) || ctx.get<bool>("-c", false);
  cfg.break_at_spaces =
      ctx.get<bool>("--spaces", false) || ctx.get<bool>("-s", false);

  auto width_opt = ctx.get<std::string>("--width", "");
  if (width_opt.empty()) {
    width_opt = ctx.get<std::string>("-w", "");
  }
  if (!width_opt.empty()) {
    auto width = parse_width(width_opt);
    if (!width) return std::unexpected(width.error());
    cfg.width = *width;
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

auto next_column(char c, size_t column, bool count_bytes, bool count_characters)
    -> size_t {
  if (count_bytes || count_characters) {
    return column + 1;
  }
  if (c == '\t') {
    return ((column / 8) + 1) * 8;
  }
  if (c == '\b') {
    return column == 0 ? 0 : column - 1;
  }
  if (c == '\r') {
    return 0;
  }
  return column + 1;
}

auto display_column(std::string_view text, bool count_bytes,
                    bool count_characters) -> size_t {
  size_t column = 0;
  for (char c : text) {
    column = next_column(c, column, count_bytes, count_characters);
  }
  return column;
}

// Fold content while preserving existing line endings, including a final line
// without a trailing newline.
auto fold_content(const std::string& content, int width, bool count_bytes,
                  bool count_characters, bool break_at_spaces) -> std::string {
  std::string result;
  std::string current;
  size_t column = 0;
  size_t last_blank = std::string::npos;

  auto append_line_break = [&]() {
    result += current;
    result += '\n';
    current.clear();
    column = 0;
    last_blank = std::string::npos;
  };

  auto break_at_last_blank = [&]() {
    result.append(current.substr(0, last_blank + 1));
    result += '\n';
    current.erase(0, last_blank + 1);
    column = display_column(current, count_bytes, count_characters);
    last_blank = current.find_last_of(" \t");
  };

  for (char c : content) {
    if (c == '\n') {
      append_line_break();
      continue;
    }

    size_t next = next_column(c, column, count_bytes, count_characters);
    while (!current.empty() && next > static_cast<size_t>(width)) {
      if (break_at_spaces && last_blank != std::string::npos) {
        break_at_last_blank();
      } else {
        append_line_break();
      }
      next = next_column(c, column, count_bytes, count_characters);
    }

    current += c;
    column = next;
    if (c == ' ' || c == '\t') {
      last_blank = current.size() - 1;
    }
  }

  result += current;
  return result;
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;

  for (const auto& file : cfg.files) {
    std::string content;
    if (file == "-") {
      content.assign(std::istreambuf_iterator<char>(std::cin),
                     std::istreambuf_iterator<char>());
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

      content.assign(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
      if (f.fail() && !f.eof()) {
        cp::Result<int> result = std::unexpected("error reading from file");
        cp::report_error(result, L"fold");
        all_ok = false;
        continue;
      }
      if (content.size() >= 3 &&
          static_cast<unsigned char>(content[0]) == 0xEF &&
          static_cast<unsigned char>(content[1]) == 0xBB &&
          static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
      }
    }

    auto folded = fold_content(content, cfg.width, cfg.count_bytes,
                               cfg.count_characters, cfg.break_at_spaces);
    safePrint(folded);
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
                 "  fold -c file.txt\n"
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
