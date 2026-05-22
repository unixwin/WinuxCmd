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
 *  - File: comm.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for comm.
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

auto constexpr COMM_OPTIONS = std::array{
    OPTION("-1", "", "suppress column 1 (lines unique to FILE1)", BOOL_TYPE),
    OPTION("-2", "", "suppress column 2 (lines unique to FILE2)", BOOL_TYPE),
    OPTION("-3", "", "suppress column 3 (lines that appear in both files)",
           BOOL_TYPE),
    OPTION("", "--check-order", "check that the input is correctly sorted",
           BOOL_TYPE),
    OPTION("", "--nocheck-order",
           "do not check that the input is correctly sorted", BOOL_TYPE),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline"),
    OPTION("", "--output-delimiter", "separate columns with STR",
           OPTIONAL_STRING_TYPE),
    OPTION("", "--total", "output a summary")};

namespace comm_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool suppress_col1 = false;
  bool suppress_col2 = false;
  bool suppress_col3 = false;
  bool check_order = false;
  bool total = false;
  bool zero_terminated = false;
  std::string output_delimiter = "\t";
  SmallVector<std::string, 64> files;
};

void add_file_arg(Config& cfg, const std::string& file_arg) {
  if (contains_wildcard(file_arg)) {
    auto glob_result = glob_expand(file_arg);
    if (glob_result.expanded) {
      for (const auto& file : glob_result.files) {
        cfg.files.push_back(wstring_to_utf8(file));
      }
      return;
    }
  }
  cfg.files.push_back(file_arg);
}

auto build_config(const CommandContext<COMM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  bool end_of_options = false;
  for (size_t i = 0; i < ctx.raw_args.size(); ++i) {
    std::string arg(ctx.raw_args[i]);

    if (!end_of_options && arg == "--") {
      end_of_options = true;
      continue;
    }

    if (!end_of_options && arg.starts_with("--output-delimiter=")) {
      std::string value = arg.substr(std::string("--output-delimiter=").size());
      cfg.output_delimiter = value.empty() ? std::string(1, '\0') : value;
      continue;
    }

    if (!end_of_options && arg == "--output-delimiter") {
      if (i + 1 >= ctx.raw_args.size()) {
        return std::unexpected(
            "option '--output-delimiter' requires an argument");
      }
      std::string value(ctx.raw_args[++i]);
      cfg.output_delimiter = value.empty() ? std::string(1, '\0') : value;
      continue;
    }

    if (!end_of_options && arg.starts_with("--")) {
      if (arg == "--check-order") {
        cfg.check_order = true;
        continue;
      }
      if (arg == "--nocheck-order") {
        cfg.check_order = false;
        continue;
      }
      if (arg == "--total") {
        cfg.total = true;
        continue;
      }
      if (arg == "--zero-terminated") {
        cfg.zero_terminated = true;
        continue;
      }
    }

    if (!end_of_options && arg.size() >= 2 && arg[0] == '-' && arg[1] != '-') {
      bool all_flags = true;
      for (size_t pos = 1; pos < arg.size(); ++pos) {
        switch (arg[pos]) {
          case '1':
            cfg.suppress_col1 = true;
            break;
          case '2':
            cfg.suppress_col2 = true;
            break;
          case '3':
            cfg.suppress_col3 = true;
            break;
          case 'z':
            cfg.zero_terminated = true;
            break;
          default:
            all_flags = false;
            break;
        }
        if (!all_flags) break;
      }
      if (all_flags) continue;
    }

    add_file_arg(cfg, arg);
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

// Read sorted lines from file
auto read_lines(const std::string& filename, char delimiter)
    -> cp::Result<SmallVector<std::string, 1024>> {
  SmallVector<std::string, 1024> lines;

  if (filename == "-") {
    // Read from stdin
    std::string content;
    {
      std::ostringstream oss;
      oss << std::cin.rdbuf();
      content = oss.str();
    }
    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        lines.emplace_back(content.substr(start, i - start));
        start = i + 1;
      }
    }
    if (start < content.size()) {
      lines.emplace_back(content.substr(start));
    }
  } else {
    // Read from file
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        std::string line = content.substr(start, i - start);
        // Skip UTF-8 BOM if present at the beginning of the first line
        if (lines.empty() && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
          line = line.substr(3);
        }
        lines.push_back(line);
        start = i + 1;
      }
    }
    if (start < content.size()) {
      std::string line = content.substr(start);
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

auto check_sorted(const SmallVector<std::string, 1024>& lines)
    -> cp::Result<void> {
  for (size_t i = 1; i < lines.size(); ++i) {
    if (lines[i - 1] > lines[i]) {
      return std::unexpected("input is not in sorted order");
    }
  }
  return {};
}

void print_record(const std::string& text, char delimiter) {
  safePrint(text);
  safePrint(std::string_view(&delimiter, 1));
}

void append_column_prefix(std::string& output, const Config& cfg, int column) {
  if (column >= 2 && !cfg.suppress_col1) {
    output += cfg.output_delimiter;
  }
  if (column >= 3 && !cfg.suppress_col2) {
    output += cfg.output_delimiter;
  }
}

auto run(const Config& cfg) -> int {
  const char record_delim = cfg.zero_terminated ? '\0' : '\n';
  const std::string& file1 = cfg.files[0];
  const std::string& file2 = cfg.files[1];

  auto lines1_result = read_lines(file1, record_delim);
  if (!lines1_result) {
    cp::report_error(lines1_result, L"comm");
    return 1;
  }

  auto lines2_result = read_lines(file2, record_delim);
  if (!lines2_result) {
    cp::report_error(lines2_result, L"comm");
    return 1;
  }

  const auto& lines1 = *lines1_result;
  const auto& lines2 = *lines2_result;

  if (cfg.check_order) {
    auto sorted1 = check_sorted(lines1);
    if (!sorted1) {
      cp::report_error(sorted1, L"comm");
      return 1;
    }
    auto sorted2 = check_sorted(lines2);
    if (!sorted2) {
      cp::report_error(sorted2, L"comm");
      return 1;
    }
  }

  // Merge and compare
  size_t i = 0, j = 0;
  size_t count_col1 = 0, count_col2 = 0, count_col3 = 0;
  while (i < lines1.size() || j < lines2.size()) {
    int cmp_result = 0;

    if (i >= lines1.size()) {
      cmp_result = 1;
    } else if (j >= lines2.size()) {
      cmp_result = -1;
    } else {
      cmp_result = lines1[i].compare(lines2[j]);
    }

    if (cmp_result < 0) {
      // Line only in file1
      ++count_col1;
      if (!cfg.suppress_col1) {
        print_record(lines1[i], record_delim);
      }
      i++;
    } else if (cmp_result > 0) {
      // Line only in file2
      ++count_col2;
      if (!cfg.suppress_col2) {
        std::string output;
        append_column_prefix(output, cfg, 2);
        output += lines2[j];
        print_record(output, record_delim);
      }
      j++;
    } else {
      // Line in both files
      ++count_col3;
      if (!cfg.suppress_col3) {
        std::string output;
        append_column_prefix(output, cfg, 3);
        output += lines1[i];
        print_record(output, record_delim);
      }
      i++;
      j++;
    }
  }

  if (cfg.total) {
    std::string output = std::to_string(count_col1);
    output += cfg.output_delimiter;
    output += std::to_string(count_col2);
    output += cfg.output_delimiter;
    output += std::to_string(count_col3);
    output += cfg.output_delimiter;
    output += "total";
    print_record(output, record_delim);
  }

  return 0;
}

}  // namespace comm_pipeline

REGISTER_COMMAND(
    comm, "comm", "comm [OPTION]... FILE1 FILE2",
    "Compare sorted files line by line.\n"
    "\n"
    "With no options, produce three-column output. Column one contains\n"
    "lines unique to FILE1, column two contains lines unique to FILE2,\n"
    "and column three contains lines common to both files.\n"
    "\n"
    "Note: Input files must be sorted. This implementation does not\n"
    "automatically sort the input files.",
    "  comm file1 file2\n"
    "  comm -12 file1 file2        # show only common lines\n"
    "  comm -23 file1 file2        # show only lines in file1\n"
    "  comm -13 file1 file2        # show only lines in file2\n"
    "  comm -3 file1 file2 | wc -l # count common lines",
    "cmp(1), diff(1), uniq(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    COMM_OPTIONS) {
  using namespace comm_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"comm");
    return 1;
  }

  return run(*cfg_result);
}
