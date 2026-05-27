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
    OPTION("-W", "--page-width", "set page width (default 72)", STRING_TYPE),
    OPTION("-b", "--balance-columns",
           "balance columns on the last page", BOOL_TYPE),
    OPTION("-c", "--show-control-chars",
           "use hat notation (^G) and octal backslash notation", BOOL_TYPE),
    OPTION("-D", "--date-format",
           "use FORMAT for the date in the header", STRING_TYPE),
    OPTION("-F", "-f",
           "use form feeds instead of newlines (same as -f)", BOOL_TYPE),
    OPTION("-i", "--output-tabs",
           "replace spaces with TABs where possible", STRING_TYPE),
    OPTION("-J", "--join-lines",
           "merge full lines (ignore --column warnings)", BOOL_TYPE),
    OPTION("-m", "--merge",
           "print all files in parallel, one in each column", BOOL_TYPE),
    OPTION("-N", "--first-line-number",
           "start counting with NUMBER at line 1 of first page",
           STRING_TYPE),
    OPTION("-S", "--sep-string",
           "separate columns by STRING (default single space)",
           STRING_TYPE),
    OPTION("-v", "--show-nonprinting",
           "use octal backslash notation for non-printing characters",
           BOOL_TYPE)};

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
  bool balance_columns = false;
  bool show_control_chars = false;
  std::string date_format;
  bool form_feed_ff = false;
  std::string output_tabs;
  bool join_lines = false;
  bool merge = false;
  std::string first_line_number;
  std::string sep_string;
  bool show_nonprinting = false;
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

  // New options
  cfg.balance_columns =
      ctx.get<bool>("--balance-columns", false) || ctx.get<bool>("-b", false);
  cfg.show_control_chars =
      ctx.get<bool>("--show-control-chars", false) || ctx.get<bool>("-c", false);
  cfg.join_lines =
      ctx.get<bool>("--join-lines", false) || ctx.get<bool>("-J", false);
  cfg.merge = ctx.get<bool>("--merge", false) || ctx.get<bool>("-m", false);
  cfg.show_nonprinting =
      ctx.get<bool>("--show-nonprinting", false) || ctx.get<bool>("-v", false);

  auto date_fmt = ctx.get<std::string>("--date-format", "");
  if (date_fmt.empty()) {
    date_fmt = ctx.get<std::string>("-D", "");
  }
  cfg.date_format = date_fmt;

  auto output_tabs_opt = ctx.get<std::string>("--output-tabs", "");
  if (output_tabs_opt.empty()) {
    output_tabs_opt = ctx.get<std::string>("-i", "");
  }
  cfg.output_tabs = output_tabs_opt;

  auto fln_opt = ctx.get<std::string>("--first-line-number", "");
  if (fln_opt.empty()) {
    fln_opt = ctx.get<std::string>("-N", "");
  }
  cfg.first_line_number = fln_opt;

  auto sep_str = ctx.get<std::string>("--sep-string", "");
  if (sep_str.empty()) {
    sep_str = ctx.get<std::string>("-S", "");
  }
  if (!sep_str.empty()) {
    cfg.sep_string = sep_str;
  }

  // -F is the same as -f (form feed)
  if (ctx.get<bool>("-F", false)) {
    cfg.form_feed = true;
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

// Expand tabs to spaces
auto expand_tabs(const std::string& line, int tab_width) -> std::string {
  std::string result;
  int col = 0;
  for (char c : line) {
    if (c == '\t') {
      int spaces = tab_width - (col % tab_width);
      result.append(spaces, ' ');
      col += spaces;
    } else {
      result += c;
      ++col;
    }
  }
  return result;
}

// Show control characters using hat notation (^X)
auto show_control_chars_hat(const std::string& line) -> std::string {
  std::string result;
  for (unsigned char c : line) {
    if (c < 32 && c != '\t' && c != '\n') {
      result += '^';
      result += static_cast<char>(c + '@');
    } else if (c == 127) {
      result += "^?";
    } else {
      result += static_cast<char>(c);
    }
  }
  return result;
}

// Show non-printing characters using octal backslash notation
auto show_nonprinting_octal(const std::string& line) -> std::string {
  std::string result;
  for (unsigned char c : line) {
    if (c < 32 || c > 126) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\%03o", c);
      result += buf;
    } else {
      result += static_cast<char>(c);
    }
  }
  return result;
}

// Replace spaces with tabs where possible
auto replace_spaces_with_tabs(const std::string& line, int tab_width)
    -> std::string {
  std::string result;
  int space_count = 0;
  int col = 0;
  for (char c : line) {
    if (c == ' ') {
      ++space_count;
      ++col;
      if (space_count == tab_width) {
        result += '\t';
        space_count = 0;
      }
    } else {
      // Flush remaining spaces
      if (space_count > 0) {
        result.append(space_count, ' ');
        space_count = 0;
      }
      result += c;
      ++col;
    }
  }
  if (space_count > 0) {
    result.append(space_count, ' ');
  }
  return result;
}

// Format a date header
auto format_date_header(const std::string& date_format) -> std::string {
  if (date_format.empty()) {
    // Default: "May 27 10:30 2026"
    SYSTEMTIME st;
    GetLocalTime(&st);
    const char* months[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                            "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %02d %02d:%02d %04d",
             months[st.wMonth - 1], st.wDay, st.wHour, st.wMinute,
             st.wYear);
    return buf;
  }
  // Custom format not fully implemented, return default
  SYSTEMTIME st;
  GetLocalTime(&st);
  char buf[64];
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
  return buf;
}

// Build column separator
auto get_separator(const Config& cfg) -> std::string {
  if (!cfg.sep_string.empty()) return cfg.sep_string;
  if (!cfg.separator.empty()) return cfg.separator;
  return "\t";
}

// Print page header
auto print_page_header(const Config& cfg, int page_num, const std::string& filename) -> void {
  if (cfg.omit_header || cfg.omit_pagination) return;

  std::string date_str = format_date_header(cfg.date_format);
  std::string header_text = cfg.header.empty() ? filename : cfg.header;

  // Build header line: date  header  page
  char page_str[32];
  snprintf(page_str, sizeof(page_str), "Page %d", page_num);

  // Center the header
  int header_len = static_cast<int>(date_str.size()) +
                   static_cast<int>(header_text.size()) +
                   static_cast<int>(strlen(page_str)) + 4;
  int pad = (cfg.page_width - header_len) / 2;
  if (pad < 1) pad = 1;

  std::string output = date_str + std::string(pad, ' ') + header_text +
                       std::string(pad, ' ') + page_str;
  safePrintLn(output);
  safePrintLn("");  // Blank line after header
}

// Print page trailer (blank lines for form feed)
auto print_page_trailer(const Config& cfg) -> void {
  if (cfg.omit_pagination) return;
  if (cfg.form_feed || cfg.form_feed_ff) {
    safePrint("\f");
  }
}

auto run(const Config& cfg) -> int {
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

  // Join lines if requested (-J/--join-lines)
  if (cfg.join_lines && all_lines.size() > 1) {
    SmallVector<std::string, 1024> joined_lines;
    for (size_t i = 0; i < all_lines.size(); ++i) {
      if (i + 1 < all_lines.size() && !all_lines[i].empty() &&
          all_lines[i].back() != '\n') {
        // Join with next line
        joined_lines.push_back(all_lines[i] + all_lines[i + 1]);
        ++i;  // Skip next line
      } else {
        joined_lines.push_back(all_lines[i]);
      }
    }
    all_lines = std::move(joined_lines);
  }

  // Merge mode: print all files in parallel columns
  if (cfg.merge) {
    // Read each file separately for merge mode
    SmallVector<SmallVector<std::string, 1024>, 16> file_lines;
    size_t max_lines = 0;
    for (const auto& file : files) {
      auto lines_result = read_lines(file);
      if (!lines_result) {
        if (!cfg.no_file_warnings) {
          cp::report_error(lines_result, L"pr");
        }
        file_lines.push_back({});
        continue;
      }
      file_lines.push_back(*lines_result);
      if (lines_result->size() > max_lines) {
        max_lines = lines_result->size();
      }
    }

    std::string sep = get_separator(cfg);
    std::string indent_str(cfg.indent, ' ');

    for (size_t i = 0; i < max_lines; ++i) {
      std::string output = indent_str;
      for (size_t f = 0; f < file_lines.size(); ++f) {
        if (f > 0) output += sep;
        if (i < file_lines[f].size()) {
          output += file_lines[f][i];
        }
      }
      safePrintLn(output);
    }
    return 0;
  }

  // Apply start_page: skip lines before the start page
  int lines_per_page = cfg.page_length - 10;  // Reserve lines for header/trailer
  if (cfg.omit_header || cfg.omit_pagination) {
    lines_per_page = cfg.page_length;
  }

  // Process lines with all options
  std::string indent_str(cfg.indent, ' ');
  std::string sep = get_separator(cfg);
  int line_num = cfg.first_line_number.empty() ? 1 : std::stoi(cfg.first_line_number);
  int page_num = 1;
  int lines_on_page = 0;
  bool in_page = false;

  // Track start_page
  bool started = (cfg.start_page <= 1);

  // Process with multi-column support
  if (cfg.columns > 1) {
    // Distribute lines across columns
    size_t total_lines = all_lines.size();
    size_t lines_per_col = (total_lines + cfg.columns - 1) / cfg.columns;

    // Balance columns on last page
    if (cfg.balance_columns) {
      lines_per_col = (total_lines + cfg.columns - 1) / cfg.columns;
    }

    for (size_t row = 0; row < lines_per_col; ++row) {
      if (!in_page && started) {
        print_page_header(cfg, page_num, files.empty() ? "" : files[0]);
        in_page = true;
      }

      std::string output = indent_str;
      for (int col = 0; col < cfg.columns; ++col) {
        if (col > 0) output += sep;
        size_t idx = col * lines_per_col + row;
        if (idx < total_lines) {
          std::string line = all_lines[idx];

          // Apply expand_tabs
          if (!cfg.expand_tabs.empty()) {
            int tab_width = 8;
            try {
              tab_width = std::stoi(cfg.expand_tabs);
            } catch (...) {
            }
            line = expand_tabs(line, tab_width);
          }

          // Apply show_control_chars
          if (cfg.show_control_chars) {
            line = show_control_chars_hat(line);
          }

          // Apply show_nonprinting
          if (cfg.show_nonprinting) {
            line = show_nonprinting_octal(line);
          }

          // Apply output_tabs
          if (!cfg.output_tabs.empty()) {
            int tab_width = 8;
            try {
              tab_width = std::stoi(cfg.output_tabs);
            } catch (...) {
            }
            line = replace_spaces_with_tabs(line, tab_width);
          }

          output += line;
        }
      }

      // Add line numbers
      if (!cfg.number_lines.empty()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%6d  ", line_num++);
        output = indent_str + buf + output.substr(indent_str.size());
      }

      safePrintLn(output);

      if (cfg.double_space) {
        safePrintLn("");
      }

      ++lines_on_page;
      if (lines_on_page >= lines_per_page && !cfg.omit_pagination) {
        print_page_trailer(cfg);
        ++page_num;
        lines_on_page = 0;
        in_page = false;
      }
    }
  } else {
    // Single column mode
    for (const auto& line : all_lines) {
      if (!started) {
        // Skip lines until we reach start_page
        ++lines_on_page;
        if (lines_on_page >= lines_per_page) {
          lines_on_page = 0;
          ++page_num;
          if (page_num >= cfg.start_page) {
            started = true;
          }
        }
        continue;
      }

      if (!in_page) {
        print_page_header(cfg, page_num, files.empty() ? "" : files[0]);
        in_page = true;
      }

      std::string processed = line;

      // Apply expand_tabs
      if (!cfg.expand_tabs.empty()) {
        int tab_width = 8;
        try {
          tab_width = std::stoi(cfg.expand_tabs);
        } catch (...) {
        }
        processed = expand_tabs(processed, tab_width);
      }

      // Apply show_control_chars
      if (cfg.show_control_chars) {
        processed = show_control_chars_hat(processed);
      }

      // Apply show_nonprinting
      if (cfg.show_nonprinting) {
        processed = show_nonprinting_octal(processed);
      }

      // Apply output_tabs
      if (!cfg.output_tabs.empty()) {
        int tab_width = 8;
        try {
          tab_width = std::stoi(cfg.output_tabs);
        } catch (...) {
        }
        processed = replace_spaces_with_tabs(processed, tab_width);
      }

      std::string output = indent_str;

      // Add line numbers
      if (!cfg.number_lines.empty()) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%6d  ", line_num++);
        output += buf;
      }

      output += processed;

      safePrintLn(output);

      if (cfg.double_space) {
        safePrintLn("");
      }

      ++lines_on_page;
      if (lines_on_page >= lines_per_page) {
        print_page_trailer(cfg);
        ++page_num;
        lines_on_page = 0;
        in_page = false;
      }
    }
  }

  // Final page trailer
  if (in_page && !cfg.omit_pagination) {
    print_page_trailer(cfg);
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
