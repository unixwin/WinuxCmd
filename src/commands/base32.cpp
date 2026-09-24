// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU]
    OPTION("-d", "--decode", "decode data", BOOL_TYPE),
    // [GNU]
    OPTION("-i", "--ignore-garbage",
           "when decoding, ignore non-alphabet characters", BOOL_TYPE),
    // [GNU]
    OPTION("-w", "--wrap",
           "wrap encoded lines after COLS character (default 76). Use 0 to "
           "disable line wrapping",
           STRING_TYPE)};

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
  return file_io::read_all_input(filename);
}

// GNU decodes quantum units of 8 characters (uppercase alphabet only — no
// case folding), keeps every byte decoded before the first error, and fails
// a trailing partial unit (uutils #6008, #12204).
auto decode_base32(std::string_view input, bool ignore_garbage)
    -> encoding::GnuDecodeResult {
  return encoding::base32_decode_gnu(input, BASE32_ALPHABET, ignore_garbage);
}

// [GNU] the wrap size is validated by xstrtol: any non-numeric token or a
// negative value dies with "invalid wrap size: '<raw>'" (uutils #14084).
auto parse_wrap_size(const std::string& raw)
    -> std::expected<int, std::string> {
  size_t digit_start = 0;
  if (!raw.empty() && (raw[0] == '+' || raw[0] == '-')) digit_start = 1;
  const bool numeric =
      digit_start < raw.size() &&
      std::ranges::all_of(
          raw.substr(digit_start),
          [](unsigned char ch) { return std::isdigit(ch) != 0; });
  if (numeric) {
    errno = 0;
    const long long value = std::strtoll(raw.c_str(), nullptr, 10);
    if (errno == 0 && value >= 0 && value <= 2147483647LL) {
      return static_cast<int>(value);
    }
  }
  return std::unexpected("invalid wrap size: '" + raw + "'");
}

auto build_config(const CommandContext<BASE32_OPTIONS.size()>& ctx)
    -> std::expected<Config, std::string> {
  Config cfg;
  cfg.decode = ctx.get<bool>("--decode", false) || ctx.get<bool>("-d", false);
  cfg.ignore_garbage =
      ctx.get<bool>("--ignore-garbage", false) || ctx.get<bool>("-i", false);
  auto wrap = parse_wrap_size(ctx.get<std::string>("--wrap", "76"));
  if (!wrap) return std::unexpected(wrap.error());
  cfg.wrap = *wrap;

  SmallVector<std::string, 16> files;
  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    for (const auto& file : expand_file_operand(file_arg)) {
      files.push_back(file);
    }
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
    safeErrorPrintLn("base32: " + content_result.error());
    return 1;
  }

  if (cfg.decode) {
    auto decoded = decode_base32(*content_result, cfg.ignore_garbage);
    if (!decoded.output.empty()) safePrint(decoded.output);
    if (!decoded.ok) {
      safeErrorPrintLn("base32: invalid input");
      return 1;
    }
    return 0;
  }

  auto data = std::span<const uint8_t>(
      reinterpret_cast<const uint8_t*>(content_result->data()),
      content_result->size());
  std::string output = encoding::base32_encode(data, cfg.wrap);
  if (!output.empty() && cfg.wrap > 0) output.push_back('\n');
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
    safeErrorPrintLn("base32: " +
                     winux::i18n::translate_error(cfg_result.error()));
    if (cfg_result.error().starts_with("extra operand '")) {
      safeErrorPrintLn("Try 'base32 --help' for more information.");
    }
    return 1;
  }

  return run(*cfg_result);
}
