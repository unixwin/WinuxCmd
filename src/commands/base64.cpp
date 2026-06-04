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

struct Config {
  bool decode = false;
  bool ignore_garbage = false;
  int wrap = 76;
  std::string file = "-";
};

auto read_input(std::string_view filename)
    -> std::expected<std::string, std::string> {
  std::string content;

  if (filename == "-") {
    content.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      return std::unexpected("error reading from standard input");
    }
    return content;
  }

  std::ifstream file(std::string(filename), std::ios::binary);
  if (!file) {
    return std::unexpected("cannot open '" + std::string(filename) +
                           "' for reading");
  }

  content.assign(std::istreambuf_iterator<char>(file),
                 std::istreambuf_iterator<char>());
  if (file.fail() && !file.eof()) {
    return std::unexpected("error reading '" + std::string(filename) + "'");
  }

  return content;
}

auto decode_base64(std::string_view input, bool ignore_garbage)
    -> std::expected<std::string, std::string> {
  constexpr std::string_view alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string clean;
  clean.reserve(input.size());
  bool saw_padding = false;
  int padding = 0;

  for (unsigned char c : input) {
    if (c == '\n' || c == '\r') continue;

    if (alphabet.find(static_cast<char>(c)) != std::string_view::npos) {
      if (saw_padding) return std::unexpected("invalid input");
      clean.push_back(static_cast<char>(c));
      continue;
    }

    if (c == '=') {
      saw_padding = true;
      ++padding;
      if (padding > 2) return std::unexpected("invalid input");
      clean.push_back('=');
      continue;
    }

    if (!ignore_garbage) return std::unexpected("invalid input");
  }

  const size_t data_chars = clean.find('=');
  const size_t encoded_chars =
      data_chars == std::string::npos ? clean.size() : data_chars;

  if (clean.size() % 4 == 1 || encoded_chars % 4 == 1) {
    return std::unexpected("invalid input");
  }
  if (padding > 0 && clean.size() % 4 != 0) {
    return std::unexpected("invalid input");
  }
  if ((padding == 1 && encoded_chars % 4 != 3) ||
      (padding == 2 && encoded_chars % 4 != 2)) {
    return std::unexpected("invalid input");
  }

  std::string output;
  output.reserve((encoded_chars / 4) * 3 + 2);
  uint32_t accumulator = 0;
  int bits = 0;

  for (char c : clean.substr(0, encoded_chars)) {
    const auto value = static_cast<uint32_t>(alphabet.find(c));
    accumulator = (accumulator << 6) | value;
    bits += 6;

    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<char>((accumulator >> bits) & 0xff));
    }
  }

  if (bits > 0 && (accumulator & ((uint32_t{1} << bits) - 1)) != 0) {
    return std::unexpected("invalid input");
  }

  return output;
}

auto build_config(const CommandContext<BASE64_OPTIONS.size()>& ctx)
    -> std::expected<Config, std::string> {
  Config cfg;
  cfg.decode = ctx.get<bool>("--decode", false) || ctx.get<bool>("-d", false);
  cfg.ignore_garbage =
      ctx.get<bool>("--ignore-garbage", false) || ctx.get<bool>("-i", false);
  cfg.wrap = ctx.get<int>("--wrap", 76);

  if (cfg.wrap < 0) return std::unexpected("invalid wrap size");

  SmallVector<std::string, 16> files;
  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          files.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    files.push_back(file_arg);
  }

  if (files.size() > 1) {
    return std::unexpected("extra operand '" + files[1] + "'");
  }
  if (!files.empty()) cfg.file = files[0];

  return cfg;
}

auto run(const Config& cfg) -> int {
  auto content_result = read_input(cfg.file);
  if (!content_result) {
    safeErrorPrintLn("base64: " + content_result.error());
    return 1;
  }

  if (cfg.decode) {
    auto decoded = decode_base64(*content_result, cfg.ignore_garbage);
    if (!decoded) {
      safeErrorPrintLn("base64: " + decoded.error());
      return 1;
    }
    safePrint(*decoded);
    return 0;
  }

  const auto& content = *content_result;
  auto data = std::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(content.data()), content.size());
  std::string output = encoding::base64_encode(data, cfg.wrap);
  if (!output.empty()) output.push_back('\n');
  safePrint(output);
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
    safeErrorPrintLn("base64: " + cfg_result.error());
    return 1;
  }

  return run(*cfg_result);
}
