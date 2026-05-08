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
 *  - File: column.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for column.
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

auto constexpr COLUMN_OPTIONS = std::array{
    OPTION("-c", "--columns", "output is formatted for a display width",
           INT_TYPE),
    OPTION("-t", "--table",
           "determine the number of columns the input contains", BOOL_TYPE),
    OPTION("-s", "--separator", "specify the possible input item delimiters",
           STRING_TYPE),
    OPTION("-o", "output-separator",
           "specify the columns separator for table output", STRING_TYPE),
    OPTION("-n", "table-name", "specify the table name for JSON or XML output",
           STRING_TYPE),
    OPTION("-x", "output-fields",
           "specify which columns to include in JSON or XML output",
           STRING_TYPE),
    OPTION("-r", "table-right", "right align text in table columns", BOOL_TYPE),
    OPTION("-R", "table-right-columns",
           "columns to right align in table output", STRING_TYPE),
    OPTION("-H", "table-hide", "don't print header in table output", BOOL_TYPE),
    OPTION("-e", "table-empty", "don't use empty lines in table output",
           BOOL_TYPE),
    OPTION("-N", "table-no-trunc", "don't truncate text in table output",
           BOOL_TYPE),
    OPTION("-E", "table-noescape",
           "don't escape newline, tab, backslash in table output", BOOL_TYPE),
    OPTION("-J", "json", "use JSON output format for table", BOOL_TYPE),
    OPTION("-O", "output-width", "maximum display width", INT_TYPE),
    OPTION("-V", "version", "output version information and exit", BOOL_TYPE),
    OPTION("-h", "help", "display this help and exit", BOOL_TYPE)};

namespace column_pipeline {
namespace cp = core::pipeline;

struct Config {
  int columns = 0;
  bool table_mode = false;
  std::string separator;
  std::string output_separator;
  std::string table_name;
  std::string output_fields;
  bool table_right = false;
  std::string table_right_columns;
  bool table_hide = false;
  bool table_empty = false;
  bool table_no_trunc = false;
  bool table_noescape = false;
  bool json_output = false;
  int output_width = 0;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<COLUMN_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.columns = ctx.get<int>("--columns", 0);
  cfg.table_mode =
      ctx.get<bool>("--table", false) || ctx.get<bool>("-t", false);
  cfg.separator = ctx.get<std::string>("--separator", "");
  cfg.output_separator = ctx.get<std::string>("-o", "");
  cfg.table_name = ctx.get<std::string>("-n", "");
  cfg.output_fields = ctx.get<std::string>("-x", "");
  cfg.table_right = ctx.get<bool>("-r", false);
  cfg.table_right_columns = ctx.get<std::string>("-R", "");
  cfg.table_hide = ctx.get<bool>("-H", false);
  cfg.table_empty = ctx.get<bool>("-e", false);
  cfg.table_no_trunc = ctx.get<bool>("-N", false);
  cfg.table_noescape = ctx.get<bool>("-E", false);
  cfg.json_output = ctx.get<bool>("-J", false);
  cfg.output_width = ctx.get<int>("-O", 0);

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

  if (filename == "-" || filename.empty()) {
    // Read from stdin
    std::string line;
    while (std::getline(std::cin, line)) {
      content += line;
      content += '\n';
    }
    if (std::cin.bad() && !std::cin.eof()) {
      return std::unexpected("error reading from standard input");
    }
  } else {
    // Read from file
    std::ifstream file(filename, std::ios::binary);
    if (!file.is_open()) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }

    // Get file size
    file.seekg(0, std::ios::end);
    size_t file_size = file.tellg();
    file.seekg(0, std::ios::beg);

    if (file_size > 0) {
      content.resize(file_size);
      if (!file.read(&content[0], file_size)) {
        return std::unexpected("error reading from file");
      }
    }

    if (file.bad() && !file.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  return content;
}

auto run(const Config& cfg) -> int {
  std::string all_content;

  for (const auto& file : cfg.files) {
    auto content_result = read_input(file);
    if (!content_result) {
      cp::report_error(content_result, L"column");
      return 1;
    }
    all_content += *content_result;
  }

  if (cfg.table_mode) {
    // Table mode - format as a table
    // Use heap allocation to avoid stack overflow
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < all_content.size()) {
      size_t end = all_content.find('\n', start);
      if (end == std::string::npos) {
        if (!all_content.substr(start).empty()) {
          lines.push_back(all_content.substr(start));
        }
        break;
      }
      lines.push_back(all_content.substr(start, end - start));
      start = end + 1;
    }

    if (lines.empty()) {
      return 0;
    }

    // Determine separator
    char sep = '\t';  // Default to tab
    if (!cfg.separator.empty()) {
      sep = cfg.separator[0];
    }

    // Parse all lines into columns
    std::vector<std::vector<std::string>> table;
    size_t max_cols = 0;

    for (const auto& line : lines) {
      std::vector<std::string> row;
      size_t col_start = 0;

      while (col_start < line.size()) {
        size_t col_end = line.find(sep, col_start);
        if (col_end == std::string::npos || col_end == col_start) {
          if (col_start < line.size()) {
            row.push_back(line.substr(col_start));
          }
          break;
        }
        if (col_end > col_start) {
          row.push_back(line.substr(col_start, col_end - col_start));
        }
        col_start = col_end + 1;
      }

      if (row.size() > max_cols) {
        max_cols = row.size();
      }

      if (!cfg.table_empty || !row.empty()) {
        table.push_back(std::move(row));
      }
    }

    // Calculate column widths
    std::vector<size_t> col_widths;
    if (max_cols > 0) {
      col_widths.resize(max_cols, 0);
      for (const auto& row : table) {
        for (size_t i = 0; i < row.size() && i < col_widths.size(); ++i) {
          if (row[i].size() > col_widths[i]) {
            col_widths[i] = row[i].size();
          }
        }
      }
    }

    // Print table
    for (size_t row_idx = 0; row_idx < table.size(); ++row_idx) {
      if (cfg.table_hide && row_idx == 0) {
        continue;  // Skip header
      }

      const auto& row = table[row_idx];
      for (size_t col_idx = 0; col_idx < row.size(); ++col_idx) {
        if (col_idx > 0) {
          safePrint(cfg.output_separator.empty() ? " " : cfg.output_separator);
        }

        size_t width = (col_idx < col_widths.size()) ? col_widths[col_idx]
                                                     : row[col_idx].size();

        if (cfg.table_right) {
          // Right align
          for (size_t i = 0; i < width - row[col_idx].size(); ++i) {
            safePrint(' ');
          }
          safePrint(row[col_idx]);
        } else {
          // Left align
          safePrint(row[col_idx]);
          for (size_t i = 0; i < width - row[col_idx].size(); ++i) {
            safePrint(' ');
          }
        }
      }
      safePrintLn("");
    }
  } else {
    // Simple column output mode
    safePrint(all_content);
  }

  return 0;
}

}  // namespace column_pipeline

REGISTER_COMMAND(column, "column", "column [options] [file...]",
                 "Columnate lists.\n"
                 "\n"
                 "The column utility formats its input into multiple columns.\n"
                 "Rows are filled before columns. Input is taken from file,\n"
                 "or from standard input by default.",
                 "  column -t -s , data.csv\n"
                 "  ls -l | column -t\n"
                 "  ps aux | column -t",
                 "colrm(1), ls(1), paste(1), sort(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", COLUMN_OPTIONS) {
  using namespace column_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"column");
    return 1;
  }

  return run(*cfg_result);
}
