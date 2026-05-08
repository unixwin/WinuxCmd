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
 *  - File: join.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for join.
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

auto constexpr JOIN_OPTIONS = std::array{
    OPTION("-1", "", "join on this FIELD of file 1", STRING_TYPE),
    OPTION("-2", "", "join on this FIELD of file 2", STRING_TYPE),
    OPTION("-t", "", "use CHAR as input and output field separator",
           STRING_TYPE),
    OPTION("-e", "--empty", "replace missing input fields with EMPTY",
           STRING_TYPE),
    OPTION("-o", "--output", "use specified output format", STRING_TYPE),
    OPTION("-j", "", "equivalent to -1 FIELD -2 FIELD", STRING_TYPE)
    // -a, --check-order (not implemented)
    // --header (not implemented)
};

namespace join_pipeline {
namespace cp = core::pipeline;

struct Config {
  int field1 = 1;
  int field2 = 1;
  char separator = ' ';  // Default to space (standard join uses whitespace)
  std::string empty_field;
  std::string output_format;
  SmallVector<std::string, 64> files;
};

auto split_line(const std::string& line, char sep, int field_num)
    -> std::string {
  std::string result;
  int current_field = 1;

  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == sep) {
      current_field++;
      if (current_field > field_num) {
        break;
      }
    } else if (current_field == field_num) {
      result += line[i];
    }
  }

  return result;
}

auto split_all_fields(const std::string& line, char sep)
    -> SmallVector<std::string, 64> {
  SmallVector<std::string, 64> fields;
  std::string current;

  for (char c : line) {
    if (c == sep) {
      // Trim trailing whitespace from current field
      while (!current.empty() &&
             (current.back() == ' ' || current.back() == '\t' ||
              current.back() == '\n' || current.back() == '\r')) {
        current.pop_back();
      }
      if (!current.empty()) {
        fields.push_back(current);
      }
      current.clear();
    } else {
      current += c;
    }
  }

  // Trim trailing whitespace from last field
  while (!current.empty() &&
         (current.back() == ' ' || current.back() == '\t' ||
          current.back() == '\n' || current.back() == '\r')) {
    current.pop_back();
  }
  if (!current.empty()) {
    fields.push_back(current);
  }

  return fields;
}

auto build_config(const CommandContext<JOIN_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  // Don't override the default separator (space) from Config struct
  // cfg.separator = '\t';  // Removed - use default space separator

  auto field1_opt = ctx.get<std::string>("-1", "");
  if (!field1_opt.empty()) {
    try {
      cfg.field1 = std::stoi(field1_opt);
    } catch (...) {
      return std::unexpected("invalid field number for -1");
    }
  }

  auto field2_opt = ctx.get<std::string>("-2", "");
  if (!field2_opt.empty()) {
    try {
      cfg.field2 = std::stoi(field2_opt);
    } catch (...) {
      return std::unexpected("invalid field number for -2");
    }
  }

  auto j_opt = ctx.get<std::string>("-j", "");
  if (!j_opt.empty()) {
    try {
      cfg.field1 = std::stoi(j_opt);
      cfg.field2 = cfg.field1;
    } catch (...) {
      return std::unexpected("invalid field number for -j");
    }
  }

  auto sep_opt = ctx.get<std::string>("-t", "");
  if (!sep_opt.empty()) {
    if (sep_opt.size() != 1) {
      return std::unexpected("separator must be a single character");
    }
    cfg.separator = sep_opt[0];
  }

  auto empty_opt = ctx.get<std::string>("--empty", "");
  if (empty_opt.empty()) {
    empty_opt = ctx.get<std::string>("-e", "");
  }
  cfg.empty_field = empty_opt;

  auto output_opt = ctx.get<std::string>("--output", "");
  if (output_opt.empty()) {
    output_opt = ctx.get<std::string>("-o", "");
  }
  cfg.output_format = output_opt;

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

  if (cfg.files.size() < 2) {
    return std::unexpected("missing operand after '" +
                           (cfg.files.empty() ? std::string() : cfg.files[0]) +
                           "'");
  }
  if (cfg.files.size() > 2) {
    return std::unexpected("extra operand '" + cfg.files[2] + "'");
  }

  return cfg;
}

auto read_lines(const std::string& filename)
    -> cp::Result<SmallVector<std::string, 1024>> {
  SmallVector<std::string, 1024> lines;

  if (filename == "-") {
    std::string line;
    while (std::getline(std::cin, line)) {
      lines.push_back(line);
    }
  } else {
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
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
      return std::unexpected("error reading from file");
    }
  }

  return lines;
}

auto run(const Config& cfg) -> int {
  const std::string& file1 = cfg.files[0];
  const std::string& file2 = cfg.files[1];

  auto lines1_result = read_lines(file1);
  if (!lines1_result) {
    cp::report_error(lines1_result, L"join");
    return 1;
  }

  auto lines2_result = read_lines(file2);
  if (!lines2_result) {
    cp::report_error(lines2_result, L"join");
    return 1;
  }

  const auto& lines1 = *lines1_result;
  const auto& lines2 = *lines2_result;

  // Build index for file2
  std::unordered_map<std::string, SmallVector<size_t, 16>> file2_index;
  for (size_t i = 0; i < lines2.size(); ++i) {
    std::string key = split_line(lines2[i], cfg.separator, cfg.field2);
    file2_index[key].push_back(i);
  }

  // Join lines
  for (const auto& line1 : lines1) {
    std::string key = split_line(line1, cfg.separator, cfg.field1);

    auto it = file2_index.find(key);
    if (it != file2_index.end()) {
      // Found matches
      for (size_t idx : it->second) {
        if (cfg.output_format.empty()) {
          // Default format: key + rest of line1 + rest of line2
          safePrint(key);

          // Get remaining fields from line1 (skip the key field at index
          // field1-1)
          auto fields1 = split_all_fields(line1, cfg.separator);
          for (size_t i = cfg.field1; i < fields1.size(); ++i) {
            safePrint(cfg.separator);
            safePrint(fields1[i]);
          }

          // Get all fields from line2 (skip the key field at index field2-1)
          auto fields2 = split_all_fields(lines2[idx], cfg.separator);
          for (size_t i = cfg.field2; i < fields2.size(); ++i) {
            safePrint(cfg.separator);
            safePrint(fields2[i]);
          }

          safePrintLn("");
        } else {
          // Custom output format (not fully implemented)
          safePrintLn(line1 + cfg.separator + lines2[idx]);
        }
      }
    }
  }

  return 0;
}

}  // namespace join_pipeline

REGISTER_COMMAND(
    join, "join", "join [OPTION]... FILE1 FILE2",
    "For each pair of input lines with identical join fields, write a line to\n"
    "standard output. The default join field is the first, delimited by "
    "blanks.\n"
    "\n"
    "Note: This is a basic implementation. Advanced features like -o format\n"
    "are not fully supported.",
    "  join file1 file2\n"
    "  join -j 1 file1 file2        # join on first field\n"
    "  join -t ',' file1 file2     # use comma as separator\n"
    "  join -1 2 -2 1 file1 file2  # join on field 2 of file1 and field 1 of "
    "file2",
    "comm(1), paste(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    JOIN_OPTIONS) {
  using namespace join_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"join");
    return 1;
  }

  return run(*cfg_result);
}
