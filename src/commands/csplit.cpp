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
 *  - File: csplit.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for csplit.
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

auto constexpr CSPLIT_OPTIONS = std::array{
    OPTION("-f", "--prefix", "use PREFIX instead of 'xx'", STRING_TYPE),
    OPTION("-b", "--suffix-format", "use sprintf FORMAT instead of %02d",
           STRING_TYPE),
    OPTION("-n", "--digits", "use specified number of digits", STRING_TYPE),
    OPTION("-k", "--keep-files", "do not remove output files on errors",
           BOOL_TYPE),
    OPTION("-s", "--quiet", "do not print counts of output file sizes",
           BOOL_TYPE),
    OPTION("-z", "--elide-empty-files", "remove empty output files", BOOL_TYPE)
    // --suppress-matched (not implemented)
};

namespace csplit_pipeline {
namespace cp = core::pipeline;

struct Config {
  std::string prefix = "xx";
  std::string suffix_format = "%02d";
  int digits = 2;
  bool keep_files = false;
  bool quiet = false;
  bool elide_empty = false;
  std::string input_file;
  SmallVector<std::string, 64> patterns;
};

auto build_config(const CommandContext<CSPLIT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  auto prefix_opt = ctx.get<std::string>("--prefix", "");
  if (prefix_opt.empty()) {
    prefix_opt = ctx.get<std::string>("-f", "");
  }
  if (!prefix_opt.empty()) {
    cfg.prefix = prefix_opt;
  }

  auto suffix_opt = ctx.get<std::string>("--suffix-format", "");
  if (suffix_opt.empty()) {
    suffix_opt = ctx.get<std::string>("-b", "");
  }
  if (!suffix_opt.empty()) {
    cfg.suffix_format = suffix_opt;
  }

  auto digits_opt = ctx.get<std::string>("--digits", "");
  if (digits_opt.empty()) {
    digits_opt = ctx.get<std::string>("-n", "");
  }
  if (!digits_opt.empty()) {
    try {
      cfg.digits = std::stoi(digits_opt);
    } catch (...) {
      return std::unexpected("invalid digits value");
    }
  }

  cfg.keep_files =
      ctx.get<bool>("--keep-files", false) || ctx.get<bool>("-k", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-s", false);
  cfg.elide_empty =
      ctx.get<bool>("--elide-empty-files", false) || ctx.get<bool>("-z", false);

  // Get input file and patterns from positionals
  if (!ctx.positionals.empty()) {
    std::string file_arg(ctx.positionals[0]);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded && !glob_result.files.empty()) {
        cfg.input_file = wstring_to_utf8(glob_result.files[0]);
      } else {
        cfg.input_file = file_arg;
      }
    } else {
      cfg.input_file = file_arg;
    }

    for (size_t i = 1; i < ctx.positionals.size(); ++i) {
      cfg.patterns.push_back(std::string(ctx.positionals[i]));
    }
  }

  if (cfg.input_file.empty()) {
    return std::unexpected("missing input file");
  }

  if (cfg.patterns.empty()) {
    return std::unexpected("missing pattern");
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

auto match_pattern(const std::string& line, const std::string& pattern)
    -> bool {
  // Simple pattern matching (supports exact match and wildcards)
  if (pattern.empty()) return false;

  // Check if pattern is a number (line number)
  try {
    int line_num = std::stoi(pattern);
    return false;  // Line number matching not implemented
  } catch (...) {
    // String pattern
    if (pattern[0] == '/') {
      // Regex pattern (not fully implemented)
      std::string search_pattern = pattern.substr(1, pattern.size() - 1);
      return line.find(search_pattern) != std::string::npos;
    } else {
      // Exact match
      return line == pattern;
    }
  }
}

auto run(const Config& cfg) -> int {
  auto lines_result = read_lines(cfg.input_file);
  if (!lines_result) {
    cp::report_error(lines_result, L"csplit");
    return 1;
  }

  const auto& lines = *lines_result;
  SmallVector<std::string, 64> output_files;
  SmallVector<SmallVector<size_t, 1024>, 64> file_ranges;

  size_t current_start = 0;
  file_ranges.push_back({});

  // Process patterns
  for (size_t i = 0; i < cfg.patterns.size(); ++i) {
    const auto& pattern = cfg.patterns[i];
    bool pattern_found = false;

    for (size_t line_idx = current_start; line_idx < lines.size(); ++line_idx) {
      if (match_pattern(lines[line_idx], pattern)) {
        // Found pattern, split here
        file_ranges.back().push_back(line_idx);
        file_ranges.push_back({});
        current_start = line_idx + 1;
        pattern_found = true;
        break;
      }
    }

    if (!pattern_found && i < cfg.patterns.size() - 1) {
      // Pattern not found (not last one)
      if (!cfg.quiet) {
        auto err = std::string("pattern '") + pattern + "' not found";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"csplit");
      }
      return 1;
    }
  }

  // Add remaining lines
  for (size_t line_idx = current_start; line_idx < lines.size(); ++line_idx) {
    file_ranges.back().push_back(line_idx);
  }

  // Write output files
  char filename_buf[256];
  int file_count = 0;

  for (const auto& range : file_ranges) {
    if (range.empty() && cfg.elide_empty) {
      continue;
    }

    snprintf(filename_buf, sizeof(filename_buf), "%s%0*d", cfg.prefix.c_str(),
             cfg.digits, file_count);
    std::string filename(filename_buf);

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
      auto err = std::string("cannot create '") + filename + "'";
      cp::Result<int> result = std::unexpected(std::string_view(err));
      cp::report_error(result, L"csplit");
      return 1;
    }

    for (size_t line_idx : range) {
      out << lines[line_idx] << "\n";
    }

    out.close();

    if (!cfg.quiet) {
      if (!cfg.elide_empty || !range.empty()) {
        safePrint(filename);
        safePrint("\n");
      }
    }

    file_count++;
  }

  return 0;
}

}  // namespace csplit_pipeline

REGISTER_COMMAND(
    csplit, "csplit", "csplit [OPTION]... FILE PATTERN...",
    "Output pieces of FILE separated by PATTERN(s) to files 'xx00', 'xx01', "
    "...\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "PATTERN is a line number, or a /regex/ pattern.\n"
    "\n"
    "Note: This is a basic implementation. Advanced pattern matching\n"
    "features like line numbers and skip/repeat modifiers are not fully "
    "supported.",
    "  csplit file.txt '/pattern/'\n"
    "  csplit -f chapter file.txt '/Chapter/'\n"
    "  csplit -n 3 file.txt 100\n"
    "  csplit -z file.txt '/^Header$/' '/^Footer$/'",
    "split(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CSPLIT_OPTIONS) {
  using namespace csplit_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"csplit");
    return 1;
  }

  return run(*cfg_result);
}
