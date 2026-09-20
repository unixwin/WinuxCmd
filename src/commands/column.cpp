// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [EXT]
    OPTION("-c", "--columns", "output is formatted for a display width",
           INT_TYPE),
    // [EXT]
    OPTION("-t", "--table",
           "determine the number of columns the input contains", BOOL_TYPE),
    // [EXT]
    OPTION("-s", "--separator", "specify the possible input item delimiters",
           STRING_TYPE),
    // [EXT]
    OPTION("", "--output-separator",
           "specify the columns separator for table output", STRING_TYPE),
    // [EXT]
    OPTION("-o", "--output-separator",
           "specify the columns separator for table output", STRING_TYPE),
    // [EXT]
    OPTION("", "--table-name", "specify the table name for JSON or XML output",
           STRING_TYPE),
    // [EXT]
    OPTION("-n", "--table-name",
           "specify the table name for JSON or XML output", STRING_TYPE),
    // [EXT]
    OPTION("", "--output-fields",
           "specify which columns to include in JSON or XML output",
           STRING_TYPE),
    // [EXT]
    OPTION("-x", "--output-fields",
           "specify which columns to include in JSON or XML output",
           STRING_TYPE),
    // [EXT]
    OPTION("", "--table-right", "columns to right align in table output",
           STRING_TYPE),
    // [EXT]
    // [DIFFERS] WinuxCmd previously mapped -r to --table-right (BOOL); GNU uses
    // -r for --tree
    // [EXT]
    OPTION("-R", "--table-right-columns",
           "columns to right align in table output", STRING_TYPE),
    // [EXT]
    // [GNU] -H/--table-hide: columns to hide in table output (STRING_TYPE per
    // GNU)
    OPTION("", "--table-hide", "columns to hide in table output", STRING_TYPE),
    // [GNU]
    OPTION("-H", "--table-hide", "columns to hide in table output",
           STRING_TYPE),
    // [EXT]
    OPTION("", "--table-empty", "don't use empty lines in table output",
           BOOL_TYPE),
    // [EXT]
    OPTION("-e", "--table-empty", "don't use empty lines in table output",
           BOOL_TYPE),
    // [EXT]
    OPTION("", "--table-no-trunc", "don't truncate text in table output",
           BOOL_TYPE),
    // [EXT]
    // [DIFFERS] WinuxCmd previously mapped -N to --table-no-trunc (BOOL); GNU
    // uses -N for --table-columns
    // [EXT]
    OPTION("", "--table-noescape",
           "don't escape newline, tab, backslash in table output", BOOL_TYPE),
    // [EXT]
    OPTION("-E", "--table-noescape",
           "don't escape newline, tab, backslash in table output", BOOL_TYPE),
    // [EXT]
    OPTION("", "--json", "use JSON output format for table", BOOL_TYPE),
    // [EXT]
    OPTION("-J", "--json", "use JSON output format for table", BOOL_TYPE),
    // [EXT]
    OPTION("", "--output-width", "maximum display width", INT_TYPE),
    // [EXT]
    // [DIFFERS] WinuxCmd previously mapped -O to --output-width (INT); GNU uses
    // -O for --table-order
    // [EXT]
    OPTION("", "--version", "output version information and exit", BOOL_TYPE),
    // [EXT]
    OPTION("-V", "--version", "output version information and exit", BOOL_TYPE),
    // [GNU] -d: suppress header printing
    OPTION("-d", "--table-noheadings", "don't print header in table output",
           BOOL_TYPE),
    // [GNU] -l: maximal number of input columns
    OPTION("-l", "--table-columns-limit", "maximal number of input columns",
           STRING_TYPE),
    // [GNU] -m: fill all available space
    OPTION("-m", "--table-maxout", "fill all available space", BOOL_TYPE),
    // [GNU] -T: truncate text in the columns when necessary
    OPTION("-T", "--table-truncate",
           "truncate text in the columns when necessary", STRING_TYPE),
    // [GNU] -W: wrap text in the columns when necessary
    OPTION("-W", "--table-wrap", "wrap text in the columns when necessary",
           STRING_TYPE),
    // [GNU] -L: don't ignore empty lines
    OPTION("-L", "--keep-empty-lines", "don't ignore empty lines", BOOL_TYPE),
    // [GNU] -C: define column properties
    OPTION("-C", "--table-column", "define column properties", STRING_TYPE),
    // [GNU] -r: column to use tree-like output for the table [DIFFERS: also
    // used as --table-right above]
    OPTION("-r", "--tree", "column to use tree-like output for the table",
           STRING_TYPE),
    // [GNU] -i: line ID to specify child-parent relation
    OPTION("-i", "--tree-id", "line ID to specify child-parent relation",
           STRING_TYPE),
    // [GNU] -p: parent to specify child-parent relation
    OPTION("-p", "--tree-parent", "parent to specify child-parent relation",
           STRING_TYPE),
    // [GNU] -O: specify order of output columns
    OPTION("-O", "--table-order", "specify order of output columns",
           STRING_TYPE),
    // [GNU] -N: comma separated column names
    OPTION("-N", "--table-columns", "comma separated column names",
           STRING_TYPE),
    // [EXT]
    OPTION("", "--help", "display this help and exit", BOOL_TYPE)};

namespace column_pipeline {
namespace cp = core::pipeline;

struct Config {
  int columns = 0;
  bool table_mode = false;
  std::string separator;
  std::string output_separator;
  std::string table_name;
  std::string output_fields;
  std::string table_right_columns;
  std::string table_hide_columns;  // [GNU] -H/--table-hide: columns to hide
  bool table_empty = false;
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
  if (cfg.separator.empty()) cfg.separator = ctx.get<std::string>("-s", "");
  cfg.output_separator = ctx.get<std::string>("--output-separator", "");
  if (cfg.output_separator.empty()) {
    cfg.output_separator = ctx.get<std::string>("-o", "");
  }

  cfg.table_name = ctx.get<std::string>("--table-name", "");
  if (cfg.table_name.empty()) {
    cfg.table_name = ctx.get<std::string>("-n", "");
  }

  cfg.output_fields = ctx.get<std::string>("--output-fields", "");
  if (cfg.output_fields.empty()) {
    cfg.output_fields = ctx.get<std::string>("-x", "");
  }

  // [GNU] -r/--tree is read via raw_args in run() for tree-like output
  // [DIFFERS]
  cfg.table_right_columns = ctx.get<std::string>("--table-right", "");
  if (cfg.table_right_columns.empty()) {
    cfg.table_right_columns = ctx.get<std::string>("-R", "");
  }
  // [GNU] -H/--table-hide: read as STRING (column list) per GNU
  cfg.table_hide_columns = ctx.get<std::string>("--table-hide", "");
  if (cfg.table_hide_columns.empty()) {
    cfg.table_hide_columns = ctx.get<std::string>("-H", "");
  }
  cfg.table_empty =
      ctx.get<bool>("--table-empty", false) || ctx.get<bool>("-e", false);
  cfg.table_noescape =
      ctx.get<bool>("--table-noescape", false) || ctx.get<bool>("-E", false);
  cfg.json_output =
      ctx.get<bool>("--json", false) || ctx.get<bool>("-J", false);
  cfg.output_width = ctx.get<int>("--columns", 0);

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
  auto input_open_error = [](std::string_view path) -> std::string {
    std::error_code ec;
    if (std::filesystem::is_directory(std::filesystem::u8path(path), ec) &&
        !ec) {
      return std::string("cannot open '") + std::string(path) +
             "' for reading: Is a directory";
    }

    return std::string("cannot open '") + std::string(path) +
           "' for reading: No such file or directory";
  };

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
      return std::unexpected(input_open_error(filename));
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

auto split_records(std::string_view content) -> std::vector<std::string> {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < content.size()) {
    size_t end = content.find('\n', start);
    if (end == std::string_view::npos) {
      std::string line(content.substr(start));
      if (!line.empty() && line.back() == '\r') line.pop_back();
      if (!line.empty()) lines.push_back(std::move(line));
      break;
    }

    std::string line(content.substr(start, end - start));
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(std::move(line));
    start = end + 1;
  }
  return lines;
}

auto split_table_line(const std::string& line, const Config& cfg)
    -> std::vector<std::string> {
  std::vector<std::string> row;

  if (cfg.separator.empty()) {
    size_t pos = 0;
    while (pos < line.size()) {
      pos = line.find_first_not_of(" \t", pos);
      if (pos == std::string::npos) break;
      size_t end = line.find_first_of(" \t", pos);
      if (end == std::string::npos) {
        row.push_back(line.substr(pos));
        break;
      }
      row.push_back(line.substr(pos, end - pos));
      pos = end + 1;
    }
    return row;
  }

  size_t start = 0;
  while (start <= line.size()) {
    size_t end = line.find_first_of(cfg.separator, start);
    if (end == std::string::npos) {
      row.push_back(line.substr(start));
      break;
    }
    row.push_back(line.substr(start, end - start));
    start = end + 1;
  }
  return row;
}

auto parse_column_set(std::string_view spec) -> std::set<size_t> {
  std::set<size_t> columns;
  size_t pos = 0;
  while (pos < spec.size()) {
    size_t comma = spec.find(',', pos);
    std::string_view token = comma == std::string_view::npos
                                 ? spec.substr(pos)
                                 : spec.substr(pos, comma - pos);
    size_t value = 0;
    auto [ptr, ec] =
        std::from_chars(token.data(), token.data() + token.size(), value);
    if (ec == std::errc() && ptr == token.data() + token.size() && value > 0) {
      columns.insert(value);
    }
    if (comma == std::string_view::npos) break;
    pos = comma + 1;
  }
  return columns;
}

auto json_escape(std::string_view text) -> std::string {
  std::string out;
  for (unsigned char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(static_cast<char>(c));
        break;
    }
  }
  return out;
}

auto table_escape(std::string_view text) -> std::string {
  std::string out;
  for (unsigned char c : text) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        out.push_back(static_cast<char>(c));
        break;
    }
  }
  return out;
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
    std::vector<std::string> lines = split_records(all_content);
    if (lines.empty()) {
      return 0;
    }

    std::vector<std::vector<std::string>> table;
    size_t max_cols = 0;

    for (const auto& line : lines) {
      std::vector<std::string> row = split_table_line(line, cfg);

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

    const std::set<size_t> include_fields =
        cfg.output_fields.empty() ? std::set<size_t>{}
                                  : parse_column_set(cfg.output_fields);

    if (cfg.json_output) {
      const std::string table_name =
          cfg.table_name.empty() ? "table" : cfg.table_name;
      safePrintLn("{");
      safePrintLn("  \"" + json_escape(table_name) + "\": [");
      for (size_t row_idx = (!cfg.table_hide_columns.empty() ? 1 : 0);
           row_idx < table.size(); ++row_idx) {
        const auto& row = table[row_idx];
        safePrint("    {");
        for (size_t col_idx = 0; col_idx < row.size(); ++col_idx) {
          if (!include_fields.empty() &&
              !include_fields.contains(col_idx + 1)) {
            continue;
          }
          if (col_idx > 0) safePrint(", ");
          std::string key = "col" + std::to_string(col_idx + 1);
          if (!table.empty() && col_idx < table[0].size()) {
            key = table[0][col_idx].empty() ? key : table[0][col_idx];
          }
          safePrint("\"" + json_escape(key) + "\": \"" +
                    json_escape(row[col_idx]) + "\"");
        }
        safePrintLn("}" + std::string(row_idx + 1 < table.size() ? "," : ""));
      }
      safePrintLn("  ]");
      safePrintLn("}");
      return 0;
    }

    const std::string output_separator =
        cfg.output_separator.empty() ? "  " : cfg.output_separator;
    const std::set<size_t> right_columns =
        parse_column_set(cfg.table_right_columns);

    for (size_t row_idx = 0; row_idx < table.size(); ++row_idx) {
      if (!cfg.table_hide_columns.empty() && row_idx == 0) {
        continue;
      }

      const auto& row = table[row_idx];
      std::string line_output;

      for (size_t col_idx = 0; col_idx < row.size(); ++col_idx) {
        if (!include_fields.empty() && !include_fields.contains(col_idx + 1)) {
          continue;
        }
        const size_t width = (col_idx < col_widths.size())
                                 ? col_widths[col_idx]
                                 : row[col_idx].size();
        const bool right_align = right_columns.contains(
            col_idx + 1);  // [DIFFERS] table_right bool removed

        std::string cell = row[col_idx];
        if (!cfg.table_noescape) {
          cell = table_escape(cell);
        }
        // [DIFFERS] table_no_trunc option removed; truncation now always
        // applies
        if (cell.size() > width) {
          cell = cell.substr(0, width);
        }

        // Check if this is the last included column
        bool is_last = true;
        for (size_t next = col_idx + 1; next < row.size(); ++next) {
          if (include_fields.empty() || include_fields.contains(next + 1)) {
            is_last = false;
            break;
          }
        }

        if (right_align && cell.size() < width) {
          line_output.append(width - cell.size(), ' ');
        }
        line_output += cell;
        if (!right_align && !is_last && cell.size() < width) {
          line_output.append(width - cell.size(), ' ');
        }
        if (!is_last) {
          line_output += output_separator;
        }
      }

      if (cfg.output_width > 0 &&
          static_cast<int>(line_output.size()) > cfg.output_width) {
        line_output = line_output.substr(0, cfg.output_width);
      }

      safePrintLn(line_output);
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

  // Handle --version/--help
  if (ctx.get<bool>("--version", false) || ctx.get<bool>("-V", false)) {
    safePrintLn("column (WinuxCmd) 1.0");
    return 0;
  }
  if (ctx.get<bool>("--help", false)) {
    safePrintLn("column (WinuxCmd) 1.0");
    safePrintLn("Columnate lists.");
    safePrintLn("");
    safePrintLn("Usage: column [options] [file...]");
    safePrintLn(
        "  -c, --columns WIDTH  output is formatted for a display width");
    safePrintLn(
        "  -t, --table          determine the number of columns the input "
        "contains");
    safePrintLn(
        "  -s, --separator SEP  specify the possible input item delimiters");
    safePrintLn("  -J, --json           use JSON output format for table");
    safePrintLn("  -N, --table-no-trunc don't truncate text in table output");
    safePrintLn("  -H, --table-hide     don't print header in table output");
    safePrintLn("  --version            output version information and exit");
    safePrintLn("  --help               display this help and exit");
    return 0;
  }

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"column");
    return 1;
  }

  return run(*cfg_result);
}
