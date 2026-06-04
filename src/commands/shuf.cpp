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
 *  - File: shuf.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for shuf.
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

auto constexpr SHUF_OPTIONS = std::array{
    OPTION("-e", "--echo", "treat each ARG as an input line", BOOL_TYPE),
    OPTION("-i", "--input-range",
           "treat each number LO through HI as an input line", STRING_TYPE),
    OPTION("-n", "--head-count", "output at most COUNT lines", STRING_TYPE),
    OPTION("-o", "--output", "write result to FILE instead of standard output",
           STRING_TYPE),
    OPTION("-r", "--repeat", "output lines can be repeated", BOOL_TYPE),
    OPTION("", "--random-source", "get random bytes from FILE", STRING_TYPE),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline",
           BOOL_TYPE)};

namespace shuf_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool echo_mode = false;
  std::optional<size_t> head_count;
  bool repeat = false;
  bool zero_terminated = false;
  std::string input_range;
  std::string output_file;
  std::string random_source;
  SmallVector<std::string, 64> files;
  SmallVector<std::string, 64> echo_args;
};

auto parse_unsigned_decimal(std::string_view text, std::string_view diagnostic)
    -> cp::Result<uint64_t> {
  if (text.empty()) {
    return std::unexpected(diagnostic);
  }
  for (char ch : text) {
    if (!std::isdigit(static_cast<unsigned char>(ch))) {
      return std::unexpected(diagnostic);
    }
  }

  uint64_t value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size()) {
    return std::unexpected(diagnostic);
  }
  return value;
}

auto split_range(std::string_view range)
    -> cp::Result<std::pair<uint64_t, uint64_t>> {
  const auto dash_pos = range.find('-');
  if (dash_pos == std::string_view::npos ||
      range.find('-', dash_pos + 1) != std::string_view::npos) {
    return std::unexpected("invalid input range");
  }

  auto lo =
      parse_unsigned_decimal(range.substr(0, dash_pos), "invalid input range");
  if (!lo) return std::unexpected(lo.error());

  auto hi =
      parse_unsigned_decimal(range.substr(dash_pos + 1), "invalid input range");
  if (!hi) return std::unexpected(hi.error());

  if (*lo > *hi) {
    return std::unexpected("invalid input range");
  }

  return std::pair<uint64_t, uint64_t>{*lo, *hi};
}

void append_record(SmallVector<std::string, 1024>& records, std::string record,
                   bool skip_bom) {
  if (skip_bom && record.size() >= 3 &&
      static_cast<unsigned char>(record[0]) == 0xEF &&
      static_cast<unsigned char>(record[1]) == 0xBB &&
      static_cast<unsigned char>(record[2]) == 0xBF) {
    record = record.substr(3);
  }
  records.push_back(std::move(record));
}

auto read_records(std::istream& in, bool zero_terminated,
                  SmallVector<std::string, 1024>& records) -> cp::Result<int> {
  const char delimiter = zero_terminated ? '\0' : '\n';
  std::string record;
  bool first = records.empty();

  while (std::getline(in, record, delimiter)) {
    append_record(records, std::move(record), first);
    first = false;
    record.clear();
  }

  if (in.fail() && !in.eof()) {
    return std::unexpected("error reading from file");
  }

  return 0;
}

auto make_rng(const Config& cfg) -> cp::Result<std::mt19937> {
  if (cfg.random_source.empty()) {
    std::random_device rd;
    return std::mt19937(rd());
  }

  std::ifstream source(cfg.random_source, std::ios::binary);
  if (!source) {
    return std::unexpected("cannot open random source");
  }

  std::vector<uint32_t> seed_words;
  uint32_t word = 0;
  int shift = 0;
  char ch = 0;
  while (source.get(ch)) {
    word |= static_cast<uint32_t>(static_cast<unsigned char>(ch)) << shift;
    shift += 8;
    if (shift == 32) {
      seed_words.push_back(word);
      word = 0;
      shift = 0;
    }
  }

  if (source.fail() && !source.eof()) {
    return std::unexpected("error reading random source");
  }

  if (shift != 0) {
    seed_words.push_back(word);
  }
  if (seed_words.empty()) {
    seed_words.push_back(0);
  }

  std::seed_seq seed(seed_words.begin(), seed_words.end());
  return std::mt19937(seed);
}

void append_output_record(std::string& output, const std::string& record,
                          bool zero_terminated) {
  output.append(record);
  output.push_back(zero_terminated ? '\0' : '\n');
}

auto emit_output(const Config& cfg, std::string_view output) -> int {
  if (!cfg.output_file.empty()) {
    std::ofstream out(cfg.output_file, std::ios::binary | std::ios::trunc);
    if (!out) {
      cp::report_custom_error(L"shuf", L"cannot open output file");
      return 1;
    }
    out.write(output.data(), static_cast<std::streamsize>(output.size()));
    if (!out) {
      cp::report_custom_error(L"shuf", L"error writing output file");
      return 1;
    }
    return 0;
  }

  safePrint(output);
  return 0;
}

auto build_config(const CommandContext<SHUF_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.echo_mode = ctx.get<bool>("--echo", false) || ctx.get<bool>("-e", false);
  cfg.repeat = ctx.get<bool>("--repeat", false) || ctx.get<bool>("-r", false);
  cfg.zero_terminated =
      ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false);

  auto range_opt = ctx.get<std::string>("--input-range", "");
  if (range_opt.empty()) {
    range_opt = ctx.get<std::string>("-i", "");
  }
  cfg.input_range = range_opt;

  auto count_opt = ctx.get<std::string>("--head-count", "");
  if (count_opt.empty()) {
    count_opt = ctx.get<std::string>("-n", "");
  }
  if (!count_opt.empty()) {
    auto count = parse_unsigned_decimal(count_opt, "invalid line count");
    if (!count) return std::unexpected(count.error());
    if (*count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
      return std::unexpected("invalid line count");
    }
    cfg.head_count = static_cast<size_t>(*count);
  }

  cfg.output_file = ctx.get<std::string>("--output", "");
  if (cfg.output_file.empty()) {
    cfg.output_file = ctx.get<std::string>("-o", "");
  }
  cfg.random_source = ctx.get<std::string>("--random-source", "");

  if (cfg.echo_mode) {
    for (auto arg : ctx.positionals) {
      cfg.echo_args.push_back(std::string(arg));
    }
  } else {
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
  }

  return cfg;
}

auto run(const Config& cfg) -> int {
  SmallVector<std::string, 1024> lines;

  if (cfg.echo_mode) {
    // Echo mode: treat each ARG as an input line
    for (const auto& arg : cfg.echo_args) {
      lines.push_back(arg);
    }
  } else if (!cfg.input_range.empty()) {
    // Input range mode: generate numbers LO through HI
    auto range = split_range(cfg.input_range);
    if (!range) {
      cp::report_error(range, L"shuf");
      return 1;
    }
    for (uint64_t i = range->first; i <= range->second; ++i) {
      lines.push_back(std::to_string(i));
      if (i == std::numeric_limits<uint64_t>::max()) break;
    }
  } else {
    // File mode: read from files
    SmallVector<std::string, 64> files = cfg.files;
    if (files.empty()) {
      files.push_back("-");  // Read from stdin
    }

    for (const auto& file : files) {
      if (file == "-") {
        auto read_result = read_records(std::cin, cfg.zero_terminated, lines);
        if (!read_result) {
          cp::report_error(read_result, L"shuf");
          return 1;
        }
      } else {
        std::ifstream f(file, std::ios::binary);
        if (!f) {
          auto err = std::string("cannot open '") + file + "' for reading";
          cp::Result<int> result = std::unexpected(std::string_view(err));
          cp::report_error(result, L"shuf");
          return 1;
        }

        auto read_result = read_records(f, cfg.zero_terminated, lines);
        if (!read_result) {
          cp::report_error(read_result, L"shuf");
          return 1;
        }
      }
    }
  }

  if (lines.empty()) {
    return emit_output(cfg, std::string_view{});
  }

  auto rng_result = make_rng(cfg);
  if (!rng_result) {
    cp::report_error(rng_result, L"shuf");
    return 1;
  }
  auto g = *rng_result;

  std::string output;
  if (cfg.repeat) {
    std::uniform_int_distribution<size_t> dist(0, lines.size() - 1);
    if (cfg.head_count) {
      for (size_t i = 0; i < *cfg.head_count; ++i) {
        append_output_record(output, lines[dist(g)], cfg.zero_terminated);
      }
      return emit_output(cfg, output);
    }

    while (!is_stdout_pipe_closed()) {
      output.clear();
      for (size_t i = 0; i < 256 && !is_stdout_pipe_closed(); ++i) {
        append_output_record(output, lines[dist(g)], cfg.zero_terminated);
      }
      if (emit_output(cfg, output) != 0) return 1;
    }
    return 0;
  }

  // Shuffle using Fisher-Yates algorithm
  for (size_t i = lines.size(); i > 1; --i) {
    std::uniform_int_distribution<size_t> dist(0, i - 1);
    size_t j = dist(g);
    std::swap(lines[i - 1], lines[j]);
  }

  const size_t output_count =
      cfg.head_count ? std::min(*cfg.head_count, lines.size()) : lines.size();

  for (size_t i = 0; i < output_count; ++i) {
    append_output_record(output, lines[i], cfg.zero_terminated);
  }

  return emit_output(cfg, output);
}

}  // namespace shuf_pipeline

REGISTER_COMMAND(
    shuf, "shuf", "shuf [OPTION]... [FILE]",
    "Shuffle randomize lines.\n"
    "\n"
    "Write a random permutation of the input lines to standard output.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Note: This implementation uses the Mersenne Twister PRNG\n"
    "from the C++ standard library.",
    "  shuf file.txt\n"
    "  shuf -n 5 file.txt\n"
    "  shuf -e a b c d e\n"
    "  shuf -i 1-10\n"
    "  shuf -r -n 10 -i 1-6",
    "sort(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", SHUF_OPTIONS) {
  using namespace shuf_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"shuf");
    return 1;
  }

  return run(*cfg_result);
}
