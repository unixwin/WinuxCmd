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

struct FoldUnit {
  size_t byte_count = 1;
  char32_t codepoint = U'\0';
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

auto decode_utf8_unit(std::string_view text, size_t offset) -> FoldUnit {
  unsigned char lead = static_cast<unsigned char>(text[offset]);
  size_t remaining = text.size() - offset;

  auto single_byte = [&]() {
    return FoldUnit{1, static_cast<char32_t>(lead)};
  };

  if (lead < 0x80) {
    return single_byte();
  }

  if (lead >= 0xC2 && lead <= 0xDF) {
    if (remaining < 2) return single_byte();
    unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
    if ((b1 & 0xC0) != 0x80) return single_byte();
    char32_t cp = (static_cast<char32_t>(lead & 0x1F) << 6) |
                  static_cast<char32_t>(b1 & 0x3F);
    return FoldUnit{2, cp};
  }

  if (lead >= 0xE0 && lead <= 0xEF) {
    if (remaining < 3) return single_byte();
    unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
    unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) return single_byte();
    if ((lead == 0xE0 && b1 < 0xA0) || (lead == 0xED && b1 >= 0xA0)) {
      return single_byte();
    }
    char32_t cp = (static_cast<char32_t>(lead & 0x0F) << 12) |
                  (static_cast<char32_t>(b1 & 0x3F) << 6) |
                  static_cast<char32_t>(b2 & 0x3F);
    return FoldUnit{3, cp};
  }

  if (lead >= 0xF0 && lead <= 0xF4) {
    if (remaining < 4) return single_byte();
    unsigned char b1 = static_cast<unsigned char>(text[offset + 1]);
    unsigned char b2 = static_cast<unsigned char>(text[offset + 2]);
    unsigned char b3 = static_cast<unsigned char>(text[offset + 3]);
    if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 ||
        (b3 & 0xC0) != 0x80) {
      return single_byte();
    }
    if ((lead == 0xF0 && b1 < 0x90) || (lead == 0xF4 && b1 >= 0x90)) {
      return single_byte();
    }
    char32_t cp = (static_cast<char32_t>(lead & 0x07) << 18) |
                  (static_cast<char32_t>(b1 & 0x3F) << 12) |
                  (static_cast<char32_t>(b2 & 0x3F) << 6) |
                  static_cast<char32_t>(b3 & 0x3F);
    return FoldUnit{4, cp};
  }

  return single_byte();
}

auto is_wide_codepoint(char32_t cp) -> bool {
  return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2329 && cp <= 0x232A) ||
         (cp >= 0x2E80 && cp <= 0xA4CF && cp != 0x303F) ||
         (cp >= 0xAC00 && cp <= 0xD7A3) || (cp >= 0xF900 && cp <= 0xFAFF) ||
         (cp >= 0xFE10 && cp <= 0xFE19) || (cp >= 0xFE30 && cp <= 0xFE6F) ||
         (cp >= 0xFF00 && cp <= 0xFF60) || (cp >= 0xFFE0 && cp <= 0xFFE6) ||
         (cp >= 0x1F300 && cp <= 0x1FAFF) || (cp >= 0x20000 && cp <= 0x3FFFD);
}

auto next_column(const FoldUnit& unit, size_t column, bool count_bytes,
                 bool count_characters) -> size_t {
  if (count_bytes) {
    return column + unit.byte_count;
  }
  if (count_characters) {
    return column + 1;
  }
  if (unit.codepoint == U'\t') {
    return ((column / 8) + 1) * 8;
  }
  if (unit.codepoint == U'\b') {
    return column == 0 ? 0 : column - 1;
  }
  if (unit.codepoint == U'\r') {
    return 0;
  }
  return column + (is_wide_codepoint(unit.codepoint) ? 2 : 1);
}

auto display_column(std::string_view text, bool count_bytes,
                    bool count_characters) -> size_t {
  size_t column = 0;
  for (size_t i = 0; i < text.size();) {
    FoldUnit unit =
        count_bytes ? FoldUnit{1, static_cast<unsigned char>(text[i])}
                    : decode_utf8_unit(text, i);
    column = next_column(unit, column, count_bytes, count_characters);
    i += unit.byte_count;
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

  for (size_t i = 0; i < content.size();) {
    if (content[i] == '\n') {
      append_line_break();
      ++i;
      continue;
    }

    FoldUnit unit =
        count_bytes ? FoldUnit{1, static_cast<unsigned char>(content[i])}
                    : decode_utf8_unit(content, i);
    std::string_view bytes(content.data() + i, unit.byte_count);
    size_t next = next_column(unit, column, count_bytes, count_characters);
    while (!current.empty() && next > static_cast<size_t>(width)) {
      if (break_at_spaces && last_blank != std::string::npos) {
        break_at_last_blank();
      } else {
        append_line_break();
      }
      next = next_column(unit, column, count_bytes, count_characters);
    }

    current.append(bytes);
    column = next;
    if (unit.codepoint == U' ' || unit.codepoint == U'\t') {
      last_blank = current.size() - 1;
    }
    i += unit.byte_count;
  }

  result += current;
  return result;
}

auto normalize_crlf_text(std::string_view content) -> std::string {
  std::string normalized;
  normalized.reserve(content.size());

  for (size_t i = 0; i < content.size(); ++i) {
    if (content[i] == '\r') {
      if (i + 1 == content.size()) {
        continue;
      }
      if (content[i + 1] == '\n') {
        continue;
      }
    }
    normalized.push_back(content[i]);
  }

  return normalized;
}

auto run(const Config& cfg) -> int {
  auto fold_input_open_error = [](std::string_view path) -> std::string {
    std::error_code ec;
    auto status = std::filesystem::status(std::filesystem::u8path(path), ec);
    if (!ec && status.type() == std::filesystem::file_type::directory) {
      return std::string("cannot open '") + std::string(path) +
             "' for reading: Is a directory";
    }
    return std::string("cannot open '") + std::string(path) +
           "' for reading: No such file or directory";
  };

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
        auto err = fold_input_open_error(file);
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

    content = normalize_crlf_text(content);

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
                 "Some locale-specific character-width details remain "
                 "approximate on Windows.",
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
