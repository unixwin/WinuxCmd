// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for unexpand.
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

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr UNEXPAND_OPTIONS = std::array{
    // [GNU] option
    OPTION("-t", "--tabs", "specify tab stop positions (default: 8)",
           STRING_TYPE),
    // [GNU] option
    OPTION("-a", "--all", "convert all spaces, not just leading ones",
           BOOL_TYPE),
    // [EXT] GNU --first-only has no short option
    OPTION("-f", "--first-only", "convert only leading sequences of blanks",
           BOOL_TYPE),
    // [EXT] option
    OPTION("-U", "--no-utf8",
           "interpret input file as 8-bit ASCII rather than UTF-8", BOOL_TYPE),
    // [GNU] obsolescent -NUM is an alias for --tabs=NUM
    OPTION("-NUM", "", "same as --tabs=NUM", INT_TYPE)};

namespace unexpand_pipeline {
namespace cp = core::pipeline;

struct Config {
  struct TabStops {
    enum class RepeatMode { every_multiple, after_last, none };

    SmallVector<size_t, 16> stops;
    size_t interval = 8;
    RepeatMode repeat_mode = RepeatMode::every_multiple;
  };

  TabStops tab_stops;
  bool all_spaces = false;
  bool first_only = true;
  bool no_utf8 = false;
  SmallVector<std::string, 64> files;
};

// [GNU] expand-common.c add_tab_stop(): appends a single stop, validating
// nonzero size and strict ascending order against every previously added
// stop.
auto add_tab_stop(std::uintmax_t stop, Config::TabStops& tab_stops)
    -> cp::Result<void> {
  if (stop == 0) {
    return std::unexpected("tab size cannot be 0");
  }
  if (stop > std::numeric_limits<size_t>::max()) {
    return std::unexpected("tab stop is too large '" + std::to_string(stop) +
                           "'");
  }
  if (!tab_stops.stops.empty() && stop <= tab_stops.stops.back()) {
    return std::unexpected("tab sizes must be ascending");
  }
  tab_stops.stops.push_back(static_cast<size_t>(stop));
  return {};
}

// [GNU] expand-common.c parse_tab_stops(): parses one -t/--tabs SPEC and
// appends its stops to TAB_STOPS. REPEAT_SPECIFIED is set when the spec
// uses the '/' (every multiple) or '+' (relative to last stop) repeat
// form, which must be the last item of the spec.
auto parse_tab_stops(std::string_view spec, Config::TabStops& tab_stops,
                     bool& repeat_specified) -> cp::Result<void> {
  if (spec.empty()) {
    return {};
  }

  std::string normalized(spec);
  for (char& c : normalized) {
    if (c == ',') c = ' ';
  }

  std::istringstream input(normalized);
  std::vector<std::string> tokens;
  for (std::string token; input >> token;) {
    tokens.push_back(token);
  }

  // Mirrors GNU expand-common.c: parse errors quote the offending part of
  // the token; zero/ascending validation runs as each stop is added.
  auto invalid_chars_error = [](const std::string_view quoted) {
    return "tab size contains invalid character(s): '" + std::string(quoted) +
           "'";
  };

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

    std::uintmax_t parsed = 0;
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
      tab_stops.interval = static_cast<size_t>(parsed);
      tab_stops.repeat_mode = token[0] == '/'
                                  ? Config::TabStops::RepeatMode::every_multiple
                                  : Config::TabStops::RepeatMode::after_last;
      repeat_specified = true;
      continue;
    }

    if (auto added = add_tab_stop(parsed, tab_stops); !added) {
      return added;
    }
  }

  return {};
}

// [GNU] expand-common.c get_next_tab_column(): the next tab stop strictly
// greater than COLUMN. With a finite stop list (no /N or +N repeat) there is
// no stop beyond the last one; LAST_TAB is set and GNU switches conversion
// off for the rest of the line.
auto get_next_tab_column(size_t column, const Config::TabStops& tab_stops,
                         bool& last_tab) -> size_t {
  const size_t max = std::numeric_limits<size_t>::max();
  for (size_t stop : tab_stops.stops) {
    if (stop > column) return stop;
  }

  // Smallest multiple of INTERVAL strictly greater than COL, saturated at
  // SIZE_MAX rather than wrapping so a huge in-range interval never yields
  // a tab column behind the current position.
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
      break;
  }

  last_tab = true;
  return column;
}

auto build_config(const CommandContext<UNEXPAND_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.all_spaces = ctx.get<bool>("--all", false) || ctx.get<bool>("-a", false);
  cfg.first_only =
      ctx.get<bool>("--first-only", false) || ctx.get<bool>("-f", false);
  cfg.no_utf8 = ctx.get<bool>("--no-utf8", false) || ctx.get<bool>("-U", false);

  // [GNU] unexpand.c parses options with getopt optstring
  // ",0123456789at:": consecutive digit options accumulate one number
  // (DECIMAL_DIGIT_ACCUMULATE), so "-4 -8" selects a single tab stop of 48,
  // not {4,8}; a ',' option flushes the pending value but cannot be
  // expressed through the generic -NUM option, and the pending value is
  // flushed once all options are parsed. A -t/--tabs spec adds its stops
  // before a pending digit value is flushed, so "-4 -t 8" and "-t 8 -4"
  // both fail with "tab sizes must be ascending". -t/--tabs also selects
  // whole-line conversion like -a; the digit form alone does not.
  Config::TabStops tab_stops;
  bool repeat_specified = false;
  bool tab_spec_seen = false;
  bool have_tabval = false;
  std::uintmax_t tabval = 0;
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= UNEXPAND_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (option_matches(meta, "-t", "--tabs")) {
      tab_spec_seen = true;
      // [GNU] case 't': convert_entire_line = true.
      cfg.all_spaces = true;
      if (const auto* value = std::get_if<std::string>(&occurrence.value)) {
        if (auto parsed = parse_tab_stops(*value, tab_stops, repeat_specified);
            !parsed) {
          return std::unexpected(parsed.error());
        }
      }
    } else if (meta.short_name == "-NUM") {
      if (const auto* value = std::get_if<int>(&occurrence.value)) {
        if (!have_tabval) {
          tabval = 0;
          have_tabval = true;
        }
        // The -NUM option yields one decimal string per occurrence; appending
        // its digits is equivalent to GNU's per-digit accumulation.
        for (const char ch : std::to_string(*value)) {
          const auto digit = static_cast<unsigned>(ch - '0');
          if (tabval >
              (std::numeric_limits<std::uintmax_t>::max() - digit) / 10) {
            return std::unexpected("tab stop value is too large");
          }
          tabval = tabval * 10 + digit;
        }
      }
    }
  }
  if (have_tabval) {
    if (auto added = add_tab_stop(tabval, tab_stops); !added) {
      return std::unexpected(added.error());
    }
  }
  if (tab_spec_seen || have_tabval) {
    // [GNU] finalize_tab_stops(): a single stop without an explicit repeat
    // becomes the repeating interval; a repeat mark applies to otherwise
    // finite lists only through its own mode.
    if (tab_stops.stops.size() == 1 && !repeat_specified &&
        tab_stops.repeat_mode == Config::TabStops::RepeatMode::every_multiple) {
      tab_stops.interval = tab_stops.stops.front();
      tab_stops.stops.clear();
    } else if (tab_stops.stops.size() > 1 &&
               tab_stops.repeat_mode ==
                   Config::TabStops::RepeatMode::every_multiple) {
      tab_stops.repeat_mode = Config::TabStops::RepeatMode::none;
    }
    cfg.tab_stops = tab_stops;
  }
  // [GNU] --first-only cancels whole-line conversion.
  if (cfg.first_only) {
    cfg.all_spaces = false;
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

// Convert spaces and tabs to the shortest equivalent tab/space sequence.
// This is a direct port of the GNU unexpand.c conversion loop: blanks are
// accumulated in PENDING_BLANK until a non-blank arrives or a blank lands
// exactly on a tab stop (which collapses the pending run into one or two
// tabs). A pending run whose first blank ended exactly on a tab stop gets
// its first character replaced by '\t' when it is longer than one blank —
// that is the case where a mid-line run crosses a tab stop by a single
// column and GNU still emits a tab.
auto unexpand_line(const std::string& line, const Config::TabStops& tab_stops,
                   bool all_spaces) -> std::string {
  std::string result;
  result.reserve(line.size());

  // If true, perform translations.
  bool convert = true;
  // Column of the next input character.
  size_t column = 0;
  // If true, the first pending blank came just before a tab stop.
  bool one_blank_before_tab_stop = false;
  // If true, the previous input character was a blank. Initially true, since
  // initial strings of blanks are treated as if the line was preceded by a
  // blank.
  bool prev_blank = true;
  // Pending blank characters.
  std::string pending_blank;

  for (size_t pos = 0; pos < line.size(); ++pos) {
    char c = line[pos];
    if (convert) {
      const bool blank = c == ' ' || c == '\t';
      if (blank) {
        bool last_tab = false;
        const size_t next_tab_column =
            get_next_tab_column(column, tab_stops, last_tab);
        if (last_tab) convert = false;
        if (convert) {
          if (c == '\t') {
            column = next_tab_column;
            if (!pending_blank.empty()) pending_blank[0] = '\t';
          } else {
            // Saturate: a column at SIZE_MAX must not wrap to 0.
            if (column != std::numeric_limits<size_t>::max()) ++column;
            if (!(prev_blank && column == next_tab_column)) {
              // It is not yet known whether the pending blanks will be
              // replaced by tabs.
              if (column == next_tab_column) one_blank_before_tab_stop = true;
              pending_blank += c;
              prev_blank = true;
              continue;
            }
            // Replace the pending blanks by a tab or two. GNU writes
            // pending_blank[0] unconditionally; when nothing is pending the
            // single character that just reached a tab stop is converted.
            if (pending_blank.empty()) pending_blank += ' ';
            pending_blank[0] = c = '\t';
          }
          // Discard pending blanks, unless it was a single blank just before
          // the previous tab stop.
          pending_blank.resize(one_blank_before_tab_stop ? 1 : 0);
        }
      } else if (c == '\b') {
        // Go back one column; the next tab stop is recomputed anyway.
        if (column > 0) --column;
      } else {
        if (column != std::numeric_limits<size_t>::max()) ++column;
      }

      if (!pending_blank.empty()) {
        if (pending_blank.size() > 1 && one_blank_before_tab_stop) {
          pending_blank[0] = '\t';
        }
        result += pending_blank;
        pending_blank.clear();
        one_blank_before_tab_stop = false;
      }
      prev_blank = blank;
      convert = convert && (all_spaces || blank);
    }
    result += c;
  }

  return result;
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;
  auto input_open_error = [](std::string_view path) -> std::string {
    std::error_code ec;
    if (std::filesystem::is_directory(std::filesystem::u8path(path), ec) &&
        !ec) {
      return "cannot open '" + std::string(path) +
             "' for reading: Is a directory";
    }

    return "cannot open '" + std::string(path) +
           "' for reading: No such file or directory";
  };

  for (const auto& file : cfg.files) {
    std::string content;

    if (file == "-") {
      // Read from stdin
      content.assign(std::istreambuf_iterator<char>(std::cin),
                     std::istreambuf_iterator<char>());
    } else {
      // Read from file
      std::ifstream f(file, std::ios::binary);
      if (!f) {
        auto err = input_open_error(file);
        cp::Result<int> result = std::unexpected(err);
        cp::report_error(result, L"unexpand");
        all_ok = false;
        continue;
      }
      content.assign(std::istreambuf_iterator<char>(f),
                     std::istreambuf_iterator<char>());
      if (f.fail() && !f.eof()) {
        cp::Result<int> result = std::unexpected("error reading from file");
        cp::report_error(result, L"unexpand");
        all_ok = false;
        continue;
      }
      // Default mode treats UTF-8 input like GNU/coreutils and drops a BOM.
      if (!cfg.no_utf8 && content.size() >= 3 &&
          static_cast<unsigned char>(content[0]) == 0xEF &&
          static_cast<unsigned char>(content[1]) == 0xBB &&
          static_cast<unsigned char>(content[2]) == 0xBF) {
        content = content.substr(3);
      }
    }

    // Process line by line to maintain line breaks
    std::string result;
    size_t line_start = 0;
    while (line_start < content.size()) {
      size_t line_end = content.find('\n', line_start);
      std::string line;

      if (line_end == std::string::npos) {
        line = content.substr(line_start);
        result += unexpand_line(line, cfg.tab_stops, cfg.all_spaces);
        break;
      } else {
        line = content.substr(line_start,
                              line_end - line_start + 1);  // Include newline
        result += unexpand_line(line, cfg.tab_stops, cfg.all_spaces);
        line_start = line_end + 1;
      }
    }

    safePrint(result);
  }

  return all_ok ? 0 : 1;
}

}  // namespace unexpand_pipeline

REGISTER_COMMAND(
    unexpand, "unexpand", "unexpand [OPTION]... [FILE]...",
    "Convert spaces to tabs.\n"
    "\n"
    "Convert spaces to tabs. By default, only convert leading spaces to tabs.\n"
    "Use -a to convert all spaces.\n"
    "\n"
    "Supports GNU tab lists, including /N and +N repeat tab stops.",
    "  unexpand file.txt\n"
    "  unexpand -t 4 file.txt\n"
    "  unexpand -a file.txt\n"
    "  echo 'hello world' | unexpand",
    "expand(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", UNEXPAND_OPTIONS) {
  using namespace unexpand_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"unexpand");
    return 1;
  }

  return run(*cfg_result);
}
