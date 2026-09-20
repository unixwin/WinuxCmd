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
    // [GNU]
    OPTION("-b", "--print-bytes", "print differing bytes", BOOL_TYPE),
    // [GNU]
    OPTION("-i", "--ignore-initial", "skip specified number of initial bytes",
           STRING_TYPE),
    // [GNU]
    OPTION("-l", "--verbose", "output byte numbers of differing bytes",
           BOOL_TYPE),
    // [GNU]
    OPTION("-n", "--bytes", "compare specified number of bytes", STRING_TYPE),
    // [GNU]
    OPTION("-s", "--quiet", "silent mode", BOOL_TYPE)};

namespace cmp_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool print_bytes = false;
  bool verbose = false;
  bool quiet = false;
  size_t skip_bytes_file1 = 0;
  size_t skip_bytes_file2 = 0;
  size_t max_bytes = SIZE_MAX;
  SmallVector<std::string, 64> files;
};

struct CountSuffix {
  std::string_view suffix;
  std::uintmax_t multiplier;
};

auto parse_unsigned_with_base_prefix(std::string_view text)
    -> std::optional<std::uintmax_t> {
  if (text.empty()) return std::nullopt;

  int base = 10;
  size_t prefix_len = 0;
  if (text.size() > 2 && text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
    base = 16;
    prefix_len = 2;
  } else if (text.size() > 1 && text[0] == '0') {
    base = 8;
  }

  auto digits = text.substr(prefix_len);
  if (digits.empty()) return std::nullopt;

  for (char ch : digits) {
    unsigned char uch = static_cast<unsigned char>(ch);
    bool ok =
        std::isdigit(uch) ||
        (base == 16 && ((ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F')));
    if (!ok) return std::nullopt;
  }

  std::uintmax_t value = 0;
  auto* begin = digits.data();
  auto* end = digits.data() + digits.size();
  auto [ptr, ec] = std::from_chars(begin, end, value, base);
  if (ec != std::errc() || ptr != end) return std::nullopt;
  return value;
}

auto parse_byte_count(std::string_view text) -> std::optional<size_t> {
  if (text.empty()) return std::nullopt;

  static constexpr std::array suffixes{
      CountSuffix{"", 1},
      CountSuffix{"c", 1},
      CountSuffix{"w", 2},
      CountSuffix{"b", 512},
      CountSuffix{"kB", 1000ULL},
      CountSuffix{"K", 1024ULL},
      CountSuffix{"KiB", 1024ULL},
      CountSuffix{"MB", 1000ULL * 1000ULL},
      CountSuffix{"M", 1024ULL * 1024ULL},
      CountSuffix{"MiB", 1024ULL * 1024ULL},
      CountSuffix{"GB", 1000ULL * 1000ULL * 1000ULL},
      CountSuffix{"G", 1024ULL * 1024ULL * 1024ULL},
      CountSuffix{"GiB", 1024ULL * 1024ULL * 1024ULL},
      CountSuffix{"TB", 1000ULL * 1000ULL * 1000ULL * 1000ULL},
      CountSuffix{"T", 1024ULL * 1024ULL * 1024ULL * 1024ULL},
      CountSuffix{"TiB", 1024ULL * 1024ULL * 1024ULL * 1024ULL}};

  for (const auto& entry : suffixes) {
    if (text.size() < entry.suffix.size()) continue;
    if (!text.ends_with(entry.suffix)) continue;

    auto numeric_part = text.substr(0, text.size() - entry.suffix.size());
    auto parsed = parse_unsigned_with_base_prefix(numeric_part);
    if (!parsed) return std::nullopt;
    if (*parsed > std::numeric_limits<size_t>::max() / entry.multiplier) {
      return std::nullopt;
    }
    return static_cast<size_t>(*parsed * entry.multiplier);
  }

  return std::nullopt;
}

auto parse_skip_pair(std::string_view text)
    -> std::optional<std::pair<size_t, size_t>> {
  if (text.empty()) return std::nullopt;

  auto colon = text.find(':');
  if (colon == std::string_view::npos) {
    auto count = parse_byte_count(text);
    if (!count) return std::nullopt;
    return std::pair<size_t, size_t>{*count, *count};
  }

  auto left = parse_byte_count(text.substr(0, colon));
  auto right = parse_byte_count(text.substr(colon + 1));
  if (!left || !right) return std::nullopt;
  return std::pair<size_t, size_t>{*left, *right};
}

auto build_config(const CommandContext<CMP_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.print_bytes =
      ctx.get<bool>("--print-bytes", false) || ctx.get<bool>("-b", false);
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= CMP_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];

    if (meta.long_name == "--verbose" || meta.short_name == "-l") {
      cfg.verbose = true;
      cfg.quiet = false;
      continue;
    }

    if (meta.long_name == "--quiet" || meta.short_name == "-s") {
      cfg.quiet = true;
      cfg.verbose = false;
      continue;
    }
  }

  auto skip_opt = ctx.get<std::string>("--ignore-initial", "");
  if (skip_opt.empty()) {
    skip_opt = ctx.get<std::string>("-i", "");
  }
  if (!skip_opt.empty()) {
    auto skip_pair = parse_skip_pair(skip_opt);
    if (!skip_pair) {
      return std::unexpected("invalid skip count");
    }
    cfg.skip_bytes_file1 = skip_pair->first;
    cfg.skip_bytes_file2 = skip_pair->second;
  }

  auto bytes_opt = ctx.get<std::string>("--bytes", "");
  if (bytes_opt.empty()) {
    bytes_opt = ctx.get<std::string>("-n", "");
  }
  if (!bytes_opt.empty()) {
    auto parsed_bytes = parse_byte_count(bytes_opt);
    if (!parsed_bytes) {
      return std::unexpected("invalid byte count");
    }
    cfg.max_bytes = *parsed_bytes;
  }

  SmallVector<std::string, 8> raw_positionals;
  for (auto arg : ctx.positionals) {
    raw_positionals.push_back(std::string(arg));
  }

  if (raw_positionals.size() < 2) {
    return std::unexpected(
        "missing operand after '" +
        (raw_positionals.empty() ? std::string() : raw_positionals[0]) + "'");
  }
  if (raw_positionals.size() > 4) {
    return std::unexpected("extra operand '" + raw_positionals[4] + "'");
  }

  for (size_t i = 0; i < 2; ++i) {
    const auto& file_arg = raw_positionals[i];
    if (!contains_wildcard(file_arg)) {
      cfg.files.push_back(file_arg);
      continue;
    }

    auto glob_result = glob_expand(file_arg);
    if (glob_result.expanded) {
      for (const auto& file : glob_result.files) {
        cfg.files.push_back(wstring_to_utf8(file));
      }
      continue;
    }

    cfg.files.push_back(file_arg);
  }

  if (cfg.files.size() < 2) {
    return std::unexpected("missing operand after '" + cfg.files[0] + "'");
  }
  if (cfg.files.size() > 2) {
    return std::unexpected("extra operand '" + cfg.files[2] + "'");
  }

  if (raw_positionals.size() >= 3) {
    auto skip1 = parse_byte_count(raw_positionals[2]);
    if (!skip1) return std::unexpected("invalid skip count");
    cfg.skip_bytes_file1 = *skip1;
    cfg.skip_bytes_file2 = *skip1;
  }

  if (raw_positionals.size() >= 4) {
    auto skip2 = parse_byte_count(raw_positionals[3]);
    if (!skip2) return std::unexpected("invalid skip count");
    cfg.skip_bytes_file2 = *skip2;
  }

  return cfg;
}

auto report_open_error(const Config& cfg, const std::string& file) -> void {
  if (cfg.quiet) return;
  auto err = std::string("cmp: ") + file + ": No such file";
  cp::Result<int> result = std::unexpected(err);
  cp::report_error(result, L"cmp");
}

auto regular_remaining_after_skip(const std::string& file, size_t skip)
    -> std::optional<std::uintmax_t> {
  if (file == "-") return std::nullopt;

  std::error_code ec;
  auto path = std::filesystem::path(utf8_to_wstring(file));
  if (!std::filesystem::is_regular_file(path, ec) || ec) return std::nullopt;

  auto size = std::filesystem::file_size(path, ec);
  if (ec) return std::nullopt;
  if (size <= skip) return 0;
  return size - skip;
}

auto seek_to_skip(std::ifstream& file, size_t skip) -> void {
  if (skip == 0) return;
  if (skip > static_cast<size_t>(std::numeric_limits<std::streamoff>::max())) {
    file.seekg(0, std::ios::end);
    return;
  }
  file.seekg(static_cast<std::streamoff>(skip), std::ios::beg);
  if (!file) {
    file.clear();
    file.seekg(0, std::ios::end);
  }
}

auto discard_stdin_prefix(size_t skip) -> void {
  std::array<char, 64 * 1024> scratch{};
  size_t remaining = skip;
  while (remaining > 0 && std::cin.good()) {
    const size_t want = std::min(remaining, scratch.size());
    std::cin.read(scratch.data(), static_cast<std::streamsize>(want));
    const auto got =
        static_cast<size_t>(std::max<std::streamsize>(std::cin.gcount(), 0));
    if (got == 0) break;
    remaining -= got;
  }
}

auto count_newlines(std::span<const char> bytes) -> size_t {
  return static_cast<size_t>(
      std::count(bytes.begin(), bytes.end(), static_cast<char>('\n')));
}

auto first_difference(const char* lhs, const char* rhs, size_t count)
    -> size_t {
  for (size_t i = 0; i < count; ++i) {
    if (lhs[i] != rhs[i]) return i;
  }
  return count;
}

auto print_difference(const Config& cfg, const std::string& file1,
                      const std::string& file2, size_t display_pos,
                      size_t line_number, unsigned char c1, unsigned char c2)
    -> void {
  // [GNU] -l lists every differing byte; -b prints only the first
  // difference, appended to the normal "differ" message (cmp.c).
  if (cfg.verbose) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%zu %3o %3o", display_pos, c1, c2);
    safePrintLn(buf);
    return;
  }

  safePrint(file1);
  safePrint(" ");
  safePrint(file2);
  safePrint(" differ: byte ");
  safePrint(std::to_string(display_pos));
  safePrint(", line ");
  safePrint(std::to_string(line_number));
  if (cfg.print_bytes) {
    char buf[32];
    snprintf(buf, sizeof(buf), " is %3o %3o", c1, c2);
    safePrint(buf);
  }
  safePrintLn("");
}

auto run(const Config& cfg) -> int {
  const std::string& file1 = cfg.files[0];
  const std::string& file2 = cfg.files[1];

  if (file1 == "-" && file2 == "-" &&
      cfg.skip_bytes_file1 == cfg.skip_bytes_file2) {
    return 0;
  }

  std::ifstream stream1;
  std::ifstream stream2;
  std::istream* in1 = &std::cin;
  std::istream* in2 = &std::cin;

  if (file1 != "-") {
    stream1.open(std::filesystem::path(utf8_to_wstring(
                     native_path::normalize_api_operand(file1))),
                 std::ios::binary);
    if (!stream1) {
      report_open_error(cfg, file1);
      return 2;
    }
    in1 = &stream1;
  }

  if (file2 != "-") {
    stream2.open(std::filesystem::path(utf8_to_wstring(
                     native_path::normalize_api_operand(file2))),
                 std::ios::binary);
    if (!stream2) {
      report_open_error(cfg, file2);
      return 2;
    }
    in2 = &stream2;
  }

  if (file1 != "-" && file2 != "-" &&
      cfg.skip_bytes_file1 == cfg.skip_bytes_file2) {
    std::error_code ec;
    if (std::filesystem::equivalent(
            std::filesystem::path(
                utf8_to_wstring(native_path::normalize_api_operand(file1))),
            std::filesystem::path(
                utf8_to_wstring(native_path::normalize_api_operand(file2))),
            ec) &&
        !ec) {
      return 0;
    }
  }

  auto remaining1 = regular_remaining_after_skip(file1, cfg.skip_bytes_file1);
  auto remaining2 = regular_remaining_after_skip(file2, cfg.skip_bytes_file2);
  if (cfg.quiet && remaining1 && remaining2 && *remaining1 != *remaining2 &&
      std::min(*remaining1, *remaining2) < cfg.max_bytes) {
    return 1;
  }

  if (file1 == "-") {
    discard_stdin_prefix(cfg.skip_bytes_file1);
  } else {
    seek_to_skip(stream1, cfg.skip_bytes_file1);
  }

  if (file2 == "-") {
    discard_stdin_prefix(cfg.skip_bytes_file2);
  } else {
    seek_to_skip(stream2, cfg.skip_bytes_file2);
  }

  constexpr size_t kBufferSize = 1024 * 1024;
  std::vector<char> buffer1(kBufferSize);
  std::vector<char> buffer2(kBufferSize);

  size_t line_number = 1;
  size_t compared = 0;
  size_t eof_newlines = 0;
  bool found_difference = false;
  size_t remaining_limit = cfg.max_bytes;

  while (remaining_limit > 0) {
    const size_t want = std::min(kBufferSize, remaining_limit);
    in1->read(buffer1.data(), static_cast<std::streamsize>(want));
    const auto read1 =
        static_cast<size_t>(std::max<std::streamsize>(in1->gcount(), 0));
    in2->read(buffer2.data(), static_cast<std::streamsize>(want));
    const auto read2 =
        static_cast<size_t>(std::max<std::streamsize>(in2->gcount(), 0));
    const size_t smaller = std::min(read1, read2);

    if (smaller > 0 &&
        std::memcmp(buffer1.data(), buffer2.data(), smaller) != 0) {
      size_t diff = first_difference(buffer1.data(), buffer2.data(), smaller);
      line_number +=
          count_newlines(std::span<const char>(buffer1.data(), diff));

      if (cfg.quiet) return 1;

      if (!cfg.verbose) {
        print_difference(cfg, file1, file2, compared + diff + 1, line_number,
                         static_cast<unsigned char>(buffer1[diff]),
                         static_cast<unsigned char>(buffer2[diff]));
        return 1;
      }

      found_difference = true;
      for (size_t i = diff; i < smaller; ++i) {
        unsigned char c1 = static_cast<unsigned char>(buffer1[i]);
        unsigned char c2 = static_cast<unsigned char>(buffer2[i]);
        if (c1 != c2) {
          print_difference(cfg, file1, file2, compared + i + 1, line_number, c1,
                           c2);
        }
      }
    } else if (!cfg.verbose) {
      line_number +=
          count_newlines(std::span<const char>(buffer1.data(), smaller));
    }

    compared += smaller;
    remaining_limit -= smaller;
    // Track the line count of the shorter stream for the EOF diagnostic.
    eof_newlines += count_newlines(std::span<const char>(
        (read1 < read2 ? buffer1 : buffer2).data(), smaller));

    if (read1 != read2) {
      // [GNU] "cmp: EOF on FILE which is empty" when the shorter side had
      // no data at all, otherwise "EOF on FILE after byte N, in line L"
      // where N is the number of bytes both streams had (cmp.c).
      if (!cfg.quiet) {
        const std::string& eof_file = read1 < read2 ? file1 : file2;
        if (compared == 0) {
          safePrint("cmp: EOF on ");
          safePrint(eof_file);
          safePrintLn(" which is empty");
        } else {
          const size_t line = 1 + eof_newlines;
          safePrint("cmp: EOF on ");
          safePrint(eof_file);
          safePrint(" after byte ");
          safePrint(std::to_string(compared));
          safePrint(", in line ");
          safePrintLn(std::to_string(line));
        }
      }
      return 1;
    }

    if (read1 == 0 || read1 < want) break;
  }

  return found_difference ? 1 : 0;
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
    "FILE1 FILE2 may be followed by SKIP1 and SKIP2 for GNU-compatible\n"
    "initial byte skipping.",
    "  cmp file1 file2\n"
    "  cmp -l file1 file2\n"
    "  cmp -b file1 file2\n"
    "  cmp -n 100 file1 file2\n"
    "  cmp -s file1 file2 && echo 'files are equal'",
    "diff(1), comm(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CMP_OPTIONS) {
  using namespace cmp_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    // [GNU] cmp exits 2 (EXIT_TROUBLE) for usage errors and trouble.
    if (cfg_result.error() == "missing operand after ''") {
      cp::report_custom_error(L"cmp", L"missing operand");
      safeErrorPrintLn("Try 'cmp --help' for more information.");
      return 2;
    }
    if (cfg_result.error().starts_with("missing operand after '")) {
      safeErrorPrint("cmp: ");
      safeErrorPrintLn(cfg_result.error());
      safeErrorPrintLn("Try 'cmp --help' for more information.");
      return 2;
    }
    if (cfg_result.error().starts_with("extra operand '")) {
      safeErrorPrint("cmp: ");
      safeErrorPrintLn(cfg_result.error());
      safeErrorPrintLn("Try 'cmp --help' for more information.");
      return 2;
    }
    cp::report_error(cfg_result, L"cmp");
    return 2;
  }

  return run(*cfg_result);
}
