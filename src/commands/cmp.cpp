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
 *  - File: cmp.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for cmp.
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

auto constexpr CMP_OPTIONS = std::array{
    OPTION("-b", "--print-bytes", "print differing bytes", BOOL_TYPE),
    OPTION("-i", "--ignore-initial", "skip specified number of initial bytes",
           STRING_TYPE),
    OPTION("-l", "--verbose", "output byte numbers of differing bytes",
           BOOL_TYPE),
    OPTION("-n", "--bytes", "compare specified number of bytes", STRING_TYPE),
    OPTION("-s", "--quiet", "silent mode", BOOL_TYPE)};

namespace cmp_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool print_bytes = false;
  bool verbose = false;
  bool quiet = false;
  size_t skip_bytes = 0;
  size_t max_bytes = SIZE_MAX;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<CMP_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.print_bytes =
      ctx.get<bool>("--print-bytes", false) || ctx.get<bool>("-b", false);
  cfg.verbose = ctx.get<bool>("--verbose", false) || ctx.get<bool>("-l", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-s", false);

  auto skip_opt = ctx.get<std::string>("--ignore-initial", "");
  if (skip_opt.empty()) {
    skip_opt = ctx.get<std::string>("-i", "");
  }
  if (!skip_opt.empty()) {
    try {
      cfg.skip_bytes = static_cast<size_t>(std::stoull(skip_opt));
    } catch (...) {
      return std::unexpected("invalid skip count");
    }
  }

  auto bytes_opt = ctx.get<std::string>("--bytes", "");
  if (bytes_opt.empty()) {
    bytes_opt = ctx.get<std::string>("-n", "");
  }
  if (!bytes_opt.empty()) {
    try {
      cfg.max_bytes = static_cast<size_t>(std::stoull(bytes_opt));
    } catch (...) {
      return std::unexpected("invalid byte count");
    }
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

auto run(const Config& cfg) -> int {
  const std::string& file1 = cfg.files[0];
  const std::string& file2 = cfg.files[1];

  // Read file 1
  std::vector<char> data1;
  if (file1 == "-") {
    data1.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
  } else {
    std::ifstream f1(file1, std::ios::binary);
    if (!f1) {
      if (!cfg.quiet) {
        auto err = std::string("cmp: ") + file1 + ": No such file";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"cmp");
      }
      return 2;
    }
    data1.assign(std::istreambuf_iterator<char>(f1),
                 std::istreambuf_iterator<char>());
    // Skip UTF-8 BOM if present at the beginning
    if (data1.size() >= 3 && static_cast<unsigned char>(data1[0]) == 0xEF &&
        static_cast<unsigned char>(data1[1]) == 0xBB &&
        static_cast<unsigned char>(data1[2]) == 0xBF) {
      data1.assign(data1.begin() + 3, data1.end());
    }
  }

  // Read file 2
  std::vector<char> data2;
  if (file2 == "-") {
    data2.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
  } else {
    std::ifstream f2(file2, std::ios::binary);
    if (!f2) {
      if (!cfg.quiet) {
        auto err = std::string("cmp: ") + file2 + ": No such file";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"cmp");
      }
      return 2;
    }
    data2.assign(std::istreambuf_iterator<char>(f2),
                 std::istreambuf_iterator<char>());
    // Skip UTF-8 BOM if present at the beginning
    if (data2.size() >= 3 && static_cast<unsigned char>(data2[0]) == 0xEF &&
        static_cast<unsigned char>(data2[1]) == 0xBB &&
        static_cast<unsigned char>(data2[2]) == 0xBF) {
      data2.assign(data2.begin() + 3, data2.end());
    }
  }

  // Skip initial bytes
  size_t start_pos = cfg.skip_bytes;
  if (start_pos >= data1.size() && start_pos >= data2.size()) {
    return 0;  // Both files empty after skip
  }

  // Compare up to max_bytes
  size_t bytes_to_compare =
      std::min(cfg.max_bytes, std::min(data1.size(), data2.size()) - start_pos);

  for (size_t i = 0; i < bytes_to_compare; ++i) {
    size_t pos = start_pos + i;
    if (pos >= data1.size() || pos >= data2.size()) {
      break;
    }

    unsigned char c1 = static_cast<unsigned char>(data1[pos]);
    unsigned char c2 = static_cast<unsigned char>(data2[pos]);

    if (c1 != c2) {
      // Files differ
      if (cfg.quiet) {
        return 1;
      }

      if (cfg.verbose) {
        safePrint(file1);
        safePrint(" ");
        safePrint(file2);
        safePrint(" differ: byte ");
        safePrintLn(std::to_string(pos + 1));
      }

      if (cfg.print_bytes) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%zu %3o %3o", pos + 1, c1, c2);
        safePrintLn(buf);
      } else {
        safePrint(file1);
        safePrint(" ");
        safePrint(file2);
        safePrint(" differ: byte ");
        safePrintLn(std::to_string(pos + 1));
      }

      return 1;
    }
  }

  // Check if one file is longer
  if (data1.size() > data2.size()) {
    if (!cfg.quiet) {
      safePrint("cmp: EOF on ");
      safePrintLn(file2);
    }
    return 1;
  } else if (data2.size() > data1.size()) {
    if (!cfg.quiet) {
      safePrint("cmp: EOF on ");
      safePrintLn(file1);
    }
    return 1;
  }

  return 0;  // Files are identical
}

}  // namespace cmp_pipeline

REGISTER_COMMAND(
    cmp, "cmp", "cmp [OPTION]... FILE1 [FILE2 [SKIP1 [SKIP2]]]",
    "Compare two files byte by byte.\n"
    "\n"
    "Compare FILE1 with FILE2.\n"
    "If FILE1 or FILE2 is -, read standard input.\n"
    "\n"
    "Exit status is 0 if inputs are the same, 1 if different, 2 if trouble.\n"
    "\n"
    "Note: This implementation supports basic byte comparison.\n"
    "Advanced features like SKIP1/SKIP2 are not implemented.",
    "  cmp file1 file2\n"
    "  cmp -l file1 file2\n"
    "  cmp -b file1 file2\n"
    "  cmp -n 100 file1 file2\n"
    "  cmp -s file1 file2 && echo 'files are equal'",
    "diff(1), comm(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CMP_OPTIONS) {
  using namespace cmp_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"cmp");
    return 1;
  }

  return run(*cfg_result);
}
