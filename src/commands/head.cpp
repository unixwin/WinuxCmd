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

using cmd::meta::option_matches;
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
    // [GNU]
    OPTION("-c", "--bytes",
           "print the first NUM bytes of each file; with the leading '-',\n"
           "print all but the last NUM bytes",
           STRING_TYPE),
    // [GNU]
    OPTION("-n", "--lines",
           "print the first NUM lines instead of the first 10; with the\n"
           "leading '-', print all but the last NUM lines",
           STRING_TYPE),
    // [GNU]
    OPTION("-q", "--quiet",
           "never print headers giving file names for multiple files"),
    // [GNU]
    OPTION("", "--silent",
           "never print headers giving file names for multiple files"),
    // [GNU]
    OPTION("-v", "--verbose",
           "always print headers giving file names for multiple files"),
    // [GNU]
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

namespace head_pipeline {
namespace cp = core::pipeline;

struct CountSpec {
  std::uintmax_t value = 10;
  bool all_but_last = false;
  bool plus_form = false;
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

struct CountSuffix {
  std::string_view suffix;
  std::uintmax_t base;
  unsigned power;
};

auto checked_pow(std::uintmax_t base, unsigned power)
    -> std::optional<std::uintmax_t> {
  std::uintmax_t result = 1;
  for (unsigned i = 0; i < power; ++i) {
    if (result > std::numeric_limits<std::uintmax_t>::max() / base) {
      return std::nullopt;
    }
    result *= base;
  }
  return result;
}

auto apply_suffix_multiplier(std::uintmax_t value, std::string_view suffix)
    -> std::optional<std::uintmax_t> {
  static constexpr std::array suffixes{
      CountSuffix{"", 1, 0},       CountSuffix{"b", 512, 1},
      CountSuffix{"K", 1024, 1},   CountSuffix{"KB", 1000, 1},
      CountSuffix{"KiB", 1024, 1}, CountSuffix{"M", 1024, 2},
      CountSuffix{"MB", 1000, 2},  CountSuffix{"MiB", 1024, 2},
      CountSuffix{"G", 1024, 3},   CountSuffix{"GB", 1000, 3},
      CountSuffix{"GiB", 1024, 3}, CountSuffix{"T", 1024, 4},
      CountSuffix{"TB", 1000, 4},  CountSuffix{"TiB", 1024, 4},
      CountSuffix{"P", 1024, 5},   CountSuffix{"PB", 1000, 5},
      CountSuffix{"PiB", 1024, 5}, CountSuffix{"E", 1024, 6},
      CountSuffix{"EB", 1000, 6},  CountSuffix{"EiB", 1024, 6},
      CountSuffix{"Z", 1024, 7},   CountSuffix{"ZB", 1000, 7},
      CountSuffix{"ZiB", 1024, 7}, CountSuffix{"Y", 1024, 8},
      CountSuffix{"YB", 1000, 8},  CountSuffix{"YiB", 1024, 8},
      CountSuffix{"R", 1024, 9},   CountSuffix{"RB", 1000, 9},
      CountSuffix{"RiB", 1024, 9}, CountSuffix{"Q", 1024, 10},
      CountSuffix{"QB", 1000, 10}, CountSuffix{"QiB", 1024, 10}};

  for (const auto& entry : suffixes) {
    if (entry.suffix != suffix) continue;
    auto multiplier = checked_pow(entry.base, entry.power);
    if (!multiplier)
      return value == 0 ? std::optional<std::uintmax_t>{0} : std::nullopt;
    if (value > std::numeric_limits<std::uintmax_t>::max() / *multiplier) {
      return std::nullopt;
    }
    return value * *multiplier;
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

  return apply_suffix_multiplier(*parsed, text.substr(i));
}

auto parse_count_spec(std::string spec_text, std::string_view opt_name)
    -> cp::Result<CountSpec> {
  // [GNU] invalid-number diagnostics quote the offending operand:
  //   head: invalid number of lines: 'bogus'
  const std::string original = spec_text;
  auto invalid = [&]() {
    return std::unexpected("invalid number of " + std::string(opt_name) +
                           ": '" + original + "'");
  };
  if (spec_text.empty()) {
    return invalid();
  }

  CountSpec spec;
  if (spec_text[0] == '-') {
    spec.all_but_last = true;
    spec_text = spec_text.substr(1);  // Avoid modifying original string
    if (spec_text.empty()) {
      return invalid();
    }
  } else if (spec_text[0] == '+') {
    spec.plus_form = true;
    spec_text = spec_text.substr(1);
    if (spec_text.empty()) {
      return invalid();
    }
  }

  auto parsed = parse_numeric_with_suffix(spec_text);
  if (!parsed.has_value()) {
    return invalid();
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

auto stream_needs_text_decoding(std::istream& in) -> bool {
  auto original = in.tellg();
  if (original == std::streampos(-1)) return true;

  std::array<char, 4096> sample{};
  in.read(sample.data(), static_cast<std::streamsize>(sample.size()));
  auto got = static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0));
  in.clear();
  in.seekg(original);

  if (got >= 3 && static_cast<std::uint8_t>(sample[0]) == 0xEF &&
      static_cast<std::uint8_t>(sample[1]) == 0xBB &&
      static_cast<std::uint8_t>(sample[2]) == 0xBF) {
    return true;
  }
  if (got >= 2 && ((static_cast<std::uint8_t>(sample[0]) == 0xFF &&
                    static_cast<std::uint8_t>(sample[1]) == 0xFE) ||
                   (static_cast<std::uint8_t>(sample[0]) == 0xFE &&
                    static_cast<std::uint8_t>(sample[1]) == 0xFF))) {
    return true;
  }

  return std::find(sample.begin(), sample.begin() + got, '\0') !=
         sample.begin() + got;
}

auto streampos_to_size(std::streampos pos) -> std::uintmax_t {
  if (pos == std::streampos(-1)) return 0;
  auto offset = static_cast<std::streamoff>(pos);
  if (offset <= 0) return 0;
  return static_cast<std::uintmax_t>(offset);
}

auto seek_to_end(std::ifstream& input) -> std::optional<std::uintmax_t> {
  input.clear();
  input.seekg(0, std::ios::end);
  auto end = input.tellg();
  if (end == std::streampos(-1) || input.bad()) return std::nullopt;
  return streampos_to_size(end);
}

auto output_file_range(std::ifstream& input, std::uintmax_t start,
                       std::optional<std::uintmax_t> byte_count = std::nullopt)
    -> bool {
  input.clear();
  input.seekg(static_cast<std::streamoff>(start), std::ios::beg);
  if (input.bad()) return false;

  std::array<char, 64 * 1024> buffer{};
  std::uintmax_t remaining =
      byte_count.value_or(std::numeric_limits<std::uintmax_t>::max());
  while (remaining > 0 && input.good()) {
    const auto want = static_cast<std::streamsize>(std::min<std::uintmax_t>(
        remaining, static_cast<std::uintmax_t>(buffer.size())));
    input.read(buffer.data(), want);
    auto got =
        static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
    if (got == 0) break;
    safePrint(std::string_view(buffer.data(), got));
    if (byte_count.has_value()) remaining -= got;
  }
  return !input.bad();
}

auto reset_to_beginning(std::ifstream& input) -> void {
  input.clear();
  input.seekg(0, std::ios::beg);
}

auto output_head_seekable_bytes(std::ifstream& input, const HeadConfig& config)
    -> bool {
  if (!config.by_bytes || !config.spec.all_but_last) return false;
  auto end = seek_to_end(input);
  if (!end) return false;

  const std::uintmax_t elide = config.spec.value;
  if (elide == 0) return output_file_range(input, 0);
  if (elide >= *end) return true;
  return output_file_range(input, 0, *end - elide);
}

auto output_head_seekable_lines(std::ifstream& input, const HeadConfig& config)
    -> bool {
  if (config.by_bytes || !config.spec.all_but_last) return false;
  std::uintmax_t lines = config.spec.value;
  if (lines == 0) return output_file_range(input, 0);

  auto end = seek_to_end(input);
  if (!end) return false;
  if (*end == 0) return true;

  constexpr std::uintmax_t kChunkSize = 64 * 1024;
  std::vector<char> buffer(static_cast<size_t>(kChunkSize));
  std::uintmax_t pos = *end;
  bool checked_last_byte = false;

  while (pos > 0) {
    const std::uintmax_t chunk_start = pos > kChunkSize ? pos - kChunkSize : 0;
    const auto chunk_size = static_cast<size_t>(pos - chunk_start);
    input.clear();
    input.seekg(static_cast<std::streamoff>(chunk_start), std::ios::beg);
    input.read(buffer.data(), static_cast<std::streamsize>(chunk_size));
    auto got =
        static_cast<size_t>(std::max<std::streamsize>(input.gcount(), 0));
    if (got == 0 || input.bad()) return false;

    if (!checked_last_byte) {
      checked_last_byte = true;
      if (buffer[got - 1] != config.delimiter && lines > 0) --lines;
    }

    for (size_t i = got; i > 0; --i) {
      if (buffer[i - 1] != config.delimiter) continue;
      if (lines == 0) {
        return output_file_range(input, 0, chunk_start + i);
      }
      --lines;
    }
    pos = chunk_start;
  }

  return true;
}

auto output_first_records(std::istream& in, size_t records, char delimiter)
    -> void {
  if (records == 0) return;

  std::array<char, 64 * 1024> buffer{};
  while (records > 0 && in.good()) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto got = static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0));
    if (got == 0) break;

    size_t bytes_to_write = got;
    for (size_t i = 0; i < got; ++i) {
      if (buffer[i] == delimiter && --records == 0) {
        bytes_to_write = i + 1;
        break;
      }
    }
    safePrint(std::string_view(buffer.data(), bytes_to_write));
  }
}

auto stream_bytes(std::istream& in, size_t bytes) -> void {
  std::array<char, 64 * 1024> buffer{};
  while (bytes > 0 && in.good()) {
    const auto want = std::min(bytes, buffer.size());
    in.read(buffer.data(), static_cast<std::streamsize>(want));
    const auto got =
        static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0));
    if (got == 0) break;
    safePrint(std::string_view(buffer.data(), got));
    bytes -= got;
  }
}

auto output_head(std::istream& in, const HeadConfig& config) -> void {
  if (config.spec.plus_form) {
    if (config.by_bytes) {
      stream_bytes(in, static_cast<size_t>(config.spec.value));
    } else {
      output_first_records(in, static_cast<size_t>(config.spec.value),
                           config.delimiter);
    }
    return;
  }

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
  output_first_records(in, n, config.delimiter);
}

auto open_input_file(const std::string& file) -> std::ifstream {
  return file_io::open_binary_file(file);
}

auto describe_open_failure(const std::string& file) -> std::string {
  std::wstring wfile = utf8_to_wstring(file);
  DWORD attrs = native_path::attributes_w(utf8_to_wstring(file));
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return "No such file or directory";
  }
  if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return "Is a directory";
  }
  return "Permission denied";
}

auto is_directory_open_failure(std::string_view reason) -> bool {
  return reason == "Is a directory";
}

auto output_text_head(std::istream& in, const HeadConfig& config) -> void {
  std::istringstream decoded(read_text_stream(in));
  output_head(decoded, config);
}

template <size_t N>
auto build_config(const CommandContext<N>& ctx) -> cp::Result<HeadConfig> {
  HeadConfig config;
  config.delimiter = ctx.get<bool>("--zero-terminated", false) ? '\0' : '\n';

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= N) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];

    if (option_matches(meta, "-q", "--quiet") ||
        option_matches(meta, "", "--silent")) {
      config.quiet = true;
      config.verbose = false;
      continue;
    }
    if (option_matches(meta, "-v", "--verbose")) {
      config.verbose = true;
      config.quiet = false;
      continue;
    }

    auto value = std::get_if<std::string>(&occurrence.value);
    if (!value) continue;

    if (option_matches(meta, "-c", "--bytes")) {
      auto spec = parse_count_spec(*value, "bytes");
      if (!spec) return std::unexpected(spec.error());
      config.by_bytes = true;
      config.spec = *spec;
      continue;
    }
    if (option_matches(meta, "-n", "--lines")) {
      auto spec = parse_count_spec(*value, "lines");
      if (!spec) return std::unexpected(spec.error());
      config.by_bytes = false;
      config.spec = *spec;
      continue;
    }
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

    bool show_header = config.verbose || (multi && !config.quiet);
    // [GNU] The header separator and the gap between headers are always
    // "\n"; the -z NUL delimiter applies to output records only.
    auto emit_header = [&]() {
      if (!show_header) return;
      if (!first_print) safePrint("\n");
      safePrint("==> ");
      safePrint(file == "-" ? "standard input" : file);
      safePrint(" <==\n");
      first_print = false;
    };

    if (file == "-") {
      emit_header();
      // [GNU] A closed standard input (<&-) is a read error, not EOF (#973).
      if (file_io::stdin_is_bad()) {
        safeErrorPrint(
            "head: error reading 'standard input': Bad file descriptor\n");
        any_error = true;
        continue;
      }
      // stdin may be a live pipe.  Do not decode the whole stream before
      // applying the head limit: an unbounded producer such as `yes` must be
      // able to stop once the requested records have been emitted.
      output_head(std::cin, config);
      if (std::cin.bad()) {
        safeErrorPrint("head: error reading '-'\n");
        any_error = true;
      }
    } else {
      std::ifstream input = open_input_file(file);
      if (!input.is_open()) {
        std::string reason = describe_open_failure(file);
        if (is_directory_open_failure(reason)) {
          safeErrorPrint("head: error reading '");
          safeErrorPrint(file);
          safeErrorPrint("': ");
          safeErrorPrint(reason);
          safeErrorPrint("\n");
        } else {
          safeErrorPrint("head: cannot open '");
          safeErrorPrint(file);
          safeErrorPrint("' for reading: ");
          safeErrorPrint(reason);
          safeErrorPrint("\n");
        }
        any_error = true;
        continue;
      }

      emit_header();
      bool handled = false;
      if (config.by_bytes) {
        if (config.spec.all_but_last) {
          handled = output_head_seekable_bytes(input, config);
          if (!handled) reset_to_beginning(input);
        }
        if (!handled) output_head(input, config);
      } else if (config.delimiter == '\0') {
        if (config.spec.all_but_last) {
          handled = output_head_seekable_lines(input, config);
          if (!handled) reset_to_beginning(input);
        }
        if (!handled) output_head(input, config);
      } else if (!stream_needs_text_decoding(input)) {
        if (config.spec.all_but_last) {
          handled = output_head_seekable_lines(input, config);
          if (!handled) reset_to_beginning(input);
        }
        if (!handled) output_head(input, config);
      } else {
        output_text_head(input, config);
      }
      if (input.bad()) {
        safeErrorPrint("head: error reading '");
        safeErrorPrint(file);
        safeErrorPrint("'\n");
        any_error = true;
      }
    }
  }

  return any_error ? 1 : 0;
}
