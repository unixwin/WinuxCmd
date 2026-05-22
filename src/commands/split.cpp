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
    OPTION("-C", "--line-bytes",
           "put at most SIZE bytes of complete lines per output file",
           STRING_TYPE),
    OPTION("-l", "--lines", "put NUMBER lines per output file", STRING_TYPE),
    OPTION("-d", "--numeric-suffixes",
           "use numeric suffixes instead of alphabetic", OPTIONAL_STRING_TYPE),
    OPTION("-x", "--hex-suffixes", "use hexadecimal suffixes",
           OPTIONAL_STRING_TYPE),
    OPTION("-a", "--suffix-length", "use suffixes of length N (default 2)",
           STRING_TYPE),
    OPTION("", "--additional-suffix", "append SUFFIX to output file names",
           STRING_TYPE)
    // -n, --number (not implemented - split into N chunks)
};

namespace split_pipeline {
namespace cp = core::pipeline;

auto resolve_input_file(const CommandContext<SPLIT_OPTIONS.size()>& ctx)
    -> cp::Result<std::string> {
  if (ctx.positionals.empty()) {
    return "-";
  }

  std::string file_arg = std::string(ctx.positionals[0]);
  if (contains_wildcard(file_arg)) {
    auto glob_result = glob_expand(file_arg);
    if (glob_result.expanded && !glob_result.files.empty()) {
      if (glob_result.files.size() != 1) {
        return std::unexpected("wildcard input must match exactly one file");
      }
      return wstring_to_utf8(glob_result.files[0]);
    }
  }

  return file_arg;
}

struct Config {
  enum class Mode { Lines, Bytes, LineBytes };
  enum class SuffixKind { Alpha, Numeric, Hex };

  Mode mode = Mode::Lines;
  int64_t chunk_size = 0;
  int64_t chunk_lines = 1000;  // Default: 1000 lines per file
  SuffixKind suffix_kind = SuffixKind::Alpha;
  int suffix_length = 2;
  bool suffix_length_explicit = false;
  uint64_t suffix_start = 0;
  bool suffix_start_explicit = false;
  std::string additional_suffix;
  std::string prefix = "x";
  std::string input_file;
};

auto checked_mul(int64_t value, int64_t multiplier) -> cp::Result<int64_t> {
  if (value <= 0 || multiplier <= 0 ||
      value > std::numeric_limits<int64_t>::max() / multiplier) {
    return std::unexpected("invalid size");
  }
  return value * multiplier;
}

auto parse_size(const std::string& size_str) -> cp::Result<int64_t> {
  try {
    std::string s = size_str;
    if (s.empty()) return std::unexpected("invalid size");

    int64_t multiplier = 1;
    auto consume_suffix = [&](std::string_view suffix, int64_t factor) {
      if (s.size() < suffix.size()) return false;
      if (std::string_view(s).substr(s.size() - suffix.size()) != suffix) {
        return false;
      }
      multiplier = factor;
      s.resize(s.size() - suffix.size());
      return true;
    };

    struct Suffix {
      std::string_view text;
      int64_t factor;
    };
    constexpr int64_t k1000 = 1000;
    constexpr int64_t k1024 = 1024;
    const std::array<Suffix, 31> suffixes = {
        Suffix{"KiB", k1024},
        Suffix{"kiB", k1024},
        Suffix{"MiB", k1024 * k1024},
        Suffix{"miB", k1024 * k1024},
        Suffix{"GiB", k1024 * k1024 * k1024},
        Suffix{"giB", k1024 * k1024 * k1024},
        Suffix{"TiB", k1024 * k1024 * k1024 * k1024},
        Suffix{"tiB", k1024 * k1024 * k1024 * k1024},
        Suffix{"PiB", k1024 * k1024 * k1024 * k1024 * k1024},
        Suffix{"piB", k1024 * k1024 * k1024 * k1024 * k1024},
        Suffix{"EiB", k1024 * k1024 * k1024 * k1024 * k1024 * k1024},
        Suffix{"eiB", k1024 * k1024 * k1024 * k1024 * k1024 * k1024},
        Suffix{"KB", k1000},
        Suffix{"MB", k1000 * k1000},
        Suffix{"GB", k1000 * k1000 * k1000},
        Suffix{"TB", k1000 * k1000 * k1000 * k1000},
        Suffix{"PB", k1000 * k1000 * k1000 * k1000 * k1000},
        Suffix{"EB", k1000 * k1000 * k1000 * k1000 * k1000 * k1000},
        Suffix{"K", k1024},
        Suffix{"M", k1024 * k1024},
        Suffix{"G", k1024 * k1024 * k1024},
        Suffix{"T", k1024 * k1024 * k1024 * k1024},
        Suffix{"P", k1024 * k1024 * k1024 * k1024 * k1024},
        Suffix{"E", k1024 * k1024 * k1024 * k1024 * k1024 * k1024},
        Suffix{"k", k1024},
        Suffix{"m", k1024 * k1024},
        Suffix{"g", k1024 * k1024 * k1024},
        Suffix{"t", k1024 * k1024 * k1024 * k1024},
        Suffix{"p", k1024 * k1024 * k1024 * k1024 * k1024},
        Suffix{"e", k1024 * k1024 * k1024 * k1024 * k1024 * k1024},
        Suffix{"b", 512},
    };

    for (const auto& suffix : suffixes) {
      if (consume_suffix(suffix.text, suffix.factor)) break;
    }

    if (s.empty()) return std::unexpected("invalid size");
    int64_t value = std::stoll(s);
    return checked_mul(value, multiplier);
  } catch (...) {
    return std::unexpected("invalid size format");
  }
}

auto parse_positive_i64(const std::string& value, std::string_view name)
    -> cp::Result<int64_t> {
  try {
    size_t consumed = 0;
    int64_t parsed = std::stoll(value, &consumed);
    if (consumed != value.size() || parsed <= 0) {
      return std::unexpected(std::string(name) + " must be positive");
    }
    return parsed;
  } catch (...) {
    return std::unexpected(std::string("invalid ") + std::string(name));
  }
}

auto parse_suffix_start(const std::string& value, int base)
    -> cp::Result<uint64_t> {
  if (value.empty()) return 0;
  try {
    size_t consumed = 0;
    uint64_t parsed = std::stoull(value, &consumed, base);
    if (consumed != value.size()) {
      return std::unexpected("invalid suffix start");
    }
    return parsed;
  } catch (...) {
    return std::unexpected("invalid suffix start");
  }
}

auto build_config(const CommandContext<SPLIT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  auto bytes_opt = ctx.get<std::string>("--bytes", "");
  if (bytes_opt.empty()) bytes_opt = ctx.get<std::string>("-b", "");
  auto line_bytes_opt = ctx.get<std::string>("--line-bytes", "");
  if (line_bytes_opt.empty()) {
    line_bytes_opt = ctx.get<std::string>("-C", "");
  }
  auto lines_opt = ctx.get<std::string>("--lines", "");
  if (lines_opt.empty()) lines_opt = ctx.get<std::string>("-l", "");

  int split_modes = 0;
  if (!bytes_opt.empty()) ++split_modes;
  if (!line_bytes_opt.empty()) ++split_modes;
  if (!lines_opt.empty()) ++split_modes;
  if (split_modes > 1) {
    return std::unexpected("cannot split in more than one way");
  }

  if (!bytes_opt.empty()) {
    auto size_result = parse_size(bytes_opt);
    if (!size_result) {
      return std::unexpected(size_result.error());
    }
    cfg.chunk_size = *size_result;
    cfg.mode = Config::Mode::Bytes;
  }

  if (!line_bytes_opt.empty()) {
    auto size_result = parse_size(line_bytes_opt);
    if (!size_result) {
      return std::unexpected(size_result.error());
    }
    cfg.chunk_size = *size_result;
    cfg.mode = Config::Mode::LineBytes;
  }

  if (!lines_opt.empty()) {
    auto lines_result = parse_positive_i64(lines_opt, "line count");
    if (!lines_result) return std::unexpected(lines_result.error());
    cfg.chunk_lines = *lines_result;
    cfg.mode = Config::Mode::Lines;
  }

  if (ctx.has("--numeric-suffixes") || ctx.has("-d")) {
    cfg.suffix_kind = Config::SuffixKind::Numeric;
    auto start = ctx.get<std::string>("--numeric-suffixes", "");
    if (start.empty()) start = ctx.get<std::string>("-d", "");
    auto start_result = parse_suffix_start(start, 10);
    if (!start_result) return std::unexpected(start_result.error());
    cfg.suffix_start = *start_result;
    cfg.suffix_start_explicit = !start.empty();
  }

  if (ctx.has("--hex-suffixes") || ctx.has("-x")) {
    cfg.suffix_kind = Config::SuffixKind::Hex;
    auto start = ctx.get<std::string>("--hex-suffixes", "");
    if (start.empty()) start = ctx.get<std::string>("-x", "");
    auto start_result = parse_suffix_start(start, 16);
    if (!start_result) return std::unexpected(start_result.error());
    cfg.suffix_start = *start_result;
    cfg.suffix_start_explicit = !start.empty();
  }

  auto suffix_opt = ctx.get<std::string>("--suffix-length", "");
  if (suffix_opt.empty()) {
    suffix_opt = ctx.get<std::string>("-a", "");
  }
  if (!suffix_opt.empty()) {
    try {
      cfg.suffix_length = std::stoi(suffix_opt);
      if (cfg.suffix_length < 0 || cfg.suffix_length > 32) {
        return std::unexpected("suffix length must be between 0 and 32");
      }
      cfg.suffix_length_explicit = cfg.suffix_length != 0;
      if (cfg.suffix_length == 0) cfg.suffix_length = 2;
    } catch (...) {
      return std::unexpected("invalid suffix length");
    }
  }

  cfg.additional_suffix = ctx.get<std::string>("--additional-suffix", "");
  if (cfg.additional_suffix.find('/') != std::string::npos ||
      cfg.additional_suffix.find('\\') != std::string::npos) {
    return std::unexpected("additional suffix must not contain slash");
  }

  auto input_result = resolve_input_file(ctx);
  if (!input_result) {
    return std::unexpected(input_result.error());
  }
  cfg.input_file = *input_result;

  if (ctx.positionals.size() > 1) {
    cfg.prefix = std::string(ctx.positionals[1]);
  }

  return cfg;
}

auto convert_unsigned(uint64_t value, uint32_t base) -> std::string {
  constexpr std::string_view digits = "0123456789abcdefghijklmnopqrstuvwxyz";
  std::string result;
  do {
    result.push_back(digits[value % base]);
    value /= base;
  } while (value != 0);
  std::reverse(result.begin(), result.end());
  return result;
}

auto alpha_suffix(uint64_t value, int length) -> cp::Result<std::string> {
  uint64_t capacity = 1;
  for (int i = 0; i < length; ++i) {
    if (capacity > std::numeric_limits<uint64_t>::max() / 26) {
      return std::unexpected("output file suffixes exhausted");
    }
    capacity *= 26;
  }
  if (value >= capacity) {
    return std::unexpected("output file suffixes exhausted");
  }

  std::string result(static_cast<size_t>(length), 'a');
  for (int i = length - 1; i >= 0; --i) {
    result[static_cast<size_t>(i)] = static_cast<char>('a' + value % 26);
    value /= 26;
  }
  return result;
}

auto generate_suffix(const Config& cfg, uint64_t part_num)
    -> cp::Result<std::string> {
  uint64_t value = cfg.suffix_start + part_num;
  if (value < cfg.suffix_start) {
    return std::unexpected("output file suffixes exhausted");
  }

  if (cfg.suffix_kind == Config::SuffixKind::Alpha) {
    int length = cfg.suffix_length;
    auto result = alpha_suffix(value, length);
    while (!result && !cfg.suffix_length_explicit && length < 32) {
      length += 2;
      result = alpha_suffix(value, length);
    }
    return result;
  }

  uint32_t base = cfg.suffix_kind == Config::SuffixKind::Hex ? 16 : 10;
  std::string suffix = convert_unsigned(value, base);
  int length = cfg.suffix_length;
  if (suffix.size() > static_cast<size_t>(length)) {
    if (cfg.suffix_length_explicit || cfg.suffix_start_explicit) {
      return std::unexpected("output file suffixes exhausted");
    }
    length = static_cast<int>(suffix.size());
  }
  if (suffix.size() < static_cast<size_t>(length)) {
    suffix.insert(suffix.begin(), static_cast<size_t>(length) - suffix.size(),
                  '0');
  }
  return suffix;
}

auto make_filename(const Config& cfg, uint64_t part_num)
    -> cp::Result<std::string> {
  auto suffix = generate_suffix(cfg, part_num);
  if (!suffix) return std::unexpected(suffix.error());
  return cfg.prefix + *suffix + cfg.additional_suffix;
}

auto write_chunk(const Config& cfg, uint64_t part_num, std::string_view chunk)
    -> cp::Result<int> {
  auto filename_result = make_filename(cfg, part_num);
  if (!filename_result) return std::unexpected(filename_result.error());
  const auto& filename = *filename_result;

  std::ofstream out(filename, std::ios::binary);
  if (!out) {
    return std::unexpected(std::string("cannot create '") + filename + "'");
  }
  out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
  if (!out) {
    return std::unexpected(std::string("error writing '") + filename + "'");
  }
  return 0;
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

  uint64_t part_num = 0;
  if (cfg.mode == Config::Mode::Lines) {
    size_t start = 0;
    int64_t lines_in_chunk = 0;
    for (size_t pos = 0; pos < input.size();) {
      size_t next = input.find('\n', pos);
      size_t record_end = next == std::string::npos ? input.size() : next + 1;
      ++lines_in_chunk;
      pos = record_end;

      if (lines_in_chunk == cfg.chunk_lines || pos == input.size()) {
        auto result = write_chunk(
            cfg, part_num++,
            std::string_view(input.data() + start, record_end - start));
        if (!result) {
          cp::report_error(result, L"split");
          return 1;
        }
        start = record_end;
        lines_in_chunk = 0;
      }
    }
  } else if (cfg.mode == Config::Mode::Bytes) {
    for (size_t pos = 0; pos < input.size();) {
      size_t chunk_size = static_cast<size_t>(std::min<int64_t>(
          cfg.chunk_size, static_cast<int64_t>(input.size() - pos)));
      auto result = write_chunk(
          cfg, part_num++, std::string_view(input.data() + pos, chunk_size));
      if (!result) {
        cp::report_error(result, L"split");
        return 1;
      }
      pos += chunk_size;
    }
  } else {
    for (size_t pos = 0; pos < input.size();) {
      size_t start = pos;
      size_t used = 0;

      while (pos < input.size()) {
        size_t next = input.find('\n', pos);
        size_t record_end = next == std::string::npos ? input.size() : next + 1;
        size_t record_size = record_end - pos;

        if (record_size > static_cast<size_t>(cfg.chunk_size)) {
          if (used == 0) pos += static_cast<size_t>(cfg.chunk_size);
          break;
        }
        if (used != 0 &&
            used + record_size > static_cast<size_t>(cfg.chunk_size)) {
          break;
        }

        used += record_size;
        pos = record_end;
        if (used == static_cast<size_t>(cfg.chunk_size)) break;
      }

      auto result = write_chunk(
          cfg, part_num++, std::string_view(input.data() + start, pos - start));
      if (!result) {
        cp::report_error(result, L"split");
        return 1;
      }
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
    "Note: This implementation supports common GNU line, byte, line-byte, "
    "and suffix options.",
    "  split -l 1000 largefile.txt\n"
    "  split -b 100M largefile.txt\n"
    "  split -C 64K log.txt chunk-\n"
    "  split -d -a 3 largefile.txt part\n"
    "  split -b 1M -x --additional-suffix=.bin input.dat output",
    "csplit(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SPLIT_OPTIONS) {
  using namespace split_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"split");
    return 1;
  }

  return run(*cfg_result);
}
