/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: head.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief HEAD command options definition
 *
 * This array defines all the options supported by the head command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -c, @a --bytes: Print the first NUM bytes of each file; with the leading
 * '-', print all but the last NUM bytes [IMPLEMENTED]
 * - @a -n, @a --lines: Print the first NUM lines instead of the first 10; with
 * the
 *   leading '-', print all but the last NUM lines [IMPLEMENTED]
 * - @a
 * -NUM: Obsolete GNU-compatible shorthand for -n NUM [IMPLEMENTED]
 * - @a -q,
 * @a --quiet: Never print headers giving file names for multiple files
 * [IMPLEMENTED]
 * - @a --silent: Never print headers giving file names for
 * multiple files [IMPLEMENTED]
 * - @a -v, @a --verbose: Always print headers giving file names for multiple
 * files [IMPLEMENTED]
 * - @a -z, @a --zero-terminated: Line delimiter is NUL, not newline
 * [IMPLEMENTED]
 */
auto constexpr HEAD_OPTIONS = std::array{
    OPTION("-c", "--bytes",
           "print the first NUM bytes of each file; with the leading '-',\n"
           "print all but the last NUM bytes",
           STRING_TYPE),
    OPTION("-n", "--lines",
           "print the first NUM lines instead of the first 10; with the\n"
           "leading '-', print all but the last NUM lines",
           STRING_TYPE),
    OPTION("-q", "--quiet",
           "never print headers giving file names for multiple files"),
    OPTION("", "--silent",
           "never print headers giving file names for multiple files"),
    OPTION("-v", "--verbose",
           "always print headers giving file names for multiple files"),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

namespace head_pipeline {
namespace cp = core::pipeline;

struct CountSpec {
  std::uintmax_t value = 10;
  bool all_but_last = false;
};

struct HeadConfig {
  bool by_bytes = false;
  CountSpec spec;
  bool quiet = false;
  bool verbose = false;
  char delimiter = '\n';
};

auto parse_uint(std::string_view text) -> std::optional<std::uintmax_t> {
  if (text.empty()) return std::nullopt;
  std::uintmax_t value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size()) {
    return std::nullopt;
  }
  return value;
}

auto suffix_multiplier(std::string_view suffix)
    -> std::optional<std::uintmax_t> {
  static constexpr auto kMultipliers =
      make_constexpr_map<std::string_view, std::uintmax_t>(
          std::array<std::pair<std::string_view, std::uintmax_t>, 25>{
              std::pair{std::string_view{"b"}, 512ULL},
              std::pair{std::string_view{"kB"}, 1000ULL},
              std::pair{std::string_view{"K"}, 1024ULL},
              std::pair{std::string_view{"KiB"}, 1024ULL},
              std::pair{std::string_view{"MB"}, 1000ULL * 1000ULL},
              std::pair{std::string_view{"M"}, 1024ULL * 1024ULL},
              std::pair{std::string_view{"MiB"}, 1024ULL * 1024ULL},
              std::pair{std::string_view{"GB"}, 1000ULL * 1000ULL * 1000ULL},
              std::pair{std::string_view{"G"}, 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"GiB"}, 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"TB"},
                        1000ULL * 1000ULL * 1000ULL * 1000ULL},
              std::pair{std::string_view{"T"},
                        1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"TiB"},
                        1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"PB"},
                        1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL},
              std::pair{std::string_view{"P"},
                        1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"PiB"},
                        1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{
                  std::string_view{"EB"},
                  1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL},
              std::pair{std::string_view{"E"}, 1024ULL * 1024ULL * 1024ULL *
                                                   1024ULL * 1024ULL * 1024ULL},
              std::pair{
                  std::string_view{"EiB"},
                  1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"Z"}, 1024ULL * 1024ULL * 1024ULL *
                                                   1024ULL * 1024ULL * 1024ULL *
                                                   1024ULL},
              std::pair{std::string_view{"Y"}, 1024ULL * 1024ULL * 1024ULL *
                                                   1024ULL * 1024ULL * 1024ULL *
                                                   1024ULL * 1024ULL},
              std::pair{std::string_view{"R"}, 0ULL},
              std::pair{std::string_view{"Q"}, 0ULL},
              std::pair{std::string_view{"ZB"}, 0ULL},
              std::pair{std::string_view{"YB"}, 0ULL}});

  if (suffix.empty()) return 1;

  if (auto it = kMultipliers.find(suffix); it != kMultipliers.end()) {
    auto mult = it->second;
    if (mult == 0ULL) return std::nullopt;
    return mult;
  }
  if (suffix == "RB" || suffix == "QB") {
    return std::nullopt;
  }

  return std::nullopt;
}

auto parse_numeric_with_suffix(std::string_view text)
    -> std::optional<std::uintmax_t> {
  if (text.empty()) return std::nullopt;

  size_t i = 0;
  while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) {
    ++i;
  }
  if (i == 0) return std::nullopt;

  auto parsed = parse_uint(text.substr(0, i));
  if (!parsed.has_value()) return std::nullopt;

  auto mult = suffix_multiplier(text.substr(i));
  if (!mult.has_value()) return std::nullopt;

  if (*mult != 0 &&
      *parsed > (std::numeric_limits<std::uintmax_t>::max() / *mult)) {
    return std::nullopt;
  }

  return *parsed * *mult;
}

auto parse_count_spec(std::string spec_text, std::string_view opt_name)
    -> cp::Result<CountSpec> {
  if (spec_text.empty()) {
    return std::unexpected("invalid number of " + std::string(opt_name));
  }

  CountSpec spec;
  if (spec_text[0] == '-') {
    spec.all_but_last = true;
    spec_text = spec_text.substr(1);  // Avoid modifying original string
    if (spec_text.empty()) {
      return std::unexpected("invalid number of " + std::string(opt_name));
    }
  }

  auto parsed = parse_numeric_with_suffix(spec_text);
  if (!parsed.has_value()) {
    return std::unexpected("invalid number of " + std::string(opt_name));
  }
  spec.value = *parsed;
  return spec;
}

auto stream_all(std::istream& in) -> void {
  std::array<char, 8192> buffer{};
  while (in.good()) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto got = in.gcount();
    if (got <= 0) break;
    safePrint(std::string_view(buffer.data(), static_cast<size_t>(got)));
  }
}

auto output_head(std::istream& in, const HeadConfig& config) -> void {
  if (config.by_bytes) {
    size_t n = static_cast<size_t>(config.spec.value);
    if (config.spec.all_but_last) {
      if (n == 0) {
        stream_all(in);
        return;
      }

      std::deque<char> trailing;
      std::array<char, 8192> buffer{};
      while (in.good()) {
        in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        auto got = in.gcount();
        if (got <= 0) break;

        for (std::streamsize i = 0; i < got; ++i) {
          trailing.push_back(buffer[static_cast<size_t>(i)]);
          if (trailing.size() > n) {
            safePrint(trailing.front());
            trailing.pop_front();
          }
        }
      }
      return;
    }

    size_t remaining = n;
    std::array<char, 8192> buffer{};
    while (remaining > 0 && in.good()) {
      size_t chunk = std::min(remaining, buffer.size());
      in.read(buffer.data(), static_cast<std::streamsize>(chunk));
      auto got = in.gcount();
      if (got <= 0) break;
      safePrint(std::string_view(buffer.data(), static_cast<size_t>(got)));
      remaining -= static_cast<size_t>(got);
    }
    return;
  }

  size_t n = static_cast<size_t>(config.spec.value);
  if (config.spec.all_but_last) {
    if (n == 0) {
      stream_all(in);
      return;
    }

    std::deque<std::string> trailing_records;
    std::string current;
    char ch = '\0';
    while (in.get(ch)) {
      current.push_back(ch);
      if (ch == config.delimiter) {
        trailing_records.push_back(std::move(current));
        current.clear();
        if (trailing_records.size() > n) {
          safePrint(trailing_records.front());
          trailing_records.pop_front();
        }
      }
    }

    if (!current.empty()) {
      trailing_records.push_back(std::move(current));
      if (trailing_records.size() > n) {
        safePrint(trailing_records.front());
        trailing_records.pop_front();
      }
    }
    return;
  }

  if (n == 0) return;
  char ch = '\0';
  while (in.get(ch)) {
    safePrint(ch);
    if (ch == config.delimiter) {
      --n;
      if (n == 0) break;
    }
  }
}

template <size_t N>
auto build_config(const CommandContext<N>& ctx) -> cp::Result<HeadConfig> {
  HeadConfig config;
  config.quiet =
      ctx.get<bool>("--quiet", false) || ctx.get<bool>("--silent", false);
  config.verbose = ctx.get<bool>("--verbose", false);
  config.delimiter = ctx.get<bool>("--zero-terminated", false) ? '\0' : '\n';

  const std::string bytes_arg = ctx.get<std::string>("--bytes", "");
  const std::string bytes_short = ctx.get<std::string>("-c", "");
  const std::string lines_arg = ctx.get<std::string>("--lines", "");
  const std::string lines_short = ctx.get<std::string>("-n", "");

  std::string bytes_spec = bytes_arg.empty() ? bytes_short : bytes_arg;
  std::string lines_spec = lines_arg.empty() ? lines_short : lines_arg;

  if (!bytes_spec.empty()) {
    auto spec = parse_count_spec(bytes_spec, "bytes");
    if (!spec) return std::unexpected(spec.error());
    config.by_bytes = true;
    config.spec = *spec;
    return config;
  }

  if (!lines_spec.empty()) {
    auto spec = parse_count_spec(lines_spec, "lines");
    if (!spec) return std::unexpected(spec.error());
    config.spec = *spec;
  }

  return config;
}

}  // namespace head_pipeline

REGISTER_COMMAND(
    head, "head", "head [OPTION]... [FILE]...",
    "Print the first 10 lines of each FILE to standard output.\n"
    "With more than one FILE, precede each with a header giving the file "
    "name.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.",
    "  head file.txt\n"
    "  head -n 20 file.txt\n"
    "  head -20 file.txt\n"
    "  head -c 64 file.txt\n"
    "  head -n -5 file.txt\n"
    "  head -v a.txt b.txt",
    "tail(1), cat(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", HEAD_OPTIONS) {
  using namespace head_pipeline;

  auto config_result = build_config(ctx);
  if (!config_result) {
    cp::report_error(config_result, L"head");
    return 1;
  }
  auto config = *config_result;

  // Use SmallVector for files (max 64 files) - all stack-allocated
  SmallVector<std::string, 64> files{};
  for (auto p : ctx.positionals) {
    std::string file_arg(p);
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
  if (files.empty()) files.push_back("-");

  bool any_error = false;
  bool first_print = true;
  bool multi = files.size() > 1;

  for (size_t i = 0; i < files.size(); ++i) {
    const auto& file = files[i];

    bool show_header =
        (config.verbose || (multi && !config.quiet)) && file != "-";
    if (show_header) {
      if (!first_print) safePrint("\n");
      safePrint("==> ");
      safePrint(file);
      safePrint(" <==\n");
    }

    if (file == "-") {
      output_head(std::cin, config);
      if (std::cin.bad()) {
        safeErrorPrint("head: error reading '-'\n");
        any_error = true;
      }
    } else {
      std::ifstream input(file, std::ios::binary);
      if (!input.is_open()) {
        safeErrorPrint("head: cannot open '");
        safeErrorPrint(file);
        safeErrorPrint("'\n");
        any_error = true;
        continue;
      }

      output_head(input, config);
      if (input.bad()) {
        safeErrorPrint("head: error reading '");
        safeErrorPrint(file);
        safeErrorPrint("'\n");
        any_error = true;
      }
    }

    first_print = false;
  }

  return any_error ? 1 : 0;
}
