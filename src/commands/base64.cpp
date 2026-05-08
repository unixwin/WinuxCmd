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
 *  - File: base64.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for base64.
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

auto constexpr BASE64_OPTIONS = std::array{
    OPTION("-d", "--decode", "decode data", BOOL_TYPE),
    OPTION("-i", "--ignore-garbage",
           "when decoding, ignore non-alphabet characters", BOOL_TYPE),
    OPTION("-w", "--wrap",
           "wrap encoded lines after COLS character (default 76). Use 0 to "
           "disable line wrapping",
           INT_TYPE)};

namespace base64_pipeline {
namespace cp = core::pipeline;

/**
 * @brief Read input from file or stdin
 */
auto read_input(std::string_view filename) -> cp::Result<std::string> {
  std::string content;

  if (filename == "-" || filename.empty()) {
    // Read from stdin
    content.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      return std::unexpected("error reading from standard input");
    }
  } else {
    // Read from file
    std::ifstream file(std::string(filename), std::ios::binary);
    if (!file) {
      return std::unexpected(std::string("cannot open '") +
                             std::string(filename) + "' for reading");
    }
    content.assign(std::istreambuf_iterator<char>(file),
                   std::istreambuf_iterator<char>());
    if (file.fail() && !file.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  return content;
}

/**
 * @brief Build configuration from command context
 */
struct Config {
  bool decode = false;
  bool ignore_garbage = false;
  int wrap = 76;
  SmallVector<std::string, 16> files;
};

auto build_config(const CommandContext<BASE64_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.decode = ctx.get<bool>("--decode", false) || ctx.get<bool>("-d", false);
  cfg.ignore_garbage =
      ctx.get<bool>("--ignore-garbage", false) || ctx.get<bool>("-i", false);
  cfg.wrap = ctx.get<int>("--wrap", 76);

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

/**
 * @brief Run base64 encode/decode
 */
auto run(const Config& cfg) -> int {
  for (const auto& file : cfg.files) {
    auto content_result = read_input(file);
    if (!content_result) {
      cp::report_error(content_result, L"base64");
      return 1;
    }

    std::string_view content = *content_result;
    std::string output;

    if (cfg.decode) {
      // Remove newlines for decoding (except when using --ignore-garbage)
      if (!cfg.ignore_garbage) {
        std::string cleaned;
        cleaned.reserve(content.size());
        for (char c : content) {
          if (c != '\n' && c != '\r') {
            cleaned += c;
          }
        }
        content = cleaned;
      }

      auto decoded = encoding::base64_decode(content, cfg.ignore_garbage);
      if (cfg.ignore_garbage && decoded.empty() && !content.empty()) {
        // Only report error if ignore_garbage is false or input was valid
        cp::report_custom_error(L"base64", L"invalid input");
        return 1;
      }
      output.assign(reinterpret_cast<char*>(decoded.data()), decoded.size());
    } else {
      auto data = std::span<const uint8_t>(
          reinterpret_cast<const uint8_t*>(content.data()), content.size());
      output = encoding::base64_encode(data, cfg.wrap);
      output += '\n';
    }

    safePrint(output);
  }

  return 0;
}

}  // namespace base64_pipeline

REGISTER_COMMAND(
    base64, "base64", "base64 [OPTION]... [FILE]",
    "Encode or decode FILE, or standard input, to standard output.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "The data are encoded as described for the base64 alphabet in RFC\n"
    "4648. When decoding, the input may contain newlines in addition\n"
    "to the bytes of the formal base64 alphabet. Use --ignore-garbage\n"
    "to attempt to recover from any other non-alphabet bytes in the\n"
    "encoded stream.",
    "  base64 <<< 'Hello, World'\n"
    "  echo 'SGVsbG8sIFdvcmxk' | base64 -d\n"
    "  base64 -w 0 file.txt  # No line wrapping",
    "base32(1), basenc(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    BASE64_OPTIONS) {
  using namespace base64_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"base64");
    return 1;
  }

  return run(*cfg_result);
}
