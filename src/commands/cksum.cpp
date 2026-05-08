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
 *  - File: cksum.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for cksum.
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

auto constexpr CKSUM_OPTIONS =
    std::array{OPTION("", "", "compute and check CRC checksums", STRING_TYPE)};

namespace cksum_pipeline {
namespace cp = core::pipeline;

struct Config {
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<CKSUM_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

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

auto calculate_crc32(const std::string& filename, uint32_t& byte_count)
    -> cp::Result<uint32_t> {
  std::vector<char> data;

  if (filename == "-" || filename.empty()) {
    // Read from stdin
    data.assign(std::istreambuf_iterator<char>(std::cin),
                std::istreambuf_iterator<char>());
  } else {
    // Read from file
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }
    data.assign(std::istreambuf_iterator<char>(f),
                std::istreambuf_iterator<char>());
    if (f.fail() && !f.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  byte_count = static_cast<uint32_t>(data.size());

  // Simple CRC32 implementation
  uint32_t crc = 0xFFFFFFFF;
  uint32_t polynomial = 0xEDB88320;

  for (unsigned char byte : data) {
    crc ^= byte;
    for (int i = 0; i < 8; ++i) {
      if (crc & 1) {
        crc = (crc >> 1) ^ polynomial;
      } else {
        crc >>= 1;
      }
    }
  }

  return ~crc;
}

auto run(const Config& cfg) -> int {
  for (const auto& file : cfg.files) {
    uint32_t byte_count = 0;
    auto crc_result = calculate_crc32(file, byte_count);

    if (!crc_result) {
      cp::report_error(crc_result, L"cksum");
      return 1;
    }

    char buf[64];
    snprintf(buf, sizeof(buf), "%u %u", *crc_result, byte_count);
    safePrint(buf);

    if (file != "-") {
      safePrint(" ");
      safePrint(file);
    }
    safePrintLn("");
  }

  return 0;
}

}  // namespace cksum_pipeline

REGISTER_COMMAND(cksum, "cksum", "cksum [FILE]...",
                 "Print CRC checksum and byte counts of each FILE.\n"
                 "\n"
                 "With no FILE, or when FILE is -, read standard input.\n"
                 "\n"
                 "Note: This is a simplified implementation using CRC32.",
                 "  cksum file.txt\n"
                 "  echo \"test\" | cksum\n"
                 "  cksum *.txt",
                 "md5sum(1), sha1sum(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", CKSUM_OPTIONS) {
  using namespace cksum_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"cksum");
    return 1;
  }

  return run(*cfg_result);
}
