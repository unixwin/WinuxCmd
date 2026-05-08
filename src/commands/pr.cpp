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
 *  - File: pr.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for pr.
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

auto constexpr PR_OPTIONS = std::array{
    OPTION("+PAGE", "", "begin printing with page PAGE [default 1]",
           STRING_TYPE),
    OPTION("-COLUMN", "", "produce COLUMN-column output", STRING_TYPE),
    OPTION("-a", "", "produce multi-column output", BOOL_TYPE),
    OPTION("-d", "", "double-space the output", BOOL_TYPE),
    OPTION("-e", "--expand", "expand input TABs", STRING_TYPE),
    OPTION("-f", "--form-feed", "use form feeds instead of newlines",
           BOOL_TYPE),
    OPTION("-h", "--header", "use a centered HEADER", STRING_TYPE),
    OPTION("-l", "--length", "set page length", STRING_TYPE),
    OPTION("-n", "--number-lines", "number lines", STRING_TYPE),
    OPTION("-o", "--indent", "offset each line", STRING_TYPE),
    OPTION("-r", "--no-file-warnings",
           "omit warning when a file cannot be opened", BOOL_TYPE),
    OPTION("-s", "--separator", "separate columns by characters", STRING_TYPE),
    OPTION("-t", "--omit-header", "omit page headers and trailers", BOOL_TYPE),
    OPTION("-T", "--omit-pagination", "omit page headers and trailers",
           BOOL_TYPE),
    OPTION("-w", "--width", "set page width", STRING_TYPE),
    OPTION("-W", "--page-width", "set page width (default 72)", STRING_TYPE)};

namespace pr_pipeline {
namespace cp = core::pipeline;

struct Config {
  int start_page = 1;
  int columns = 1;
  bool double_space = false;
  std::string expand_tabs;
  bool form_feed = false;
  std::string header;
  int page_length = 66;
  std::string number_lines;
  int indent = 0;
  bool no_file_warnings = false;
  std::string separator = "\t";
  bool omit_header = false;
  bool omit_pagination = false;
  int page_width = 72;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<PR_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  // Parse +PAGE option
  for (auto arg : ctx.positionals) {
    std::string arg_str(arg);
    if (arg_str.size() > 0 && arg_str[0] == '+') {
      try {
        cfg.start_page = std::stoi(arg_str.substr(1));
      } catch (...) {
        return std::unexpected("invalid page number");
      }
    } else {
      if (contains_wildcard(arg_str)) {
        auto glob_result = glob_expand(arg_str);
        if (glob_result.expanded) {
          for (const auto& file : glob_result.files) {
            cfg.files.push_back(wstring_to_utf8(file));
          }
          continue;
        }
      }
      cfg.files.push_back(arg_str);
    }
  }

  cfg.double_space = ctx.get<bool>("-d", false);
  cfg.form_feed =
      ctx.get<bool>("--form-feed", false) || ctx.get<bool>("-f", false);
  cfg.no_file_warnings =
      ctx.get<bool>("--no-file-warnings", false) || ctx.get<bool>("-r", false);
  cfg.omit_header =
      ctx.get<bool>("--omit-header", false) || ctx.get<bool>("-t", false);
  cfg.omit_pagination =
      ctx.get<bool>("--omit-pagination", false) || ctx.get<bool>("-T", false);

  auto expand_opt = ctx.get<std::string>("--expand", "");
  if (expand_opt.empty()) {
    expand_opt = ctx.get<std::string>("-e", "");
  }
  cfg.expand_tabs = expand_opt;

  auto header_opt = ctx.get<std::string>("--header", "");
  if (header_opt.empty()) {
    header_opt = ctx.get<std::string>("-h", "");
  }
  cfg.header = header_opt;

  auto length_opt = ctx.get<std::string>("--length", "");
  if (length_opt.empty()) {
    length_opt = ctx.get<std::string>("-l", "");
  }
  if (!length_opt.empty()) {
    try {
      cfg.page_length = std::stoi(length_opt);
    } catch (...) {
      return std::unexpected("invalid page length");
    }
  }

  auto number_opt = ctx.get<std::string>("--number-lines", "");
  if (number_opt.empty()) {
    number_opt = ctx.get<std::string>("-n", "");
  }
  cfg.number_lines = number_opt;

  auto indent_opt = ctx.get<std::string>("--indent", "");
  if (indent_opt.empty()) {
    indent_opt = ctx.get<std::string>("-o", "");
  }
  if (!indent_opt.empty()) {
    try {
      cfg.indent = std::stoi(indent_opt);
    } catch (...) {
      return std::unexpected("invalid indent value");
    }
  }

  auto sep_opt = ctx.get<std::string>("--separator", "");
  if (sep_opt.empty()) {
    sep_opt = ctx.get<std::string>("-s", "");
  }
  if (!sep_opt.empty()) {
    cfg.separator = sep_opt;
  }

  auto width_opt = ctx.get<std::string>("--width", "");
  if (width_opt.empty()) {
    width_opt = ctx.get<std::string>("-w", "");
  }
  if (!width_opt.empty()) {
    try {
      cfg.page_width = std::stoi(width_opt);
    } catch (...) {
      return std::unexpected("invalid page width");
    }
  }

  auto col_opt = ctx.get<std::string>("-COLUMN", "");
  if (!col_opt.empty()) {
    try {
      cfg.columns = std::stoi(col_opt);
    } catch (...) {
      return std::unexpected("invalid column count");
    }
  }

  auto pwidth_opt = ctx.get<std::string>("--page-width", "");
  if (!pwidth_opt.empty()) {
    try {
      cfg.page_width = std::stoi(pwidth_opt);
    } catch (...) {
      return std::unexpected("invalid page width");
    }
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
  // Note: This is a simplified implementation
  // Real pr would do complex multi-column layout and pagination

  SmallVector<std::string, 64> files = cfg.files;
  if (files.empty()) {
    files.push_back("-");
  }

  // Read all files
  SmallVector<std::string, 1024> all_lines;
  for (const auto& file : files) {
    auto lines_result = read_lines(file);
    if (!lines_result) {
      if (!cfg.no_file_warnings) {
        cp::report_error(lines_result, L"pr");
      }
      continue;
    }
    for (const auto& line : *lines_result) {
      all_lines.push_back(line);
    }
  }

  // Add indentation
  std::string indent_str(cfg.indent, ' ');

  // Output lines
  int line_num = 1;
  for (const auto& line : all_lines) {
    std::string output = indent_str;

    // Add line numbers if requested
    if (!cfg.number_lines.empty()) {
      char buf[32];
      snprintf(buf, sizeof(buf), "%6d  ", line_num++);
      output += buf;
    }

    output += line;

    safePrintLn(output);

    if (cfg.double_space) {
      safePrintLn("");
    }
  }

  return 0;
}

}  // namespace pr_pipeline

REGISTER_COMMAND(
    pr, "pr", "pr [OPTION]... [FILE]...",
    "Convert text files for printing.\n"
    "\n"
    "Note: This is a simplified implementation. Advanced features\n"
    "like multi-column layout and complex pagination are not fully\n"
    "supported.",
    "  pr file.txt\n"
    "  pr -n file.txt\n"
    "  pr -o 4 file.txt\n"
    "  pr -l 60 file.txt",
    "lp(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", PR_OPTIONS) {
  using namespace pr_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"pr");
    return 1;
  }

  return run(*cfg_result);
}
