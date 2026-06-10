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
 *  - File: sum.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for sum.
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

auto constexpr SUM_OPTIONS = std::array{
    OPTION("-r", "--bsd", "use BSD sum algorithm (1024-byte blocks)",
           BOOL_TYPE),
    OPTION("-s", "--sysv", "use System V sum algorithm (512-byte blocks)",
           BOOL_TYPE)};

namespace sum_pipeline {
namespace cp = core::pipeline;

struct Config {
  enum class Algorithm { Bsd, Sysv };

  Algorithm algorithm = Algorithm::Bsd;
  struct InputFile {
    std::string path;
    bool display_name = true;
  };
  SmallVector<InputFile, 64> files;
};

auto choose_algorithm(const CommandContext<SUM_OPTIONS.size()>& ctx)
    -> Config::Algorithm {
  auto algorithm = Config::Algorithm::Bsd;
  for (auto arg : ctx.raw_args) {
    if (arg == "-r" || arg == "--bsd") algorithm = Config::Algorithm::Bsd;
    if (arg == "-s" || arg == "--sysv") algorithm = Config::Algorithm::Sysv;
  }
  return algorithm;
}

auto build_config(const CommandContext<SUM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.algorithm = choose_algorithm(ctx);

  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          cfg.files.push_back({wstring_to_utf8(file), true});
        }
        continue;
      }
    }
    cfg.files.push_back({file_arg, true});
  }

  if (cfg.files.empty()) {
    cfg.files.push_back({"-", false});
  }

  return cfg;
}

auto calculate_checksum(const std::string& filename, uint32_t& block_count,
                        Config::Algorithm algorithm) -> cp::Result<uint16_t> {
  std::vector<char> data;
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
    data.assign(std::istreambuf_iterator<char>(std::cin),
                std::istreambuf_iterator<char>());
  } else {
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(input_open_error(filename));
    }
    data.assign(std::istreambuf_iterator<char>(f),
                std::istreambuf_iterator<char>());
    if (f.fail() && !f.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  // Calculate block count
  int block_size = algorithm == Config::Algorithm::Sysv ? 512 : 1024;
  block_count =
      (static_cast<uint32_t>(data.size()) + block_size - 1) / block_size;

  if (algorithm == Config::Algorithm::Sysv) {
    uint32_t sum = 0;
    for (unsigned char byte : data) {
      sum += byte;
    }
    uint32_t folded = (sum & 0xffffU) + (sum >> 16U);
    folded = (folded & 0xffffU) + (folded >> 16U);
    return static_cast<uint16_t>(folded);
  }

  uint32_t checksum = 0;
  for (unsigned char byte : data) {
    checksum = (checksum >> 1) + ((checksum & 1) << 15);
    checksum += byte;
    checksum &= 0xFFFF;
  }

  return static_cast<uint16_t>(checksum);
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;

  for (const auto& file : cfg.files) {
    uint32_t block_count = 0;
    auto checksum_result =
        calculate_checksum(file.path, block_count, cfg.algorithm);

    if (!checksum_result) {
      cp::report_error(checksum_result, L"sum");
      all_ok = false;
      continue;
    }

    char buf[64];
    if (cfg.algorithm == Config::Algorithm::Bsd) {
      snprintf(buf, sizeof(buf), "%05u %5u", *checksum_result, block_count);
    } else {
      snprintf(buf, sizeof(buf), "%u %u", *checksum_result, block_count);
    }
    safePrint(buf);

    if (file.display_name) {
      safePrint(" ");
      safePrint(file.path);
    }
    safePrintLn("");
  }

  return all_ok ? 0 : 1;
}

}  // namespace sum_pipeline

REGISTER_COMMAND(sum, "sum", "sum [OPTION]... [FILE]...",
                 "Checksum and count the blocks in a file.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "The BSD algorithm is used by default (1024-byte blocks).\n",
                 "  sum file.txt\n"
                 "  echo \"test\" | sum\n"
                 "  sum -r file.txt",
                 "cksum(1), md5sum(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 SUM_OPTIONS) {
  using namespace sum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"sum");
    return 1;
  }

  return run(*cfg_result);
}
