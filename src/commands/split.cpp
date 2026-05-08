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
 *  - File: split.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for split.
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

auto constexpr SPLIT_OPTIONS = std::array{
    OPTION("-b", "--bytes", "put SIZE bytes per output file", STRING_TYPE),
    OPTION("-l", "--lines", "put NUMBER lines per output file", STRING_TYPE),
    OPTION("-d", "--numeric-suffixes",
           "use numeric suffixes instead of alphabetic", BOOL_TYPE),
    OPTION("-a", "--suffix-length", "use suffixes of length N (default 2)",
           STRING_TYPE)
    // -n, --number (not implemented - split into N chunks)
    // -C, --line-bytes (not implemented - split by bytes with line integrity)
};

namespace split_pipeline {
namespace cp = core::pipeline;

struct Config {
  int64_t chunk_size = 0;
  int chunk_lines = 1000;  // Default: 1000 lines per file
  bool numeric_suffixes = false;
  int suffix_length = 2;
  std::string prefix = "x";
  std::string input_file;
};

auto parse_size(const std::string& size_str) -> cp::Result<int64_t> {
  try {
    // Support: N, NKB, NMB, NG, NT, NP, NE, etc.
    std::string s = size_str;

    if (s.empty()) {
      return std::unexpected("invalid size");
    }

    int64_t multiplier = 1;
    if (s.size() > 2) {
      std::string suffix = s.substr(s.size() - 2);
      std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);

      if (suffix == "kb") {
        multiplier = 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "mb") {
        multiplier = 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "gb") {
        multiplier = 1024LL * 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "tb") {
        multiplier = 1024LL * 1024 * 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "pb") {
        multiplier = 1024LL * 1024 * 1024 * 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (suffix == "eb") {
        multiplier = 1024LL * 1024 * 1024 * 1024 * 1024 * 1024;
        s = s.substr(0, s.size() - 2);
      } else if (s.size() > 1) {
        suffix = s.substr(s.size() - 1);
        std::transform(suffix.begin(), suffix.end(), suffix.begin(), ::tolower);

        if (suffix == "k") {
          multiplier = 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "m") {
          multiplier = 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "g") {
          multiplier = 1024LL * 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "t") {
          multiplier = 1024LL * 1024 * 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "p") {
          multiplier = 1024LL * 1024 * 1024 * 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        } else if (suffix == "e") {
          multiplier = 1024LL * 1024 * 1024 * 1024 * 1024 * 1024;
          s = s.substr(0, s.size() - 1);
        }
      }
    }

    int64_t value = std::stoll(s);
    value *= multiplier;

    return value;
  } catch (...) {
    return std::unexpected("invalid size format");
  }
}

auto build_config(const CommandContext<SPLIT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  auto bytes_opt = ctx.get<std::string>("--bytes", "");
  if (bytes_opt.empty()) {
    bytes_opt = ctx.get<std::string>("-b", "");
  }
  if (!bytes_opt.empty()) {
    auto size_result = parse_size(bytes_opt);
    if (!size_result) {
      return std::unexpected(size_result.error());
    }
    cfg.chunk_size = *size_result;
  }

  auto lines_opt = ctx.get<std::string>("--lines", "");
  if (lines_opt.empty()) {
    lines_opt = ctx.get<std::string>("-l", "");
  }
  if (!lines_opt.empty()) {
    try {
      cfg.chunk_lines = std::stoi(lines_opt);
      if (cfg.chunk_lines <= 0) {
        return std::unexpected("line count must be positive");
      }
    } catch (...) {
      return std::unexpected("invalid line count");
    }
  }

  cfg.numeric_suffixes =
      ctx.get<bool>("--numeric-suffixes", false) || ctx.get<bool>("-d", false);

  auto suffix_opt = ctx.get<std::string>("--suffix-length", "");
  if (suffix_opt.empty()) {
    suffix_opt = ctx.get<std::string>("-a", "");
  }
  if (!suffix_opt.empty()) {
    try {
      cfg.suffix_length = std::stoi(suffix_opt);
      if (cfg.suffix_length < 1 || cfg.suffix_length > 10) {
        return std::unexpected("suffix length must be between 1 and 10");
      }
    } catch (...) {
      return std::unexpected("invalid suffix length");
    }
  }

  // Get input file and prefix from positionals
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

    if (ctx.positionals.size() > 1) {
      cfg.prefix = std::string(ctx.positionals[1]);
    }
  }

  return cfg;
}

auto generate_suffix(int part_num, bool numeric, int length) -> std::string {
  if (numeric) {
    char buf[32];
    snprintf(buf, sizeof(buf), "%0*d", length, part_num);
    return buf;
  } else {
    // Alphabetic suffix (a, b, ..., z, aa, ab, ...)
    std::string result;
    int n = part_num;
    while (n >= 0 && result.size() < static_cast<size_t>(length)) {
      result = static_cast<char>('a' + (n % 26)) + result;
      n = n / 26 - 1;
    }
    while (result.size() < static_cast<size_t>(length)) {
      result = 'a' + result;
    }
    if (result.size() > static_cast<size_t>(length)) {
      result = result.substr(result.size() - length);
    }
    return result;
  }
}

auto run(const Config& cfg) -> int {
  // Read input
  std::string input;

  if (cfg.input_file.empty() || cfg.input_file == "-") {
    // Read from stdin
    input.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      cp::Result<int> result = std::unexpected("error reading from file");
      cp::report_error(result, L"split");
      return 1;
    }
  } else {
    // Read from file
    std::ifstream f(cfg.input_file, std::ios::binary);
    if (!f) {
      auto err =
          std::string("cannot open '") + cfg.input_file + "' for reading";
      cp::Result<int> result = std::unexpected(std::string_view(err));
      cp::report_error(result, L"split");
      return 1;
    }
    input.assign(std::istreambuf_iterator<char>(f),
                 std::istreambuf_iterator<char>());
    if (f.fail() && !f.eof()) {
      cp::Result<int> result = std::unexpected("error reading from file");
      cp::report_error(result, L"split");
      return 1;
    }
  }

  if (input.empty()) {
    return 0;  // Nothing to split
  }

  // Determine split mode
  bool by_lines = (cfg.chunk_size == 0);

  if (by_lines) {
    // Split by lines
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < input.size()) {
      size_t end = input.find('\n', start);
      if (end == std::string::npos) {
        lines.push_back(input.substr(start));
        break;
      }
      lines.push_back(input.substr(start, end - start + 1));  // Include newline
      start = end + 1;
    }

    int part_num = 0;
    for (size_t i = 0; i < lines.size(); i += cfg.chunk_lines) {
      std::string filename =
          cfg.prefix +
          generate_suffix(part_num, cfg.numeric_suffixes, cfg.suffix_length);
      part_num++;

      std::ofstream out(filename, std::ios::binary);
      if (!out) {
        auto err = std::string("cannot create '") + filename + "'";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"split");
        return 1;
      }

      for (size_t j = i; j < i + cfg.chunk_lines && j < lines.size(); ++j) {
        out << lines[j];
      }
    }
  } else {
    // Split by bytes
    int part_num = 0;
    for (size_t i = 0; i < input.size(); i += cfg.chunk_size) {
      std::string filename =
          cfg.prefix +
          generate_suffix(part_num, cfg.numeric_suffixes, cfg.suffix_length);
      part_num++;

      std::ofstream out(filename, std::ios::binary);
      if (!out) {
        auto err = std::string("cannot create '") + filename + "'";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"split");
        return 1;
      }

      size_t chunk_end = std::min(i + cfg.chunk_size, input.size());
      out.write(input.data() + i, chunk_end - i);
    }
  }

  return 0;
}

}  // namespace split_pipeline

REGISTER_COMMAND(
    split, "split", "split [OPTION]... [INPUT [PREFIX]]",
    "Output fixed-size pieces of INPUT to PREFIXaa, PREFIXab, ...\n"
    "\n"
    "By default, split puts 1000 lines of INPUT (or stdin) into each output "
    "file.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "SIZE may have a multiplier suffix: b for 512, K for 1K, M for 1M, G for "
    "1G, etc.\n"
    "\n"
    "Note: This implementation supports basic line and byte splitting.\n"
    "Advanced features like -C (line-bytes) are not implemented.",
    "  split -l 1000 largefile.txt\n"
    "  split -b 100M largefile.txt\n"
    "  split -d -a 3 largefile.txt part\n"
    "  split -b 1M -d -a 3 input.dat output",
    "csplit(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SPLIT_OPTIONS) {
  using namespace split_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"split");
    return 1;
  }

  return run(*cfg_result);
}
