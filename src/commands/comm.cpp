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
    OPTION("--check-order", "", "check that the input is correctly sorted",
           STRING_TYPE),
    OPTION("--nocheck-order", "",
           "do not check that the input is correctly sorted", STRING_TYPE),
    OPTION("--output-delimiter", "", "separate columns with STR", STRING_TYPE)};

namespace comm_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool suppress_col1 = false;
  bool suppress_col2 = false;
  bool suppress_col3 = false;
  bool check_order = false;
  std::string output_delimiter = "\t";
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<COMM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.suppress_col1 = ctx.get<bool>("-1", false);
  cfg.suppress_col2 = ctx.get<bool>("-2", false);
  cfg.suppress_col3 = ctx.get<bool>("-3", false);
  cfg.check_order = ctx.get<bool>("--check-order", false);
  cfg.output_delimiter = ctx.get<std::string>("--output-delimiter", "\t");

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

// Read sorted lines from file
auto read_lines(const std::string& filename)
    -> cp::Result<SmallVector<std::string, 1024>> {
  SmallVector<std::string, 1024> lines;

  if (filename == "-") {
    // Read from stdin
    std::string line;
    while (std::getline(std::cin, line)) {
      lines.push_back(line);
    }
  } else {
    // Read from file
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
    cp::report_error(lines1_result, L"comm");
    return 1;
  }

  auto lines2_result = read_lines(file2);
  if (!lines2_result) {
    cp::report_error(lines2_result, L"comm");
    return 1;
  }

  const auto& lines1 = *lines1_result;
  const auto& lines2 = *lines2_result;

  // Merge and compare
  size_t i = 0, j = 0;
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
      if (!cfg.suppress_col1) {
        safePrintLn(lines1[i]);
      }
      i++;
    } else if (cmp_result > 0) {
      // Line only in file2
      if (!cfg.suppress_col2) {
        if (!cfg.suppress_col1) {
          safePrint(cfg.output_delimiter);
        }
        safePrintLn(lines2[j]);
      }
      j++;
    } else {
      // Line in both files
      if (!cfg.suppress_col3) {
        if (!cfg.suppress_col1) {
          safePrint(cfg.output_delimiter);
        }
        if (!cfg.suppress_col2) {
          safePrint(cfg.output_delimiter);
        }
        safePrintLn(lines1[i]);
      }
      i++;
      j++;
    }
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
