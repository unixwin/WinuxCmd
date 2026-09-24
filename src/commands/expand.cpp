// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for expand.
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

auto constexpr EXPAND_OPTIONS =
    std::array{// [GNU]
               OPTION("-t", "--tabs", "specify tab stop positions (default: 8)",
                      STRING_TYPE),
               // [GNU]
               OPTION("-i", "--initial",
                      "only convert tabs at the beginning of lines", BOOL_TYPE),
               // [GNU] obsolescent -NUM is an alias for --tabs=NUM
               OPTION("-NUM", "", "same as --tabs=NUM", INT_TYPE)};

namespace expand_pipeline {
namespace cp = core::pipeline;

struct Config {
  struct TabStops {
    enum class RepeatMode { every_multiple, after_last, none };

    SmallVector<size_t, 16> stops;
    size_t interval = 8;
    RepeatMode repeat_mode = RepeatMode::every_multiple;
  };

  TabStops tab_stops;
  bool initial_only = false;
  SmallVector<std::string, 64> files;
};

auto parse_tab_stops(const std::string& spec) -> cp::Result<Config::TabStops> {
  Config::TabStops tab_stops;
  if (spec.empty()) {
    return tab_stops;
  }

  std::string normalized = spec;
  for (char& c : normalized) {
    if (c == ',') c = ' ';
  }

  std::istringstream input(normalized);
  std::vector<std::string> tokens;
  for (std::string token; input >> token;) {
    tokens.push_back(token);
  }

  // Mirrors GNU expand-common.c: parse errors quote the offending part of
  // the token; zero/ascending validation runs after the whole list parses.
  auto invalid_chars_error = [](const std::string_view quoted) {
    return "tab size contains invalid character(s): '" + std::string(quoted) +
           "'";
  };

  bool repeat_specified = false;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const std::string& token = tokens[i];
    const bool repeat = token[0] == '/' || token[0] == '+';
    const std::string_view value =
        repeat ? std::string_view(token).substr(1) : std::string_view(token);

    size_t digits_end = 0;
    while (digits_end < value.size() &&
           std::isdigit(static_cast<unsigned char>(value[digits_end]))) {
      ++digits_end;
    }
    if (digits_end < value.size()) {
      return std::unexpected(invalid_chars_error(value.substr(digits_end)));
    }

    size_t parsed = 0;
    const auto [ptr, ec] =
        std::from_chars(value.data(), value.data() + digits_end, parsed);
    if (ec == std::errc::result_out_of_range) {
      return std::unexpected("tab stop is too large '" +
                             std::string(value.substr(0, digits_end)) + "'");
    }
    if (ec != std::errc() || ptr != value.data() + digits_end) {
      return std::unexpected(invalid_chars_error(value.substr(digits_end)));
    }

    if (repeat) {
      if (i + 1 != tokens.size()) {
        return std::unexpected("repeat tab stop must be last");
      }
      if (parsed == 0) {
        return std::unexpected("tab size cannot be 0");
      }
      tab_stops.interval = parsed;
      tab_stops.repeat_mode = token[0] == '/'
                                  ? Config::TabStops::RepeatMode::every_multiple
                                  : Config::TabStops::RepeatMode::after_last;
      repeat_specified = true;
      continue;
    }

    tab_stops.stops.push_back(parsed);
  }

  // GNU validates nonzero and ascending order after parsing everything.
  size_t prev_stop = 0;
  for (const size_t stop : tab_stops.stops) {
    if (stop == 0) {
      return std::unexpected("tab size cannot be 0");
    }
    if (stop <= prev_stop) {
      return std::unexpected("tab sizes must be ascending");
    }
    prev_stop = stop;
  }

  if (tab_stops.stops.size() == 1 && !repeat_specified &&
      tab_stops.repeat_mode == Config::TabStops::RepeatMode::every_multiple) {
    tab_stops.interval = tab_stops.stops.front();
    tab_stops.stops.clear();
  } else if (tab_stops.stops.size() > 1 &&
             tab_stops.repeat_mode ==
                 Config::TabStops::RepeatMode::every_multiple) {
    tab_stops.repeat_mode = Config::TabStops::RepeatMode::none;
  }

  return tab_stops;
}

auto next_tab_stop(size_t column, const Config::TabStops& tab_stops) -> size_t {
  const size_t max = std::numeric_limits<size_t>::max();
  for (size_t stop : tab_stops.stops) {
    if (stop > column) return stop;
  }

  // Smallest multiple of INTERVAL strictly greater than COL. The result is
  // saturated at SIZE_MAX instead of wrapping: an overflowing stop behind
  // the current column would corrupt the padding count (next_stop - column
  // would underflow into a bogus huge value for the wrong reason).
  auto next_multiple = [max](size_t col, size_t interval) -> size_t {
    const size_t delta = interval - col % interval;  // in [1, interval]
    return col > max - delta ? max : col + delta;
  };

  switch (tab_stops.repeat_mode) {
    case Config::TabStops::RepeatMode::every_multiple:
      return next_multiple(column, tab_stops.interval);
    case Config::TabStops::RepeatMode::after_last: {
      size_t anchor = tab_stops.stops.empty() ? 0 : tab_stops.stops.back();
      if (column < anchor) return anchor;
      const size_t rel = next_multiple(column - anchor, tab_stops.interval);
      return rel > max - anchor ? max : anchor + rel;
    }
    case Config::TabStops::RepeatMode::none:
      return column == max ? max : column + 1;
  }

  return column == max ? max : column + 1;
}

auto build_config(const CommandContext<EXPAND_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.initial_only =
      ctx.get<bool>("--initial", false) || ctx.get<bool>("-i", false);

  auto tabs_opt = ctx.get<std::string>("--tabs", "");
  if (tabs_opt.empty()) {
    tabs_opt = ctx.get<std::string>("-t", "");
  }

  // [GNU] obsolescent -NUM operands add to the tab-stop list.
  for (const int stop : ctx.get_all<int>("-NUM")) {
    if (!tabs_opt.empty()) tabs_opt += ' ';
    tabs_opt += std::to_string(stop);
  }

  if (!tabs_opt.empty()) {
    auto tab_stops = parse_tab_stops(tabs_opt);
    if (!tab_stops) return std::unexpected(tab_stops.error());
    cfg.tab_stops = *tab_stops;
  }

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

  if (cfg.files.empty()) {
    cfg.files.push_back("-");
  }

  return cfg;
}

// Bounded stdout sink. Tab expansion can legitimately emit far more bytes
// than it reads (`expand -t 999999999999` must really write ~1e12 spaces,
// like GNU), so output is streamed through a fixed-size buffer instead of
// being accumulated in a std::string — materializing the whole padding run
// previously attempted a single ~1TB allocation that failed and silently
// dropped all output.
class ChunkedOutput {
 public:
  void put(char c) {
    if (used_ == kChunkSize) flush();
    buffer_[used_++] = c;
  }

  void write_spaces(size_t count) {
    if (used_ != 0) {
      const size_t n = std::min(count, kChunkSize - used_);
      std::memcpy(buffer_.data() + used_, kSpaces.data(), n);
      used_ += n;
      count -= n;
      if (used_ == kChunkSize) flush();
    }
    // Full chunks are written straight from the static space block so a
    // huge padding run costs one bounded write per chunk, no big string.
    while (count >= kChunkSize) {
      safePrint(std::string_view(kSpaces.data(), kChunkSize));
      count -= kChunkSize;
    }
    if (count != 0) {
      std::memcpy(buffer_.data(), kSpaces.data(), count);
      used_ = count;
    }
  }

  void flush() {
    if (used_ == 0) return;
    safePrint(std::string_view(buffer_.data(), used_));
    used_ = 0;
  }

 private:
  static constexpr size_t kChunkSize = 64 * 1024;
  static constexpr auto kSpaces = [] {
    std::array<char, kChunkSize> filled{};
    filled.fill(' ');
    return filled;
  }();
  std::array<char, kChunkSize> buffer_{};
  size_t used_ = 0;
};

// Expand tabs to spaces in a single line, streaming the result into OUT.
auto expand_line(const std::string& line, const Config::TabStops& tab_stops,
                 bool initial_only, ChunkedOutput& out) -> void {
  size_t column = 0;
  bool before_non_blank = true;
  const size_t max = std::numeric_limits<size_t>::max();

  for (char c : line) {
    if (c == '\t') {
      size_t next_stop = next_tab_stop(column, tab_stops);
      if (!initial_only || before_non_blank) {
        // next_tab_stop never wraps, so next_stop >= column here.
        out.write_spaces(next_stop - column);
      } else {
        out.put(c);
      }
      column = next_stop;
    } else if (c == '\n') {
      out.put(c);
      column = 0;
      before_non_blank = true;
    } else if (c == '\b') {
      out.put(c);
      if (column > 0) --column;
      before_non_blank = false;
    } else {
      out.put(c);
      if (column != max) ++column;
      if (c != ' ') before_non_blank = false;
    }
  }
}

auto run(const Config& cfg) -> int {
  auto expand_input_open_error = [](std::string_view path) -> std::string {
    std::error_code ec;
    auto status = std::filesystem::status(std::filesystem::u8path(path), ec);
    if (!ec && status.type() == std::filesystem::file_type::directory) {
      return std::string("cannot open '") + std::string(path) +
             "' for reading: Is a directory";
    }
    return std::string("cannot open '") + std::string(path) +
           "' for reading: No such file or directory";
  };

  bool all_ok = true;

  for (const auto& file : cfg.files) {
    std::string content;

    if (file == "-") {
      // [GNU] closed stdin (<&-) errors "expand: -: Bad file descriptor".
      if (file_io::stdin_is_bad()) {
        cp::Result<int> result = std::unexpected("-: Bad file descriptor");
        cp::report_error(result, L"expand");
        return 1;
      }
      // Read from stdin
      content.assign(std::istreambuf_iterator<char>(std::cin),
                     std::istreambuf_iterator<char>());
    } else {
      // Read from file
      std::ifstream f(file, std::ios::binary);
      if (!f) {
        auto err = expand_input_open_error(file);
        cp::Result<int> result = std::unexpected(err);
        cp::report_error(result, L"expand");
        all_ok = false;
        continue;
      }
      content.assign(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
      if (f.fail() && !f.eof()) {
        cp::Result<int> result = std::unexpected("error reading from file");
        cp::report_error(result, L"expand");
        all_ok = false;
        continue;
      }
      // Skip UTF-8 BOM if present at the beginning
      if (content.size() >= 3 &&
          static_cast<unsigned char>(content[0]) == 0xEF &&
          static_cast<unsigned char>(content[1]) == 0xBB &&
          static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
      }
    }

    // Process line by line to maintain line breaks
    ChunkedOutput out;
    size_t line_start = 0;
    while (line_start < content.size()) {
      size_t line_end = content.find('\n', line_start);
      std::string line;

      if (line_end == std::string::npos) {
        line = content.substr(line_start);
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        expand_line(line, cfg.tab_stops, cfg.initial_only, out);
        break;
      } else {
        line = content.substr(line_start,
                              line_end - line_start + 1);  // Include newline
        if (line.size() >= 2 && line[line.size() - 2] == '\r' &&
            line.back() == '\n') {
          line.erase(line.end() - 2);
        }
        expand_line(line, cfg.tab_stops, cfg.initial_only, out);
        line_start = line_end + 1;
      }
    }

    out.flush();
  }

  return all_ok ? 0 : 1;
}

}  // namespace expand_pipeline

REGISTER_COMMAND(
    expand, "expand", "expand [OPTION]... [FILE]...",
    "Convert tabs to spaces.\n"
    "\n"
    "Convert each tab character to one or more spaces.\n"
    "By default, tabs are converted to 8 spaces.\n"
    "\n"
    "Supports GNU tab lists, including /N and +N repeat tab stops.",
    "  expand file.txt\n"
    "  expand -t 4 file.txt\n"
    "  expand -i file.txt\n"
    "  echo -e 'hello\tworld' | expand",
    "unexpand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", EXPAND_OPTIONS) {
  using namespace expand_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"expand");
    return 1;
  }

  return run(*cfg_result);
}
