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
    OPTION("", "--random-seed",
           "use STRING to initialize a reproducible random permutation",
           STRING_TYPE),
    OPTION("", "--random-source", "get random bytes from FILE", STRING_TYPE),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline",
           BOOL_TYPE)};

namespace shuf_pipeline {
namespace cp = core::pipeline;

struct Uint128Product {
  uint64_t low = 0;
  uint64_t high = 0;
};

auto multiply_u64(uint64_t lhs, uint64_t rhs) -> Uint128Product {
  const uint64_t lhs_lo = lhs & 0xFFFFFFFFull;
  const uint64_t lhs_hi = lhs >> 32;
  const uint64_t rhs_lo = rhs & 0xFFFFFFFFull;
  const uint64_t rhs_hi = rhs >> 32;

  const uint64_t p00 = lhs_lo * rhs_lo;
  const uint64_t p01 = lhs_lo * rhs_hi;
  const uint64_t p10 = lhs_hi * rhs_lo;
  const uint64_t p11 = lhs_hi * rhs_hi;

  const uint64_t middle =
      (p00 >> 32) + (p01 & 0xFFFFFFFFull) + (p10 & 0xFFFFFFFFull);

  Uint128Product product;
  product.low = (p00 & 0xFFFFFFFFull) | (middle << 32);
  product.high = p11 + (p01 >> 32) + (p10 >> 32) + (middle >> 32);
  return product;
}

struct Config {
  bool echo_mode = false;
  std::optional<size_t> head_count;
  bool repeat = false;
  bool zero_terminated = false;
  std::string input_range;
  std::string output_file;
  std::string random_seed;
  std::string random_source;
  SmallVector<std::string, 64> files;
  SmallVector<std::string, 64> echo_args;
};

class DeterministicRng {
 public:
  explicit DeterministicRng(std::string_view seed_text)
      : state_(make_seed_state(seed_text)),
        block_words_(generate_block(state_)),
        word_index_(0) {}

  auto next_u64() -> uint64_t {
    if (word_index_ >= block_words_.size()) {
      refill_block();
    }

    const uint64_t low = static_cast<uint64_t>(block_words_[word_index_]);
    const uint64_t high = static_cast<uint64_t>(block_words_[word_index_ + 1]);
    word_index_ += 2;
    return (high << 32) | low;
  }

  auto choose_from_range(uint64_t lo, uint64_t hi) -> uint64_t {
    return lo + generate_at_most(hi - lo);
  }

  auto choose_index(size_t size) -> size_t {
    return static_cast<size_t>(generate_at_most(
        static_cast<uint64_t>(size == 0 ? 0 : size - 1)));
  }

  template <typename T>
  auto choose_from_slice(const T& vals) -> decltype(vals[0]) {
    return vals[choose_index(vals.size())];
  }

  template <typename T>
  void shuffle_prefix(T& vals, size_t amount) {
    amount = std::min(amount, vals.size());
    for (size_t idx = 0; idx < amount; ++idx) {
      const auto offset = static_cast<size_t>(generate_at_most(
          static_cast<uint64_t>(vals.size() - idx - 1)));
      std::swap(vals[idx], vals[idx + offset]);
    }
  }

 private:
  static constexpr std::array<uint64_t, 24> keccak_round_constants_{
      0x0000000000000001ull, 0x0000000000008082ull,
      0x800000000000808Aull, 0x8000000080008000ull,
      0x000000000000808Bull, 0x0000000080000001ull,
      0x8000000080008081ull, 0x8000000000008009ull,
      0x000000000000008Aull, 0x0000000000000088ull,
      0x0000000080008009ull, 0x000000008000000Aull,
      0x000000008000808Bull, 0x800000000000008Bull,
      0x8000000000008089ull, 0x8000000000008003ull,
      0x8000000000008002ull, 0x8000000000000080ull,
      0x000000000000800Aull, 0x800000008000000Aull,
      0x8000000080008081ull, 0x8000000000008080ull,
      0x0000000080000001ull, 0x8000000080008008ull};

  static constexpr std::array<int, 25> keccak_rho_offsets_{
      0,  1,  62, 28, 27, 36, 44, 6,  55, 20, 3,  10, 43,
      25, 39, 41, 45, 15, 21, 8,  18, 2,  61, 56, 14};

  static constexpr std::array<int, 25> keccak_pi_indices_{
      0,  10, 20, 5,  15, 16, 1,  11, 21, 6,  7,  17, 2,
      12, 22, 23, 8,  18, 3,  13, 14, 24, 9,  19, 4};

  static constexpr std::array<uint32_t, 4> chacha_constants_{
      0x61707865u, 0x3320646Eu, 0x79622D32u, 0x6B206574u};

  static auto load64_le(const uint8_t* bytes) -> uint64_t {
    uint64_t value = 0;
    for (size_t i = 0; i < 8; ++i) {
      value |= static_cast<uint64_t>(bytes[i]) << (8 * i);
    }
    return value;
  }

  static void keccak_permute(std::array<uint64_t, 25>& state) {
    for (uint64_t round_constant : keccak_round_constants_) {
      std::array<uint64_t, 5> c{};
      std::array<uint64_t, 5> d{};
      for (int x = 0; x < 5; ++x) {
        c[x] =
            state[x] ^ state[x + 5] ^ state[x + 10] ^ state[x + 15] ^ state[x + 20];
      }
      for (int x = 0; x < 5; ++x) {
        d[x] = c[(x + 4) % 5] ^ std::rotl(c[(x + 1) % 5], 1);
      }
      for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
          state[x + 5 * y] ^= d[x];
        }
      }

      std::array<uint64_t, 25> b{};
      for (int i = 0; i < 25; ++i) {
        b[keccak_pi_indices_[i]] =
            std::rotl(state[i], keccak_rho_offsets_[i]);
      }

      for (int y = 0; y < 5; ++y) {
        for (int x = 0; x < 5; ++x) {
          state[x + 5 * y] =
              b[x + 5 * y] ^
              ((~b[((x + 1) % 5) + 5 * y]) & b[((x + 2) % 5) + 5 * y]);
        }
      }

      state[0] ^= round_constant;
    }
  }

  static auto sha3_256(std::string_view text) -> std::array<uint8_t, 32> {
    constexpr size_t rate_bytes = 136;
    constexpr size_t rate_words = rate_bytes / 8;

    std::array<uint64_t, 25> state{};
    size_t offset = 0;

    while (offset + rate_bytes <= text.size()) {
      const uint8_t* block =
          reinterpret_cast<const uint8_t*>(text.data() + offset);
      for (size_t i = 0; i < rate_words; ++i) {
        state[i] ^= load64_le(block + i * 8);
      }
      keccak_permute(state);
      offset += rate_bytes;
    }

    std::array<uint8_t, rate_bytes> tail{};
    const size_t remaining = text.size() - offset;
    for (size_t i = 0; i < remaining; ++i) {
      tail[i] = static_cast<uint8_t>(text[offset + i]);
    }
    tail[remaining] ^= 0x06u;
    tail[rate_bytes - 1] ^= 0x80u;
    for (size_t i = 0; i < rate_words; ++i) {
      state[i] ^= load64_le(tail.data() + i * 8);
    }
    keccak_permute(state);

    std::array<uint8_t, 32> out{};
    for (size_t i = 0; i < 4; ++i) {
      const uint64_t lane = state[i];
      for (size_t j = 0; j < 8; ++j) {
        out[i * 8 + j] = static_cast<uint8_t>((lane >> (8 * j)) & 0xFFu);
      }
    }
    return out;
  }

  static auto make_seed_state(std::string_view seed_text)
      -> std::array<uint32_t, 16> {
    std::array<uint32_t, 16> state{};
    const auto seed = sha3_256(seed_text);

    state[0] = chacha_constants_[0];
    state[1] = chacha_constants_[1];
    state[2] = chacha_constants_[2];
    state[3] = chacha_constants_[3];
    for (size_t i = 0; i < 8; ++i) {
      state[4 + i] = static_cast<uint32_t>(seed[i * 4]) |
                     (static_cast<uint32_t>(seed[i * 4 + 1]) << 8) |
                     (static_cast<uint32_t>(seed[i * 4 + 2]) << 16) |
                     (static_cast<uint32_t>(seed[i * 4 + 3]) << 24);
    }
    return state;
  }

  static void quarter_round(std::array<uint32_t, 16>& x, int a, int b, int c,
                            int d) {
    x[a] += x[b];
    x[d] = std::rotl(x[d] ^ x[a], 16);
    x[c] += x[d];
    x[b] = std::rotl(x[b] ^ x[c], 12);
    x[a] += x[b];
    x[d] = std::rotl(x[d] ^ x[a], 8);
    x[c] += x[d];
    x[b] = std::rotl(x[b] ^ x[c], 7);
  }

  static auto generate_block(const std::array<uint32_t, 16>& state)
      -> std::array<uint32_t, 16> {
    std::array<uint32_t, 16> working = state;
    for (int round = 0; round < 6; ++round) {
      quarter_round(working, 0, 4, 8, 12);
      quarter_round(working, 1, 5, 9, 13);
      quarter_round(working, 2, 6, 10, 14);
      quarter_round(working, 3, 7, 11, 15);
      quarter_round(working, 0, 5, 10, 15);
      quarter_round(working, 1, 6, 11, 12);
      quarter_round(working, 2, 7, 8, 13);
      quarter_round(working, 3, 4, 9, 14);
    }
    for (size_t i = 0; i < working.size(); ++i) {
      working[i] += state[i];
    }
    return working;
  }

  void refill_block() {
    state_[12] += 1;
    if (state_[12] == 0) {
      state_[13] += 1;
    }
    block_words_ = generate_block(state_);
    word_index_ = 0;
  }

  auto generate_at_most(uint64_t at_most) -> uint64_t {
    if (at_most == std::numeric_limits<uint64_t>::max()) {
      return next_u64();
    }

    const uint64_t span = at_most + 1;
    uint64_t sample = next_u64();
    auto product = multiply_u64(sample, span);
    uint64_t low = product.low;
    uint64_t high = product.high;
    if (low < span) {
      const uint64_t threshold = (0ull - span) % span;
      while (low < threshold) {
        sample = next_u64();
        product = multiply_u64(sample, span);
        low = product.low;
        high = product.high;
      }
    }
    return high;
  }

  std::array<uint32_t, 16> state_{};
  std::array<uint32_t, 16> block_words_{};
  size_t word_index_ = 0;
};

class CompatRandomSource {
 public:
  explicit CompatRandomSource(std::istream& in) : in_(in) {}

  auto choose_from_range(uint64_t lo, uint64_t hi) -> cp::Result<uint64_t> {
    auto offset = generate_at_most(hi - lo);
    if (!offset) return std::unexpected(offset.error());
    return lo + *offset;
  }

  template <typename T>
  auto choose_from_slice(const T& vals)
      -> cp::Result<std::decay_t<decltype(vals[0])>> {
    if (vals.empty()) {
      return std::unexpected("no lines to repeat");
    }
    auto idx = generate_at_most(static_cast<uint64_t>(vals.size() - 1));
    if (!idx) return std::unexpected(idx.error());
    return vals[static_cast<size_t>(*idx)];
  }

  template <typename T>
  auto shuffle_prefix(T& vals, size_t amount) -> cp::Result<size_t> {
    amount = std::min(amount, vals.size());
    for (size_t idx = 0; idx < amount; ++idx) {
      auto offset =
          generate_at_most(static_cast<uint64_t>(vals.size() - idx - 1));
      if (!offset) return std::unexpected(offset.error());
      std::swap(vals[idx], vals[idx + static_cast<size_t>(*offset)]);
    }
    return amount;
  }

 private:
  auto generate_at_most(uint64_t at_most) -> cp::Result<uint64_t> {
    while (entropy_ < at_most) {
      const int ch = in_.get();
      if (ch == std::char_traits<char>::eof()) {
        if (in_.eof()) {
          return std::unexpected("end of file");
        }
        return std::unexpected("error reading random source");
      }
      state_ = state_ * 256 +
               static_cast<uint64_t>(static_cast<unsigned char>(ch));
      entropy_ = entropy_ * 256 + 255;
    }

    if (at_most == std::numeric_limits<uint64_t>::max()) {
      const uint64_t value = state_;
      state_ = 0;
      entropy_ = 0;
      return value;
    }

    const uint64_t possibilities = at_most + 1;
    uint64_t margin = 0;
    if (entropy_ == std::numeric_limits<uint64_t>::max()) {
      margin = (0ull - possibilities) % possibilities;
    } else {
      margin = (entropy_ + 1) % possibilities;
    }
    const uint64_t safe_zone = entropy_ - margin;

    if (state_ <= safe_zone) {
      const uint64_t value = state_ % possibilities;
      state_ /= possibilities;
      entropy_ -= at_most;
      entropy_ /= possibilities;
      return value;
    }

    state_ %= possibilities;
    entropy_ %= possibilities;
    return generate_at_most(at_most);
  }

  std::istream& in_;
  uint64_t state_ = 0;
  uint64_t entropy_ = 0;
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
                   bool skip_bom, bool zero_terminated) {
  if (skip_bom && record.size() >= 3 &&
      static_cast<unsigned char>(record[0]) == 0xEF &&
      static_cast<unsigned char>(record[1]) == 0xBB &&
      static_cast<unsigned char>(record[2]) == 0xBF) {
    record = record.substr(3);
  }
  if (!zero_terminated && !record.empty() && record.back() == '\r') {
    record.pop_back();
  }
  records.push_back(std::move(record));
}

auto read_records(std::istream& in, bool zero_terminated,
                  SmallVector<std::string, 1024>& records) -> cp::Result<int> {
  const char delimiter = zero_terminated ? '\0' : '\n';
  std::string record;
  bool first = records.empty();

  while (std::getline(in, record, delimiter)) {
    append_record(records, std::move(record), first, zero_terminated);
    first = false;
    record.clear();
  }

  if (in.fail() && !in.eof()) {
    return std::unexpected("error reading from file");
  }

  return 0;
}

auto make_rng(const Config& cfg) -> cp::Result<std::mt19937> {
  std::random_device rd;
  return std::mt19937(rd());
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

  std::optional<size_t> min_count;
  auto head_count_values = ctx.get_all<std::string>("--head-count");
  auto short_head_count_values = ctx.get_all<std::string>("-n");
  head_count_values.insert(head_count_values.end(), short_head_count_values.begin(),
                           short_head_count_values.end());
  for (const auto& count_opt : head_count_values) {
    auto count = parse_unsigned_decimal(count_opt, "invalid line count");
    if (!count) return std::unexpected(count.error());
    if (*count > static_cast<uint64_t>(std::numeric_limits<size_t>::max())) {
      return std::unexpected("invalid line count");
    }
    const size_t parsed = static_cast<size_t>(*count);
    min_count = min_count ? std::min(*min_count, parsed) : parsed;
  }
  if (min_count.has_value()) {
    cfg.head_count = *min_count;
  }

  cfg.output_file = ctx.get<std::string>("--output", "");
  if (cfg.output_file.empty()) {
    cfg.output_file = ctx.get<std::string>("-o", "");
  }
  cfg.random_seed = ctx.get<std::string>("--random-seed", "");
  cfg.random_source = ctx.get<std::string>("--random-source", "");
  if (!cfg.random_seed.empty() && !cfg.random_source.empty()) {
    return std::unexpected(
        "the arguments '--random-seed <STRING>' and '--random-source <FILE>' "
        "are mutually exclusive");
  }

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
  auto shuf_input_open_error = [](std::string_view path) -> std::string {
    std::error_code ec;
    auto status = std::filesystem::status(std::filesystem::u8path(path), ec);
    if (!ec && status.type() == std::filesystem::file_type::directory) {
      return std::string("cannot open '") + std::string(path) +
             "' for reading: Is a directory";
    }
    return std::string("cannot open '") + std::string(path) +
           "' for reading: No such file or directory";
  };

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
          auto err = shuf_input_open_error(file);
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

  if (!cfg.random_source.empty()) {
    std::ifstream random_source(cfg.random_source, std::ios::binary);
    if (!random_source) {
      cp::Result<int> open_result = std::unexpected("cannot open random source");
      cp::report_error(open_result, L"shuf");
      return 1;
    }

    CompatRandomSource rng(random_source);
    std::string output;

    if (cfg.repeat) {
      if (cfg.head_count) {
        for (size_t i = 0; i < *cfg.head_count; ++i) {
          auto record = rng.choose_from_slice(lines);
          if (!record) {
            cp::report_error(record, L"shuf");
            return 1;
          }
          append_output_record(output, *record, cfg.zero_terminated);
        }
        return emit_output(cfg, output);
      }

      while (!is_stdout_pipe_closed()) {
        output.clear();
        for (size_t i = 0; i < 256 && !is_stdout_pipe_closed(); ++i) {
          auto record = rng.choose_from_slice(lines);
          if (!record) {
            cp::report_error(record, L"shuf");
            return 1;
          }
          append_output_record(output, *record, cfg.zero_terminated);
        }
        if (emit_output(cfg, output) != 0) return 1;
      }
      return 0;
    }

    const size_t output_count =
        cfg.head_count ? std::min(*cfg.head_count, lines.size()) : lines.size();
    auto shuffled = rng.shuffle_prefix(lines, output_count);
    if (!shuffled) {
      cp::report_error(shuffled, L"shuf");
      return 1;
    }
    for (size_t i = 0; i < *shuffled; ++i) {
      append_output_record(output, lines[i], cfg.zero_terminated);
    }
    return emit_output(cfg, output);
  }

  if (!cfg.random_seed.empty()) {
    DeterministicRng rng(cfg.random_seed);
    std::string output;

    if (cfg.repeat) {
      if (cfg.head_count) {
        for (size_t i = 0; i < *cfg.head_count; ++i) {
          append_output_record(output, rng.choose_from_slice(lines),
                               cfg.zero_terminated);
        }
        return emit_output(cfg, output);
      }

      while (!is_stdout_pipe_closed()) {
        output.clear();
        for (size_t i = 0; i < 256 && !is_stdout_pipe_closed(); ++i) {
          append_output_record(output, rng.choose_from_slice(lines),
                               cfg.zero_terminated);
        }
        if (emit_output(cfg, output) != 0) return 1;
      }
      return 0;
    }

    const size_t output_count =
        cfg.head_count ? std::min(*cfg.head_count, lines.size()) : lines.size();
    rng.shuffle_prefix(lines, output_count);
    for (size_t i = 0; i < output_count; ++i) {
      append_output_record(output, lines[i], cfg.zero_terminated);
    }
    return emit_output(cfg, output);
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
    "Note: --random-seed uses a reproducible SHA3-256 + ChaCha12 path,\n"
    "and --random-source follows the GNU/Microsoft-compatible entropy-\n"
    "recycling adapter.",
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
