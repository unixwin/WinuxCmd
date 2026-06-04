/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: base32.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for base32 command (RFC 4648).
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr BASE32_OPTIONS = std::array{
    OPTION("-d", "--decode", "decode data", BOOL_TYPE),
    OPTION("-i", "--ignore-garbage",
           "when decoding, ignore non-alphabet characters", BOOL_TYPE),
    OPTION("-w", "--wrap",
           "wrap encoded lines after COLS character (default 76). Use 0 to "
           "disable line wrapping",
           INT_TYPE)};

namespace base32_pipeline {

constexpr std::string_view BASE32_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

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

auto encode_base32(std::string_view input, int wrap) -> std::string {
  std::string result;
  result.reserve(((input.size() + 4) / 5) * 8);

  for (size_t i = 0; i < input.size(); i += 5) {
    uint8_t block[5] = {0};
    const size_t block_size = std::min<size_t>(5, input.size() - i);
    for (size_t j = 0; j < block_size; ++j) {
      block[j] = static_cast<uint8_t>(input[i + j]);
    }

    result.push_back(BASE32_ALPHABET[(block[0] >> 3) & 0x1f]);
    result.push_back(
        BASE32_ALPHABET[((block[0] & 0x07) << 2) | (block[1] >> 6)]);
    result.push_back(block_size > 1 ? BASE32_ALPHABET[(block[1] >> 1) & 0x1f]
                                    : '=');
    result.push_back(
        block_size > 1
            ? BASE32_ALPHABET[((block[1] & 0x01) << 4) | (block[2] >> 4)]
            : '=');
    result.push_back(
        block_size > 2
            ? BASE32_ALPHABET[((block[2] & 0x0f) << 1) | (block[3] >> 7)]
            : '=');
    result.push_back(block_size > 3 ? BASE32_ALPHABET[(block[3] >> 2) & 0x1f]
                                    : '=');
    result.push_back(
        block_size > 3
            ? BASE32_ALPHABET[((block[3] & 0x03) << 3) | (block[4] >> 5)]
            : '=');
    result.push_back(block_size > 4 ? BASE32_ALPHABET[block[4] & 0x1f] : '=');
  }

  if (wrap <= 0 || result.empty()) return result;

  std::string wrapped;
  for (size_t i = 0; i < result.size(); i += static_cast<size_t>(wrap)) {
    if (!wrapped.empty()) wrapped.push_back('\n');
    wrapped.append(result.substr(i, static_cast<size_t>(wrap)));
  }
  return wrapped;
}

auto decode_base32(std::string_view input, bool ignore_garbage)
    -> std::expected<std::string, std::string> {
  std::string clean;
  clean.reserve(input.size());
  bool saw_padding = false;
  int padding = 0;

  for (unsigned char c : input) {
    if (c == '\n' || c == '\r') continue;

    char upper = static_cast<char>(std::toupper(c));
    if (BASE32_ALPHABET.find(upper) != std::string_view::npos) {
      if (saw_padding) return std::unexpected("invalid input");
      clean.push_back(upper);
      continue;
    }

    if (c == '=') {
      saw_padding = true;
      ++padding;
      if (padding > 6) return std::unexpected("invalid input");
      clean.push_back('=');
      continue;
    }

    if (!ignore_garbage) return std::unexpected("invalid input");
  }

  const size_t data_chars = clean.find('=');
  const size_t encoded_chars =
      data_chars == std::string::npos ? clean.size() : data_chars;
  const size_t encoded_mod = encoded_chars % 8;

  if (encoded_mod == 1 || encoded_mod == 3 || encoded_mod == 6) {
    return std::unexpected("invalid input");
  }
  if (padding > 0 && clean.size() % 8 != 0) {
    return std::unexpected("invalid input");
  }
  if ((padding == 1 && encoded_mod != 7) ||
      (padding == 3 && encoded_mod != 5) ||
      (padding == 4 && encoded_mod != 4) ||
      (padding == 6 && encoded_mod != 2) || (padding == 2 || padding == 5)) {
    return std::unexpected("invalid input");
  }

  std::string output;
  output.reserve((encoded_chars * 5) / 8);
  uint32_t accumulator = 0;
  int bits = 0;

  for (char c : clean.substr(0, encoded_chars)) {
    const auto value = static_cast<uint32_t>(BASE32_ALPHABET.find(c));
    accumulator = (accumulator << 5) | value;
    bits += 5;

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

auto build_config(const CommandContext<BASE32_OPTIONS.size()>& ctx)
    -> std::expected<Config, std::string> {
  Config cfg;
  cfg.decode = ctx.get<bool>("--decode", false) || ctx.get<bool>("-d", false);
  cfg.ignore_garbage =
      ctx.get<bool>("--ignore-garbage", false) || ctx.get<bool>("-i", false);
  cfg.wrap = ctx.get<int>("--wrap", 76);

  if (cfg.wrap < 0) return std::unexpected("invalid wrap size");
  if (ctx.positionals.size() > 1) {
    return std::unexpected("extra operand '" + std::string(ctx.positionals[1]) +
                           "'");
  }
  if (!ctx.positionals.empty()) cfg.file = std::string(ctx.positionals[0]);

  return cfg;
}

auto run(const Config& cfg) -> int {
  auto content_result = read_input(cfg.file);
  if (!content_result) {
    safeErrorPrintLn("base32: " + content_result.error());
    return 1;
  }

  if (cfg.decode) {
    auto decoded = decode_base32(*content_result, cfg.ignore_garbage);
    if (!decoded) {
      safeErrorPrintLn("base32: " + decoded.error());
      return 1;
    }
    safePrint(*decoded);
    return 0;
  }

  std::string output = encode_base32(*content_result, cfg.wrap);
  if (!output.empty()) output.push_back('\n');
  safePrint(output);
  return 0;
}

}  // namespace base32_pipeline

REGISTER_COMMAND(
    base32, "base32", "base32 [OPTION]... [FILE]",
    "Base32 encode or decode FILE, or standard input, to standard output.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "The input may contain newlines when decoding. Use --ignore-garbage to\n"
    "attempt to recover from any other non-alphabet bytes.",
    "  base32 file.txt\n"
    "  echo 'Hello' | base32\n"
    "  base32 -d encoded.txt\n"
    "  base32 -w 64 file.bin",
    "base64(1), basenc(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    BASE32_OPTIONS) {
  using namespace base32_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    safeErrorPrintLn("base32: " + cfg_result.error());
    return 1;
  }

  return run(*cfg_result);
}
