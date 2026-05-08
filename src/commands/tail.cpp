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
 *  - File: tail.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

#include "pch/pch.h"

//include other header after pch.h

#include "core/command_macros.h"



import std;

import core;

import utils;

import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

/**
 * @brief TAIL command options definition
 *
 * This array defines all the options supported by the tail command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -c, @a --bytes: Output the last NUM bytes; or use -c +NUM to output
 *   starting with byte NUM of each file [IMPLEMENTED]
 * - @a -f, @a --follow: Output appended data as the file grows [NOT SUPPORT]
 * - @a -F: Same as --follow=name --retry [NOT SUPPORT]
 * - @a -n, @a --lines: Output the last NUM lines, instead of the last 10; or
 *   use -n +NUM to skip NUM-1 lines at the start [IMPLEMENTED]
 * - @a -NUM: Obsolete GNU-compatible shorthand for -n NUM [IMPLEMENTED]
 * - @a +NUM: Obsolete compatibility shorthand for -n +NUM [IMPLEMENTED]
 * - @a --max-unchanged-stats: With --follow=name, reopen a FILE which has not changed
 *   size after N iterations to see if it has been renamed [NOT SUPPORT]
 * - @a --pid: With -f, terminate after process ID, PID dies [NOT SUPPORT]
 * - @a -q, @a --quiet: Never output headers giving file names [IMPLEMENTED]
 * - @a --silent: Never output headers giving file names [IMPLEMENTED]
 * - @a --retry: Keep trying to open a file if it is inaccessible [NOT SUPPORT]
 * - @a -s, @a --sleep-interval: With -f, sleep for approximately N seconds between iterations
 *   [NOT SUPPORT]
 * - @a -v, @a --verbose: Always output headers giving file names [IMPLEMENTED]
 * - @a -z, @a --zero-terminated: Line delimiter is NUL, not newline [IMPLEMENTED]
 */
auto constexpr TAIL_OPTIONS = std::array{
    OPTION("-c", "--bytes",
           "output the last NUM bytes; or use -c +NUM to output\n"
           "starting with byte NUM of each file",
           STRING_TYPE),
    OPTION("-f", "--follow",
           "output appended data as the file grows"),
    OPTION("-F", "", "same as --follow=name --retry [NOT SUPPORT]"),
    OPTION("-n", "--lines",
           "output the last NUM lines, instead of the last 10; or\n"
           "use -n +NUM to skip NUM-1 lines at the start",
           STRING_TYPE),
    OPTION("", "--max-unchanged-stats",
           "with --follow=name, reopen a FILE which has not changed\n"
           "size after N iterations to see if it has been renamed\n"
           "[NOT SUPPORT]",
           INT_TYPE),
    OPTION("", "--pid",
           "with -f, terminate after process ID, PID dies [NOT SUPPORT]",
           INT_TYPE),
    OPTION("-q", "--quiet", "never output headers giving file names"),
    OPTION("", "--silent", "never output headers giving file names"),
    OPTION("", "--retry",
           "keep trying to open a file if it is inaccessible [NOT SUPPORT]"),
    OPTION("-s", "--sleep-interval",
           "with -f, sleep for approximately N seconds between iterations\n"
           "[NOT SUPPORT]",
           STRING_TYPE),
    OPTION("-v", "--verbose", "always output headers giving file names"),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

namespace tail_pipeline {
namespace cp = core::pipeline;

struct CountSpec {
  std::uintmax_t value = 10;
  bool from_start = false;
};

struct TailConfig {
  bool by_bytes = false;
  CountSpec spec;
  bool quiet = false;
  bool verbose = false;
  bool follow = false;
  bool stdin_mode = false;
  char delimiter = '\n';
};

auto stream_all(std::istream& in) -> void {
  std::array<char, 8192> buffer{};
  while (in.good()) {
    in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    auto got = in.gcount();
    if (got <= 0) break;
    safePrint(std::string_view(buffer.data(), static_cast<size_t>(got)));
  }
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
              std::pair{std::string_view{"TB"}, 1000ULL * 1000ULL * 1000ULL * 1000ULL},
              std::pair{std::string_view{"T"}, 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"TiB"}, 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"PB"}, 1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL},
              std::pair{std::string_view{"P"}, 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"PiB"}, 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"EB"}, 1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL},
              std::pair{std::string_view{"E"}, 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"EiB"}, 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"Z"}, 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"Y"}, 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
              std::pair{std::string_view{"R"}, 0ULL},
              std::pair{std::string_view{"Q"}, 0ULL},
              std::pair{std::string_view{"ZB"}, 0ULL},
              std::pair{std::string_view{"YB"}, 0ULL}
          });
  
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

  std::uintmax_t base = 0;
  auto [ptr, ec] = std::from_chars(text.data(), text.data() + i, base);
  if (ec != std::errc() || ptr != text.data() + i) return std::nullopt;

  auto mult = suffix_multiplier(text.substr(i));
  if (!mult.has_value()) return std::nullopt;

  if (*mult != 0 &&
      base > (std::numeric_limits<std::uintmax_t>::max() / *mult)) {
    return std::nullopt;
  }

  return base * *mult;
}

auto parse_count_spec(std::string spec_text, std::string_view opt_name)
    -> cp::Result<CountSpec> {
  if (spec_text.empty()) {
    return std::unexpected("invalid number of " + std::string(opt_name));
  }

  CountSpec spec;
  if (spec_text[0] == '+') {
    spec.from_start = true;
    spec_text = spec_text.substr(1);  // Avoid modifying original string
  }

  auto parsed = parse_numeric_with_suffix(spec_text);
  if (!parsed.has_value()) {
    return std::unexpected("invalid number of " + std::string(opt_name));
  }

  spec.value = *parsed;
  return spec;
}

auto output_tail(std::istream& in, const TailConfig& config) -> void {
  if (config.by_bytes) {
    size_t n = static_cast<size_t>(config.spec.value);
    if (config.spec.from_start) {
      size_t skip = n > 0 ? n - 1 : 0;
      std::array<char, 8192> discard{};
      while (skip > 0 && in.good()) {
        size_t chunk = std::min(skip, discard.size());
        in.read(discard.data(), static_cast<std::streamsize>(chunk));
        auto got = in.gcount();
        if (got <= 0) return;
        skip -= static_cast<size_t>(got);
      }
      stream_all(in);
      return;
    }

    if (n == 0) return;
    std::deque<char> trailing;
    std::array<char, 8192> buffer{};
    while (in.good()) {
      in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
      auto got = in.gcount();
      if (got <= 0) break;
      for (std::streamsize i = 0; i < got; ++i) {
        trailing.push_back(buffer[static_cast<size_t>(i)]);
        if (trailing.size() > n) trailing.pop_front();
      }
    }
    if (!trailing.empty()) {
      std::string out;
      out.reserve(trailing.size());
      for (char ch : trailing) out.push_back(ch);
      safePrint(out);
    }
    return;
  }

  size_t n = static_cast<size_t>(config.spec.value);
  if (config.spec.from_start) {
    size_t start = n > 0 ? n - 1 : 0;
    if (start == 0) {
      stream_all(in);
      return;
    }

    size_t record_index = 0;
    std::string current;
    char ch = '\0';
    while (in.get(ch)) {
      current.push_back(ch);
      if (ch == config.delimiter) {
        if (record_index >= start) safePrint(current);
        current.clear();
        ++record_index;
      }
    }
    if (!current.empty() && record_index >= start) {
      safePrint(current);
    }
    return;
  }

  if (n == 0) return;
  std::deque<std::string> trailing_records;
  std::string current;
  char ch = '\0';
  while (in.get(ch)) {
    current.push_back(ch);
    if (ch == config.delimiter) {
      trailing_records.push_back(std::move(current));
      current.clear();
      if (trailing_records.size() > n) trailing_records.pop_front();
    }
  }
  if (!current.empty()) {
    trailing_records.push_back(std::move(current));
    if (trailing_records.size() > n) trailing_records.pop_front();
  }
  for (const auto& rec : trailing_records) safePrint(rec);
}

template <size_t N>
auto check_unsupported(const CommandContext<N>& ctx) -> cp::Result<void> {
  if (ctx.get<bool>("-F", false)) {
    return std::unexpected("-F is [NOT SUPPORT] on this platform");
  }
  if (ctx.get<int>("--max-unchanged-stats", -1) >= 0) {
    return std::unexpected("--max-unchanged-stats is [NOT SUPPORT]");
  }
  if (ctx.get<int>("--pid", -1) >= 0) {
    return std::unexpected("--pid is [NOT SUPPORT]");
  }
  if (ctx.get<bool>("--retry", false)) {
    return std::unexpected("--retry is [NOT SUPPORT]");
  }
  if (!ctx.get<std::string>("--sleep-interval", "").empty() ||
      !ctx.get<std::string>("-s", "").empty()) {
    return std::unexpected("--sleep-interval is [NOT SUPPORT]");
  }
  return {};
}

template <size_t N>
auto build_config(const CommandContext<N>& ctx) -> cp::Result<TailConfig> {
  TailConfig config;
  config.quiet =
      ctx.get<bool>("--quiet", false) || ctx.get<bool>("--silent", false);
  config.verbose = ctx.get<bool>("--verbose", false);
  config.delimiter = ctx.get<bool>("--zero-terminated", false) ? '\0' : '\n';
  config.follow = ctx.get<bool>("-f", false) || ctx.get<bool>("--follow", false);

  auto unsupported = check_unsupported(ctx);
  if (!unsupported) return std::unexpected(unsupported.error());

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

}  // namespace tail_pipeline

REGISTER_COMMAND(
    tail, "tail", "tail [OPTION]... [FILE]...",
    "Print the last 10 lines of each FILE to standard output.\n"
    "With more than one FILE, precede each with a header giving the file "
    "name.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.",
    "  tail file.txt\n"
    "  tail -n 20 file.txt\n"
    "  tail -20 file.txt\n"
    "  tail -n +5 file.txt\n"
    "  tail +5 file.txt\n"
    "  tail -c 64 file.txt\n"
    "  tail -v a.txt b.txt",
    "head(1), cat(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TAIL_OPTIONS) {
  using namespace tail_pipeline;

  auto config_result = build_config(ctx);
  if (!config_result) {
    cp::report_error(config_result, L"tail");
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
      config.stdin_mode = true;
      output_tail(std::cin, config);
      if (std::cin.bad()) {
        safeErrorPrint("tail: error reading '-'\n");
        any_error = true;
      }
    } else {
      std::ifstream input(file, std::ios::binary);
      if (!input.is_open()) {
        safeErrorPrint("tail: cannot open '");
        safeErrorPrint(file);
        safeErrorPrint("'\n");
        any_error = true;
        continue;
      }

      output_tail(input, config);
      if (input.bad()) {
        safeErrorPrint("tail: error reading '");
        safeErrorPrint(file);
        safeErrorPrint("'\n");
        any_error = true;
      }
      input.close();

      if (config.follow && !config.stdin_mode) {
        std::ifstream monitor_file(file, std::ios::binary);
        if (!monitor_file.is_open()) {
          safeErrorPrint("tail: cannot open '");
          safeErrorPrint(file);
          safeErrorPrint("' for following\n");
          any_error = true;
        } else {
          monitor_file.seekg(0, std::ios::end);
          while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            auto current_pos = monitor_file.tellg();
            monitor_file.seekg(0, std::ios::end);
            auto end_pos = monitor_file.tellg();
            if (end_pos > current_pos) {
              monitor_file.seekg(current_pos);
              std::string line;
              while (std::getline(monitor_file, line)) {
                safePrint(line);
                safePrint("\n");
              }
            }
            if (GetAsyncKeyState(VK_CONTROL) & 0x8000) {
              if (GetAsyncKeyState('C') & 0x8000) {
                break;
              }
            }
          }
          monitor_file.close();
        }
      }
    }

    first_print = false;
  }

  return any_error ? 1 : 0;
}
