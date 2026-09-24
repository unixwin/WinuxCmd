// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for pr.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <cstdlib>  // std::getenv
#include <ctime>    // localtime_s (TZ env support)

#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PR_OPTIONS = std::array{
    // [GNU]
    OPTION("+PAGE", "", "begin printing with page PAGE [default 1]",
           STRING_TYPE),
    // [GNU]
    OPTION("-COLUMN", "", "produce COLUMN-column output", STRING_TYPE),
    // [GNU]
    OPTION("-a", "", "produce multi-column output", BOOL_TYPE),
    // [GNU]
    OPTION("-d", "", "double-space the output", BOOL_TYPE),
    // [GNU] -e takes an optional *attached* [CHAR[WIDTH]] argument only.
    OPTION("-e", "--expand", "expand input TABs", OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-f", "--form-feed", "use form feeds instead of newlines",
           BOOL_TYPE),
    // [GNU]
    OPTION("-h", "--header", "use a centered HEADER", STRING_TYPE),
    // [GNU]
    OPTION("-l", "--length", "set page length", STRING_TYPE),
    // [GNU] -n takes an optional *attached* [SEP[DIGITS]] argument only.
    OPTION("-n", "--number-lines", "number lines", OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-o", "--indent", "offset each line", STRING_TYPE),
    // [GNU]
    OPTION("-r", "--no-file-warnings",
           "omit warning when a file cannot be opened", BOOL_TYPE),
    // [GNU] -s takes an optional *attached* [CHAR] argument only.
    OPTION("-s", "--separator", "separate columns by characters",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-t", "--omit-header", "omit page headers and trailers", BOOL_TYPE),
    // [GNU]
    OPTION("-T", "--omit-pagination", "omit page headers and trailers",
           BOOL_TYPE),
    // [GNU]
    OPTION("-w", "--width", "set page width", STRING_TYPE),
    // [GNU]
    OPTION("-W", "--page-width", "set page width (default 72)", STRING_TYPE),
    // [GNU]
    OPTION("-b", "--balance-columns", "balance columns on the last page",
           BOOL_TYPE),
    // [GNU]
    OPTION("-c", "--show-control-chars",
           "use hat notation (^G) and octal backslash notation", BOOL_TYPE),
    // [GNU]
    OPTION("-D", "--date-format", "use FORMAT for the date in the header",
           STRING_TYPE),
    // [GNU]
    OPTION("-F", "-f", "use form feeds instead of newlines (same as -f)",
           BOOL_TYPE),
    // [GNU] -i takes an optional *attached* [CHAR[WIDTH]] argument only.
    OPTION("-i", "--output-tabs", "replace spaces with TABs where possible",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-J", "--join-lines", "merge full lines (ignore --column warnings)",
           BOOL_TYPE),
    // [GNU]
    OPTION("-m", "--merge", "print all files in parallel, one in each column",
           BOOL_TYPE),
    // [GNU]
    OPTION("-N", "--first-line-number",
           "start counting with NUMBER at line 1 of first page", STRING_TYPE),
    // [GNU] -S takes an optional *attached* [STRING] argument only; a
    // separate word is a file operand (pr -S , f fails to open ',').
    OPTION("-S", "--sep-string",
           "separate columns by STRING (default single space)",
           OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-v", "--show-nonprinting",
           "use octal backslash notation for non-printing characters",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--across", "print across pages", BOOL_TYPE),
    // [GNU]
    OPTION("", "--columns", "output COLUMN-column output", STRING_TYPE),
    // [GNU]
    OPTION("", "--double-space", "double-space the output", BOOL_TYPE),
    // [GNU] --expand-tabs takes an optional *attached* [=CHAR[WIDTH]] value.
    OPTION("", "--expand-tabs", "expand input TABs", OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("", "--pages", "begin printing with page PAGE", STRING_TYPE)};

namespace pr_pipeline {
namespace cp = core::pipeline;

struct Config {
  int start_page = 1;
  int columns = 1;
  bool double_space = false;
  // [GNU] -e/--expand-tabs: presence flag + validated width (default 8).
  bool expand_set = false;
  int expand_width = 8;
  bool form_feed = false;
  std::string header;
  int page_length = 66;
  // [GNU] -n/--number-lines: presence flag (numbering enabled) plus the
  // [SEP[DIGITS]] parameters — the separator character defaults to TAB and
  // the field width to 5 digits.
  bool number_lines_set = false;
  char number_sep = '\t';
  int number_digits = 5;
  int indent = 0;
  bool no_file_warnings = false;
  std::string separator = "\t";
  // [GNU] -s/--separator is an "old" option (pr.c old_s): bare it selects
  // field mode for column output, and without -w/-W it annuls column
  // alignment entirely (join_lines).
  bool old_s = false;
  bool old_s_arg = false;
  // [GNU] -a/--across only changes the column fill order; it never selects
  // a column count by itself.
  bool across = false;
  // [GNU] -w is an "old" option (old_w): with columns/-m it activates -W
  // (truncate+align), without them it selects join mode.
  bool width_set = false;
  bool page_width_set = false;  // -W/--page-width
  bool sep_string_set = false;  // -S/--sep-string
  bool omit_header = false;
  bool omit_pagination = false;
  int page_width = 72;
  bool balance_columns = false;
  bool show_control_chars = false;
  std::string date_format;
  bool form_feed_ff = false;
  // [GNU] -i/--output-tabs: presence flag + validated width (default 8).
  bool output_tabs_set = false;
  int output_tabs_width = 8;
  bool join_lines = false;
  bool merge = false;
  std::string first_line_number;
  std::string sep_string;
  bool show_nonprinting = false;
  SmallVector<std::string, 64> files;
};

// [GNU] pr.c validates numeric option values via xstrtoui and reports:
//   pr: '-l PAGE_LENGTH' invalid number of lines: 'abc'
//   pr: '-l PAGE_LENGTH' invalid number of lines: '0': Numerical result out of
//   range pr: '-w PAGE_WIDTH' invalid number of characters: '0': Numerical
//   result out of range
auto parse_positive_number(const std::string& label, const std::string& what,
                           const std::string& value)
    -> std::expected<int, std::string> {
  auto fail = [&](bool out_of_range) {
    std::string msg =
        "'" + label + "' invalid number of " + what + ": '" + value + "'";
    if (out_of_range) msg += ": Numerical result out of range";
    return std::unexpected(msg);
  };
  int v = 0;
  try {
    size_t pos = 0;
    v = std::stoi(value, &pos);
    if (pos != value.size()) return fail(false);
  } catch (std::out_of_range&) {
    return fail(true);
  } catch (...) {
    return fail(false);
  }
  if (v <= 0) return fail(true);
  return v;
}

// [GNU] -e/-i take an optional attached [CHAR[WIDTH]] argument. The CHAR part
// (a single non-digit) selects the tab character (TAB is assumed here); the
// WIDTH part must be a number >= 1. Mirrors pr.c messages:
//   pr: '-e' extra characters or invalid number in the argument: '0'
auto parse_tab_spec(const std::string& label, const std::string& raw)
    -> std::expected<int, std::string> {
  auto fail = [&](const std::string& quoted) {
    return std::unexpected("'" + label +
                           "' extra characters or invalid number in the "
                           "argument: '" +
                           quoted + "'");
  };
  size_t i = 0;
  if (i < raw.size() && !std::isdigit(static_cast<unsigned char>(raw[i]))) {
    ++i;  // CHAR part (tab character; assumed TAB here)
  }
  std::string rest = raw.substr(i);
  if (rest.empty()) return 8;  // [GNU] default width
  int v = 0;
  try {
    size_t pos = 0;
    v = std::stoi(rest, &pos);
    if (pos != rest.size()) return fail(rest);
  } catch (...) {
    return fail(rest);
  }
  if (v < 1) return fail(rest);
  return v;
}

// [GNU] -n takes an optional attached [SEP[DIGITS]] argument: when the first
// character is not a digit it is the separator character, and the remaining
// digits give the line-number field width. Mirrors pr.c messages:
//   pr: '-n' extra characters or invalid number in the argument: '3x'
auto parse_number_spec(const std::string& label, const std::string& raw)
    -> std::expected<std::pair<char, int>, std::string> {
  auto fail = [&](const std::string& quoted) {
    return std::unexpected(winux::i18n::format(
        "command.pr.error.extra_characters",
        "'{}' extra characters or invalid number in the argument: '{}'", label,
        quoted));
  };
  size_t i = 0;
  char sep = '\t';
  if (i < raw.size() && !std::isdigit(static_cast<unsigned char>(raw[i]))) {
    sep = raw[i++];
  }
  std::string rest = raw.substr(i);
  if (rest.empty()) return std::pair{sep, 5};  // [GNU] default field width
  int v = 0;
  try {
    size_t pos = 0;
    v = std::stoi(rest, &pos);
    if (pos != rest.size()) return fail(rest);
  } catch (...) {
    return fail(rest);
  }
  if (v < 1) return fail(rest);
  return std::pair{sep, v};
}

auto build_config(const CommandContext<PR_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  // Parse +PAGE option
  for (auto arg : ctx.positionals) {
    std::string arg_str(arg);
    if (arg_str.size() > 0 && arg_str[0] == '+') {
      try {
        cfg.start_page = std::stoi(arg_str.substr(1));
      } catch (...) {
        return std::unexpected("invalid page number");
      }
    } else {
      if (contains_wildcard(arg_str)) {
        auto glob_result = glob_expand(arg_str);
        if (glob_result.expanded) {
          for (const auto& file : glob_result.files) {
            cfg.files.push_back(wstring_to_utf8(file));
          }
          continue;
        }
      }
      cfg.files.push_back(arg_str);
    }
  }

  // [GNU] --double-space is the long-name alias of -d.
  cfg.double_space =
      ctx.get<bool>("-d", false) || ctx.get<bool>("--double-space", false);
  // [GNU] -a/--across only switches the column fill order; it does not
  // imply a column count (pr -a FILE prints single-column output).
  cfg.across = ctx.get<bool>("--across", false) || ctx.get<bool>("-a", false);
  cfg.form_feed =
      ctx.get<bool>("--form-feed", false) || ctx.get<bool>("-f", false);
  cfg.no_file_warnings =
      ctx.get<bool>("--no-file-warnings", false) || ctx.get<bool>("-r", false);
  cfg.omit_header =
      ctx.get<bool>("--omit-header", false) || ctx.get<bool>("-t", false);
  cfg.omit_pagination =
      ctx.get<bool>("--omit-pagination", false) || ctx.get<bool>("-T", false);

  // [GNU] -e/--expand-tabs accept an optional attached [CHAR[WIDTH]] value;
  // the option alone enables expansion with the default width 8. pr.c always
  // reports the short option name '-e' in diagnostics.
  cfg.expand_set = ctx.count({"-e", "--expand", "--expand-tabs"}) > 0;
  std::string expand_raw;
  for (auto name :
       {std::string_view("--expand"), std::string_view("--expand-tabs"),
        std::string_view("-e")}) {
    auto v = ctx.get<std::string>(name, "");
    if (!v.empty()) {
      expand_raw = v;
      break;
    }
  }
  if (!expand_raw.empty()) {
    auto w = parse_tab_spec("-e", expand_raw);
    if (!w) return std::unexpected(w.error());
    cfg.expand_width = *w;
  }

  auto header_opt = ctx.get<std::string>("--header", "");
  if (header_opt.empty()) {
    header_opt = ctx.get<std::string>("-h", "");
  }
  cfg.header = header_opt;

  auto length_opt = ctx.get<std::string>("--length", "");
  if (length_opt.empty()) {
    length_opt = ctx.get<std::string>("-l", "");
  }
  if (!length_opt.empty()) {
    auto v = parse_positive_number("-l PAGE_LENGTH", "lines", length_opt);
    if (!v) return std::unexpected(v.error());
    cfg.page_length = *v;
  }

  // [GNU] -n takes an optional attached [SEP[DIGITS]] argument; the option
  // alone enables numbering with defaults. pr.c always reports the short
  // option name '-n' in diagnostics.
  cfg.number_lines_set = ctx.count({"-n", "--number-lines"}) > 0;
  std::string number_raw;
  for (auto name :
       {std::string_view("--number-lines"), std::string_view("-n")}) {
    auto v = ctx.get<std::string>(name, "");
    if (!v.empty()) {
      number_raw = v;
      break;
    }
  }
  if (!number_raw.empty()) {
    auto spec = parse_number_spec("-n", number_raw);
    if (!spec) return std::unexpected(spec.error());
    cfg.number_sep = spec->first;
    cfg.number_digits = spec->second;
  }

  auto indent_opt = ctx.get<std::string>("--indent", "");
  if (indent_opt.empty()) {
    indent_opt = ctx.get<std::string>("-o", "");
  }
  if (!indent_opt.empty()) {
    // [GNU] pr.c reports '-o MARGIN' invalid line offset: 'abc' for a
    // malformed or negative margin; zero is accepted.
    auto bad_indent = [&]() {
      return std::unexpected(winux::i18n::format(
          "command.pr.error.invalid_line_offset",
          "'-o MARGIN' invalid line offset: '{}'", indent_opt));
    };
    try {
      size_t pos = 0;
      int v = std::stoi(indent_opt, &pos);
      if (pos != indent_opt.size() || v < 0) return bad_indent();
      cfg.indent = v;
    } catch (...) {
      return bad_indent();
    }
  }

  // [GNU] -s takes an optional attached character; a bare -s selects field
  // mode for column output without naming a separator character.
  cfg.old_s = ctx.count({"-s", "--separator"}) > 0;
  auto sep_opt = ctx.get<std::string>("--separator", "");
  if (sep_opt.empty()) {
    sep_opt = ctx.get<std::string>("-s", "");
  }
  cfg.old_s_arg = !sep_opt.empty();
  if (!sep_opt.empty()) {
    cfg.separator = sep_opt;
  }

  auto width_opt = ctx.get<std::string>("--width", "");
  if (width_opt.empty()) {
    width_opt = ctx.get<std::string>("-w", "");
  }
  cfg.width_set = ctx.count({"-w", "--width"}) > 0;
  if (!width_opt.empty()) {
    auto v = parse_positive_number("-w PAGE_WIDTH", "characters", width_opt);
    if (!v) return std::unexpected(v.error());
    cfg.page_width = *v;
  }

  const bool columns_explicit = ctx.count({"-COLUMN", "--columns"}) > 0;
  auto col_opt = ctx.get<std::string>("--columns", "");
  if (col_opt.empty()) {
    col_opt = ctx.get<std::string>("-COLUMN", "");
  }
  if (!col_opt.empty()) {
    try {
      cfg.columns = std::stoi(col_opt);
    } catch (...) {
      return std::unexpected("invalid column count");
    }
    if (cfg.columns < 1) {
      return std::unexpected("column count must be at least 1");
    }
  }

  // New options
  cfg.balance_columns =
      ctx.get<bool>("--balance-columns", false) || ctx.get<bool>("-b", false);
  cfg.show_control_chars = ctx.get<bool>("--show-control-chars", false) ||
                           ctx.get<bool>("-c", false);
  cfg.join_lines =
      ctx.get<bool>("--join-lines", false) || ctx.get<bool>("-J", false);
  cfg.merge = ctx.get<bool>("--merge", false) || ctx.get<bool>("-m", false);
  cfg.show_nonprinting =
      ctx.get<bool>("--show-nonprinting", false) || ctx.get<bool>("-v", false);

  auto date_fmt = ctx.get<std::string>("--date-format", "");
  if (date_fmt.empty()) {
    date_fmt = ctx.get<std::string>("-D", "");
  }
  cfg.date_format = date_fmt;

  // [GNU] -i takes an optional attached [CHAR[WIDTH]] argument. pr.c always
  // reports the short option name '-i' in diagnostics.
  cfg.output_tabs_set = ctx.count({"-i", "--output-tabs"}) > 0;
  std::string output_tabs_raw;
  for (auto name :
       {std::string_view("--output-tabs"), std::string_view("-i")}) {
    auto v = ctx.get<std::string>(name, "");
    if (!v.empty()) {
      output_tabs_raw = v;
      break;
    }
  }
  if (!output_tabs_raw.empty()) {
    auto w = parse_tab_spec("-i", output_tabs_raw);
    if (!w) return std::unexpected(w.error());
    cfg.output_tabs_width = *w;
  }

  auto fln_opt = ctx.get<std::string>("--first-line-number", "");
  if (fln_opt.empty()) {
    fln_opt = ctx.get<std::string>("-N", "");
  }
  cfg.first_line_number = fln_opt;

  cfg.sep_string_set = ctx.count({"-S", "--sep-string"}) > 0;
  auto sep_str = ctx.get<std::string>("--sep-string", "");
  if (sep_str.empty()) {
    sep_str = ctx.get<std::string>("-S", "");
  }
  if (!sep_str.empty()) {
    cfg.sep_string = sep_str;
  }

  // -F is the same as -f (form feed)
  if (ctx.get<bool>("-F", false)) {
    cfg.form_feed = true;
  }

  // [GNU] --pages is the long form of +PAGE.
  auto pages_opt = ctx.get<std::string>("--pages", "");
  if (!pages_opt.empty()) {
    try {
      cfg.start_page = std::stoi(pages_opt);
    } catch (...) {
      return std::unexpected("invalid page number");
    }
  }

  auto pwidth_opt = ctx.get<std::string>("--page-width", "");
  if (pwidth_opt.empty()) pwidth_opt = ctx.get<std::string>("-W", "");
  cfg.page_width_set = ctx.count({"-W", "--page-width"}) > 0;
  if (!pwidth_opt.empty()) {
    auto v = parse_positive_number("-W PAGE_WIDTH", "characters", pwidth_opt);
    if (!v) return std::unexpected(v.error());
    cfg.page_width = *v;
  }

  // [GNU] -m conflicts with both an explicit column count and -a
  // (pr.c: "cannot specify number of columns when printing in parallel",
  // "cannot specify both printing across and printing in parallel").
  if (cfg.merge && columns_explicit) {
    return std::unexpected(
        "cannot specify number of columns when printing in parallel");
  }
  if (cfg.merge && cfg.across) {
    return std::unexpected(
        "cannot specify both printing across and printing in parallel");
  }

  return cfg;
}

auto read_lines(const std::string& filename)
    -> cp::Result<SmallVector<std::string, 1024>> {
  SmallVector<std::string, 1024> lines;
  auto normalize_text_line = [](std::string& line) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
  };

  if (filename == "-") {
    // [GNU] closed stdin (<&-) errors "pr: 'standard input': Bad file
    // descriptor" rather than producing an empty page set.
    if (file_io::stdin_is_bad()) {
      return std::unexpected("'standard input': Bad file descriptor");
    }
    std::string line;
    while (std::getline(std::cin, line)) {
      normalize_text_line(line);
      lines.push_back(line);
    }
  } else {
    auto f = file_io::open_binary_file(filename);
    if (!f) {
      // [GNU] cannot-open diagnostics carry the errno text; directories
      // are reported GNU-style ("<path>: Is a directory").
      auto operand = native_path::make_api_path_operand(filename);
      const DWORD attrs = native_path::operand_target_attributes_w(operand);
      if (native_path::attributes_are_directory(attrs)) {
        return std::unexpected(filename + ": Is a directory");
      }
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading: No such file or directory");
    }

    std::string line;
    while (std::getline(f, line)) {
      // Skip UTF-8 BOM if present at the beginning of the first line
      if (lines.empty() && line.size() >= 3 &&
          static_cast<unsigned char>(line[0]) == 0xEF &&
          static_cast<unsigned char>(line[1]) == 0xBB &&
          static_cast<unsigned char>(line[2]) == 0xBF) {
        line = line.substr(3);
      }
      normalize_text_line(line);
      lines.push_back(line);
    }

    if (f.fail() && !f.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  return lines;
}

// Expand tabs to spaces
auto expand_tabs(const std::string& line, int tab_width) -> std::string {
  std::string result;
  int col = 0;
  for (char c : line) {
    if (c == '\t') {
      int spaces = tab_width - (col % tab_width);
      result.append(spaces, ' ');
      col += spaces;
    } else {
      result += c;
      ++col;
    }
  }
  return result;
}

// Show control characters using hat notation (^X)
auto show_control_chars_hat(const std::string& line) -> std::string {
  std::string result;
  for (unsigned char c : line) {
    if (c < 32 && c != '\t' && c != '\n') {
      result += '^';
      result += static_cast<char>(c + '@');
    } else if (c == 127) {
      result += "^?";
    } else {
      result += static_cast<char>(c);
    }
  }
  return result;
}

// Show non-printing characters using octal backslash notation
auto show_nonprinting_octal(const std::string& line) -> std::string {
  std::string result;
  for (unsigned char c : line) {
    if (c < 32 || c > 126) {
      char buf[8];
      snprintf(buf, sizeof(buf), "\\%03o", c);
      result += buf;
    } else {
      result += static_cast<char>(c);
    }
  }
  return result;
}

// Replace spaces with tabs where possible
auto replace_spaces_with_tabs(const std::string& line, int tab_width)
    -> std::string {
  std::string result;
  int space_count = 0;
  int col = 0;
  for (char c : line) {
    if (c == ' ') {
      ++space_count;
      ++col;
      if (space_count == tab_width) {
        result += '\t';
        space_count = 0;
      }
    } else {
      // Flush remaining spaces
      if (space_count > 0) {
        result.append(space_count, ' ');
        space_count = 0;
      }
      result += c;
      ++col;
    }
  }
  if (space_count > 0) {
    result.append(space_count, ' ');
  }
  return result;
}

// Current wall-clock time honoring the TZ environment variable (GNU pr
// formats the header timestamp in the TZ-selected timezone). Falls back to
// the system timezone when TZ is unset. (Savannah #28492)
auto now_local_st() -> SYSTEMTIME {
  const char* tz = std::getenv("TZ");
  if (tz != nullptr && *tz != '\0') {
    FILETIME now_ft{};
    GetSystemTimeAsFileTime(&now_ft);
    ULARGE_INTEGER uli{};
    uli.LowPart = now_ft.dwLowDateTime;
    uli.HighPart = now_ft.dwHighDateTime;
    const time_t t =
        static_cast<time_t>(uli.QuadPart / 10000000ULL) - 11644473600LL;
    struct tm tmv{};
    if (localtime_s(&tmv, &t) == 0) {
      SYSTEMTIME st{};
      st.wYear = static_cast<WORD>(tmv.tm_year + 1900);
      st.wMonth = static_cast<WORD>(tmv.tm_mon + 1);
      st.wDay = static_cast<WORD>(tmv.tm_mday);
      st.wHour = static_cast<WORD>(tmv.tm_hour);
      st.wMinute = static_cast<WORD>(tmv.tm_min);
      return st;
    }
  }
  SYSTEMTIME st{};
  GetLocalTime(&st);
  return st;
}

// [GNU] init_header uses the file's last-modified time as the header date
// for regular files and the current time for standard input.
auto file_header_time(const std::string& filename) -> SYSTEMTIME {
  if (filename != "-") {
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (GetFileAttributesExW(utf8_to_wstring(filename).c_str(),
                             GetFileExInfoStandard, &data)) {
      ULARGE_INTEGER uli{};
      uli.LowPart = data.ftLastWriteTime.dwLowDateTime;
      uli.HighPart = data.ftLastWriteTime.dwHighDateTime;
      const time_t t =
          static_cast<time_t>(uli.QuadPart / 10000000ULL) - 11644473600LL;
      struct tm tmv{};
      if (localtime_s(&tmv, &t) == 0) {
        SYSTEMTIME st{};
        st.wYear = static_cast<WORD>(tmv.tm_year + 1900);
        st.wMonth = static_cast<WORD>(tmv.tm_mon + 1);
        st.wDay = static_cast<WORD>(tmv.tm_mday);
        st.wHour = static_cast<WORD>(tmv.tm_hour);
        st.wMinute = static_cast<WORD>(tmv.tm_min);
        return st;
      }
    }
  }
  return now_local_st();
}

// Format a date header
auto format_date_header(const std::string& date_format, const SYSTEMTIME& st)
    -> std::string {
  char buf[64];
  if (date_format.empty()) {
    // GNU pr's default header uses an ISO-like local timestamp.
    snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", st.wYear, st.wMonth,
             st.wDay, st.wHour, st.wMinute);
    return buf;
  }
  // Custom format not fully implemented, return default
  snprintf(buf, sizeof(buf), "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
  return buf;
}

// [GNU] Column layout resolved like pr.c init_parameters()/init_funcs():
//   * no -s/-S: the separator is a single space for aligned columns or a
//     TAB in join mode;
//   * -s[CHAR] (the "old" option) without -w/-W annuls alignment: columns
//     become fields joined by the separator (join_lines);
//   * -S STRING selects an explicit separator but keeps alignment;
//   * a single-TAB separator under alignment degrades to a space;
//   * columns > 1 truncate cell text to the column width unless joining.
struct ColumnLayout {
  int columns = 1;
  int margin = 0;
  int colw = 0;
  std::string sep;
  int sep_len = 0;
  bool join = false;
  bool truncate = false;
  bool parallel = false;
  bool stored = false;  // down-column mode: lines are buffered first
  bool numbered = false;
  int in_tab = 8;
  int num_digits = 5;
  char num_sep = '\t';
  int num_width = 0;
  int out_tab = 8;
};

auto make_column_layout(const Config& cfg, int columns, bool parallel)
    -> ColumnLayout {
  ColumnLayout L;
  L.columns = columns;
  L.parallel = parallel;
  L.stored = !parallel && !cfg.across;
  L.in_tab = cfg.expand_width;
  L.margin = cfg.indent;
  L.numbered = cfg.number_lines_set;
  L.num_digits = cfg.number_digits;
  L.num_sep = cfg.number_sep;
  L.out_tab = cfg.output_tabs_set ? cfg.output_tabs_width : 8;
  // [GNU] number_width = digits + TAB_WIDTH(8, digits) when the number
  // separator is TAB, digits + 1 otherwise (pr.c init_parameters).
  L.num_width = cfg.number_digits +
                (cfg.number_sep == '\t' ? (8 - cfg.number_digits % 8) : 1);

  L.join = cfg.join_lines || (cfg.old_s && !cfg.width_set &&
                              !cfg.page_width_set && (parallel || columns > 1));

  const bool use_sep = cfg.sep_string_set || (cfg.old_s && cfg.old_s_arg);
  if (use_sep) {
    L.sep = cfg.sep_string_set ? cfg.sep_string : cfg.separator;
    L.sep_len = static_cast<int>(L.sep.size());
  } else {
    L.sep = L.join ? "\t" : " ";
    L.sep_len = 1;
  }
  if (!L.join && L.sep_len == 1 && L.sep == "\t") {
    L.sep = " ";
  }
  L.truncate = columns > 1 && !L.join;

  const int used_by_number = (parallel && L.numbered) ? L.num_width : 0;
  const int useful =
      cfg.page_width - used_by_number - (columns - 1) * L.sep_len;
  L.colw = columns > 0 ? useful / columns : useful;
  return L;
}

// [GNU] Column c is padded out to start_position - col_sep_length, i.e.
// margin plus the slots (column width + separator) of the columns before
// it.  For parallel numbered output the first slot is wider by the number
// field (pr.c: h_next = h + chars_per_column + number_width).
auto column_pad_target(const ColumnLayout& L, int c) -> int {
  int target = L.margin;
  for (int j = 0; j < c; ++j) {
    target += L.colw + L.sep_len;
    if (L.parallel && L.numbered && j == 0) {
      target += L.num_width;
    }
  }
  return target;
}

// [GNU] Row emitter implementing pr.c's lazy whitespace model: runs of
// spaces are not printed immediately but accumulated (spaces_not_printed)
// and tabified when flushed — a TAB is emitted whenever the next tab stop
// lands on or before the pending goal and more than one column remains.
// Trailing pending spaces at end of row are never emitted.
struct ColumnEmitter {
  const ColumnLayout& L;
  std::string out;
  int outpos = 0;
  int pending = 0;
  int pending_seps = 0;

  void flush_spaces() {
    const int goal = outpos + pending;
    while (goal - outpos > 1) {
      const int next = outpos + (L.out_tab - outpos % L.out_tab);
      if (next > goal) break;
      out += '\t';
      outpos = next;
    }
    while (outpos < goal) {
      out += ' ';
      ++outpos;
    }
    pending = 0;
  }

  // [GNU] print_char: spaces join the pending run; other characters flush
  // it first. Only printable characters advance output_position — a TAB in
  // the text counts as zero width ('\b' backs up one).
  void emit_char(char c) {
    if (c == ' ') {
      ++pending;
      return;
    }
    if (pending > 0) flush_spaces();
    out += c;
    if (c == '\b') {
      --outpos;
    } else if (static_cast<unsigned char>(c) >= 0x20 && c != '\x7f') {
      ++outpos;
    }
  }

  void emit_text(const std::string& s) {
    for (char c : s) emit_char(c);
  }

  // [GNU] print_sep_string: emits every separator counted so far; space
  // characters join the pending run, other characters flush it first and
  // are printed literally (a separator TAB counts as one column, not a tab
  // stop), and a separator ending in spaces flushes the run.
  void emit_seps() {
    if (pending_seps <= 0) {
      if (pending > 0) flush_spaces();
      return;
    }
    for (int k = pending_seps; k > 0; --k) {
      for (char s : L.sep) {
        if (s == ' ') {
          ++pending;
        } else {
          if (pending > 0) flush_spaces();
          out += s;
          ++outpos;
        }
      }
      if (pending > 0) flush_spaces();
    }
    pending_seps = 0;
  }

  // [GNU] pad_across_to assigns the pending run so the logical position
  // reaches the target; overshooting lines leave it negative (no-op pad).
  void pad_to(int target) { pending = target - outpos; }

  // [GNU] add_line_number: right-justified digits followed, for columns>1
  // and a TAB separator, by number_width - digits spaces (the separator is
  // not printed literally in that case).
  void emit_number(long num) {
    const std::string digits = std::to_string(num);
    for (int i = static_cast<int>(digits.size()); i < L.num_digits; ++i) {
      emit_char(' ');
    }
    emit_text(digits);
    if (L.num_sep == '\t') {
      for (int i = L.num_width - L.num_digits; i > 0; --i) emit_char(' ');
    } else {
      emit_char(L.num_sep);
    }
  }
};

struct ColumnCell {
  std::string text;
  bool present = false;
  long number = 0;
};

// [GNU] Print one body row of columns (pr.c print_page inner loop).  GNU
// stores each column's start_position and pads to start_position minus
// col_sep_length; column 0's start_position already includes the separator
// slot (init_funcs presets h = margin + col_sep_length), so its pad target
// is just the margin.  In join mode (-J, or bare -s without -w/-W) the
// start positions are ANYWHERE and no padding happens — except that the
// margin still applies to column 0.  Every column contributes one
// separator slot whether it prints or not; a column that has a line pads
// and flushes the pending separators before its text.  Exhausted parallel
// columns encountered after text still emit their padding/separators,
// while exhausted leading columns are deferred (align_empty_cols): they
// pad — and column 0 prints its number — without emitting separators.
auto emit_column_row(const ColumnLayout& L,
                     const std::vector<ColumnCell>& cells,
                     long parallel_row_number) -> void {
  ColumnEmitter e{L};
  auto pad_for = [&](int c) {
    e.pad_to(column_pad_target(L, c) - (c > 0 ? L.sep_len : 0));
  };
  bool any_printed = false;
  for (int c = 0; c < L.columns; ++c) {
    const ColumnCell& cell = cells[static_cast<size_t>(c)];
    if (cell.present) {
      if (L.parallel && !any_printed && c > 0) {
        // [GNU] align_empty_cols: each exhausted leading column is aligned
        // while separators_not_printed is held at zero, so no separator is
        // emitted for it; the slots are re-counted afterwards.
        for (int q = 0; q < c; ++q) {
          if (!L.join) pad_for(q);
          if (e.pending > 0) e.flush_spaces();
          if (q == 0 && L.numbered) {
            e.emit_number(parallel_row_number);
          }
        }
        // [GNU] read_line then resets spaces_not_printed (0 when joining,
        // chars_per_column when truncating); an aligned column's own
        // pad_across_to overwrites it anyway.
        if (L.join) e.pending = 0;
      }
      if (!L.join || c == 0) pad_for(c);
      e.emit_seps();
      if (L.numbered && (!L.parallel || c == 0)) {
        e.emit_number(cell.number);
      }
      e.emit_text(cell.text);
      // [GNU] print_stored finishes a buffered column by snapping
      // output_position to its start_position plus the stored line length
      // (end_vector[line]); column 0's start_position includes the
      // separator slot which is subtracted back out.  This makes the
      // logical column position — not the tabified visual one — the base
      // for the next column, which matters in join mode where no padding
      // fixes the positions.
      if (L.stored && e.pending == 0) {
        int text_len = 0;
        for (char ch : cell.text) {
          text_len += (ch == '\t') ? (L.in_tab - text_len % L.in_tab) : 1;
        }
        const int cell_len = (L.numbered ? L.num_width : 0) + text_len;
        const int start = (L.join && c > 0) ? 0
                          : c == 0          ? L.margin + L.sep_len
                                            : column_pad_target(L, c);
        int pos = start + cell_len;
        if (start - L.sep_len == L.margin) pos -= L.sep_len;
        e.outpos = pos;
      }
      any_printed = true;
    } else if (L.parallel && any_printed) {
      // [GNU] align_column for a file that ran out mid-row.
      if (!L.join) pad_for(c);
      e.emit_seps();
    }
    ++e.pending_seps;
  }
  safePrintLn(e.out);
}

// [GNU] Apply the per-cell transformations and truncation that pr.c applies
// while reading a column line: input tabs are expanded unless the column
// separator is a single TAB (untabify_input), and aligned columns truncate
// at the column width.
auto prepare_cell_text(const Config& cfg, const ColumnLayout& L,
                       std::string text, bool numbered_cell) -> std::string {
  const bool untabify = L.columns > 1 && !(L.sep_len == 1 && L.sep == "\t");
  if (cfg.expand_set || untabify) {
    text = expand_tabs(text, cfg.expand_width);
  }
  if (cfg.show_control_chars) {
    text = show_control_chars_hat(text);
  }
  if (cfg.show_nonprinting) {
    text = show_nonprinting_octal(text);
  }
  if (L.truncate) {
    int limit = L.colw;
    if (numbered_cell && !L.parallel) {
      limit -= L.num_width;
    }
    if (limit < 0) limit = 0;
    int pos = 0;
    size_t cut = text.size();
    for (size_t i = 0; i < text.size(); ++i) {
      const int w =
          (text[i] == '\t') ? (cfg.expand_width - pos % cfg.expand_width) : 1;
      if (pos + w > limit) {
        cut = i;
        break;
      }
      pos += w;
    }
    text.resize(cut);
  }
  return text;
}

// Print page header
// [GNU] When the page length leaves no room for a body (page_length <= 10,
// i.e. lines_per_body = page_length - 10 <= 0), pr.c sets extremities =
// false: the header block, the 5-line footer margin and all inter-page
// padding are dropped and output is continuous, although pages are still
// counted for +PAGE selection.
auto extremities_on(const Config& cfg) -> bool {
  return !cfg.omit_header && !cfg.omit_pagination && cfg.page_length > 10;
}

auto print_page_header(const Config& cfg, int page_num,
                       const std::string& filename, const std::string& date_str)
    -> void {
  if (!extremities_on(cfg)) return;

  std::string header_text = cfg.header.empty() ? filename : cfg.header;

  // Build header line: date  header  page
  char page_str[32];
  snprintf(page_str, sizeof(page_str), "Page %d", page_num);

  // [GNU] Center the date+text+page composite within the page width, with
  // at least one space on each side of the text. (Savannah #1728)
  const int dlen = static_cast<int>(date_str.size());
  const int tlen = static_cast<int>(header_text.size());
  const int plen = static_cast<int>(strlen(page_str));
  int left = (cfg.page_width - dlen - plen - tlen) / 2;
  if (left < 1) left = 1;
  int right = cfg.page_width - dlen - left - tlen - plen;
  if (right < 1) right = 1;

  // [GNU] The page starts with two blank lines, the header line, and two
  // more blank lines before the body. (Savannah #1728) The -o margin is
  // emitted before the first of those newlines and again before the header
  // text line (pr.c print_header pads across to chars_per_margin first);
  // the remaining header/trailer blank lines carry no margin.
  const std::string margin(cfg.indent, ' ');
  safePrintLn(margin);
  safePrintLn("");
  safePrintLn(margin + date_str + std::string(left, ' ') + header_text +
              std::string(right, ' ') + page_str);
  safePrintLn("");
  safePrintLn("");
}

// [GNU] The header block occupies 5 lines at the top of the page and a
// 5-line margin is kept at the bottom, so the body capacity is
// page_length - 10 (measured against GNU 8.32: 66-line pages break the
// body after 56 lines). (Savannah #1728)
auto header_block_lines(const Config& cfg) -> int {
  return extremities_on(cfg) ? 5 : 0;
}

auto body_capacity(const Config& cfg) -> int {
  if (!extremities_on(cfg)) {
    return std::max(1, cfg.page_length);
  }
  return std::max(1, cfg.page_length - 10);
}

// Print page trailer (blank lines for form feed)
auto print_page_trailer(const Config& cfg) -> void {
  if (!extremities_on(cfg)) return;
  if (cfg.form_feed || cfg.form_feed_ff) {
    safePrint("\f");
  }
}

// [GNU] -n[SEP[DIGITS]] line number field: the number right-justified in
// DIGITS columns (default 5) followed by the separator character (default
// TAB). Numbers wider than the field are not truncated.
auto number_field(const Config& cfg, int line_num) -> std::string {
  std::string field;
  std::string num = std::to_string(line_num);
  if (static_cast<int>(num.size()) < cfg.number_digits) {
    field.append(cfg.number_digits - num.size(), ' ');
  }
  field += num;
  field += cfg.number_sep;
  return field;
}

// [GNU] Paginate one file's lines. Every file is printed by a separate
// print_files(1, &file_names[i]) call in pr.c, so page numbering, line
// numbering, and the header (which names that file) restart per file.
auto paginate_file(const Config& cfg, SmallVector<std::string, 1024> all_lines,
                   const std::string& header_name, const std::string& date_str)
    -> void {
  // Apply start_page: skip lines before the start page
  // [GNU] Paging counts body lines only; the header block is part of the
  // page length. (Savannah #1728)
  // [GNU] -d halves the number of text lines per page (pr.c:
  // lines_per_body = MAX(1, lines_per_body / 2)) and each printed line is
  // followed by a blank, so a page holds lines_per_body*2 physical body
  // lines; when page_length-10 is odd the printed page is one line shorter
  // than -l.
  const int lines_per_page = cfg.double_space
                                 ? std::max(1, body_capacity(cfg) / 2)
                                 : body_capacity(cfg);
  const int body_slots = cfg.double_space ? lines_per_page * 2 : lines_per_page;
  const int page_lines_total =
      extremities_on(cfg) ? body_slots + 10 : body_slots;

  // [GNU] a start page beyond the file's page count is reported and the
  // file is skipped (uutils #13557). pr.c reports this via error(0, ...),
  // so the exit status is unaffected and later files are still printed.
  if (cfg.start_page > 1) {
    const size_t total_lines = all_lines.size();
    int total_pages;
    if (cfg.columns > 1) {
      const size_t rows = (total_lines + cfg.columns - 1) / cfg.columns;
      total_pages =
          static_cast<int>((rows + lines_per_page - 1) / lines_per_page);
    } else {
      total_pages =
          static_cast<int>((total_lines + lines_per_page - 1) / lines_per_page);
    }
    if (total_pages < 1) total_pages = 1;
    if (cfg.start_page > total_pages) {
      safeErrorPrintLn(winux::i18n::format(
          "command.pr.error.page_exceeds",
          "pr: starting page number {} exceeds page count {}", cfg.start_page,
          total_pages));
      return;
    }
  }

  // Process lines with all options
  std::string indent_str(cfg.indent, ' ');
  int line_num =
      cfg.first_line_number.empty() ? 1 : std::stoi(cfg.first_line_number);
  int page_num = 1;
  int lines_on_page = 0;
  bool in_page = false;

  // Track start_page
  bool started = (cfg.start_page <= 1);

  // Process with multi-column support
  if (cfg.columns > 1) {
    // [GNU] One file, multiple columns (pr.c storing_columns): each page
    // holds lines_per_body rows of `columns` cells; the cells of a page are
    // a contiguous chunk of the file, distributed down the columns (or
    // across the rows with -a).  Column heights are balanced.
    const ColumnLayout L = make_column_layout(cfg, cfg.columns, false);
    const size_t total_lines = all_lines.size();
    const int cols = cfg.columns;
    size_t pos = 0;
    while (pos < total_lines) {
      const size_t chunk = std::min(static_cast<size_t>(lines_per_page) * cols,
                                    total_lines - pos);
      std::vector<std::vector<ColumnCell>> page_rows;
      if (cfg.across) {
        const size_t nrows = (chunk + cols - 1) / cols;
        page_rows.assign(nrows, std::vector<ColumnCell>(cols));
        for (size_t i = 0; i < chunk; ++i) {
          ColumnCell& cell = page_rows[i / cols][i % cols];
          cell.present = true;
          cell.number = line_num + static_cast<long>(pos + i);
          cell.text = prepare_cell_text(cfg, L, all_lines[pos + i],
                                        cfg.number_lines_set);
        }
      } else {
        // [GNU] balance(): first chunk%cols columns get one extra line.
        std::vector<size_t> heights(cols);
        size_t maxh = 0;
        size_t off = pos;
        for (int c = 0; c < cols; ++c) {
          heights[c] =
              chunk / cols + (c < static_cast<int>(chunk % cols) ? 1 : 0);
          maxh = std::max(maxh, heights[c]);
        }
        page_rows.assign(maxh, std::vector<ColumnCell>(cols));
        for (int c = 0; c < cols; ++c) {
          for (size_t r = 0; r < heights[c]; ++r) {
            ColumnCell& cell = page_rows[r][c];
            cell.present = true;
            cell.number = line_num + static_cast<long>(off + r);
            cell.text = prepare_cell_text(cfg, L, all_lines[off + r],
                                          cfg.number_lines_set);
          }
          off += heights[c];
        }
      }

      if (page_num >= cfg.start_page) {
        if (extremities_on(cfg)) {
          print_page_header(cfg, page_num, header_name, date_str);
        }
        int used = 0;
        for (const auto& row : page_rows) {
          emit_column_row(L, row, 0);
          ++used;
          if (cfg.double_space) {
            safePrintLn("");
            ++used;
          }
        }
        if (extremities_on(cfg) && !cfg.omit_pagination) {
          for (int pad = page_lines_total - header_block_lines(cfg) - used;
               pad > 0; --pad) {
            safePrintLn("");
          }
          print_page_trailer(cfg);
        }
      }
      pos += chunk;
      ++page_num;
    }
    return;
  }

  {
    // Single column mode
    for (const auto& line : all_lines) {
      if (!started) {
        // Skip lines until we reach start_page
        ++lines_on_page;
        if (lines_on_page >= lines_per_page) {
          lines_on_page = 0;
          ++page_num;
          if (page_num >= cfg.start_page) {
            started = true;
          }
        }
        continue;
      }

      if (!in_page) {
        print_page_header(cfg, page_num, header_name, date_str);
        in_page = true;
      }

      std::string processed = line;

      // Apply expand_tabs
      if (cfg.expand_set) {
        processed = expand_tabs(processed, cfg.expand_width);
      }

      // Apply show_control_chars
      if (cfg.show_control_chars) {
        processed = show_control_chars_hat(processed);
      }

      // Apply show_nonprinting
      if (cfg.show_nonprinting) {
        processed = show_nonprinting_octal(processed);
      }

      // [GNU] -W truncates single-column lines at the page width; the old
      // -w option only selects join mode and leaves lines intact.
      if (cfg.page_width_set) {
        int pos = 0;
        size_t cut = processed.size();
        for (size_t i = 0; i < processed.size(); ++i) {
          const int w = (processed[i] == '\t')
                            ? (cfg.expand_width - pos % cfg.expand_width)
                            : 1;
          if (pos + w > cfg.page_width) {
            cut = i;
            break;
          }
          pos += w;
        }
        processed.resize(cut);
      }

      // Apply output_tabs
      if (cfg.output_tabs_set) {
        processed = replace_spaces_with_tabs(processed, cfg.output_tabs_width);
      }

      std::string output = indent_str;

      // Add line numbers
      if (cfg.number_lines_set) {
        output += number_field(cfg, line_num++);
      }

      output += processed;

      safePrintLn(output);

      ++lines_on_page;
      if (cfg.double_space) {
        // [GNU] The blank line inserted by -d occupies a body slot too.
        safePrintLn("");
        ++lines_on_page;
      }

      if (lines_on_page >= body_slots) {
        // [GNU] Every page is filled to the page length before the break.
        for (int pad =
                 page_lines_total - header_block_lines(cfg) - lines_on_page;
             pad > 0; --pad) {
          safePrintLn("");
        }
        print_page_trailer(cfg);
        ++page_num;
        lines_on_page = 0;
        in_page = false;
      }
    }
  }

  // Final page trailer
  if (in_page && !cfg.omit_pagination) {
    // [GNU] Pad the final page with blank lines up to the page length
    // (Savannah #1728). -t omits headers *and* trailers, so no padding; the
    // same applies when -l leaves no room for a body (page_length <= 10).
    if (extremities_on(cfg)) {
      int used = header_block_lines(cfg) + lines_on_page;
      while (used < page_lines_total) {
        safePrintLn("");
        ++used;
      }
    }
    print_page_trailer(cfg);
  }
}

auto run(const Config& cfg) -> int {
  SmallVector<std::string, 64> files = cfg.files;
  if (files.empty()) {
    files.push_back("-");
  }

  // [GNU] failed_opens in pr.c: any file that cannot be opened makes the
  // process exit nonzero, even when -r suppresses the diagnostic.
  bool all_ok = true;

  // [GNU] -m/--merge prints the files in parallel columns and paginates
  // the result like any other page stream: one shared header per page with
  // an empty file name and the current time (pr.c init_fps/init_header).
  if (cfg.merge) {
    SmallVector<SmallVector<std::string, 1024>, 16> file_lines;
    size_t max_lines = 0;
    for (const auto& file : files) {
      auto lines_result = read_lines(file);
      if (!lines_result) {
        all_ok = false;
        if (!cfg.no_file_warnings) {
          cp::report_error(lines_result, L"pr");
        }
        // [GNU] a file that cannot be opened removes its column entirely.
        continue;
      }
      file_lines.push_back(*lines_result);
      if (lines_result->size() > max_lines) {
        max_lines = lines_result->size();
      }
    }
    if (file_lines.empty()) {
      return 1;
    }

    const int ncols = static_cast<int>(file_lines.size());
    const ColumnLayout L = make_column_layout(cfg, ncols, true);
    if (L.colw < 1) {
      safeErrorPrintLn(winux::i18n::format("command.pr.error.page_width_narrow",
                                           "pr: page width too narrow"));
      return 1;
    }

    const std::string date_str =
        format_date_header(cfg.date_format, now_local_st());
    const int start_num =
        cfg.first_line_number.empty() ? 1 : std::stoi(cfg.first_line_number);
    const int lines_per_page = cfg.double_space
                                   ? std::max(1, body_capacity(cfg) / 2)
                                   : body_capacity(cfg);
    const int body_slots =
        cfg.double_space ? lines_per_page * 2 : lines_per_page;
    const int page_lines_total =
        extremities_on(cfg) ? body_slots + 10 : body_slots;

    // [GNU] a start page beyond the page count is reported, the file is
    // skipped, and the exit status is unaffected (uutils #13557).
    const int total_pages = std::max(
        1, static_cast<int>((max_lines + lines_per_page - 1) / lines_per_page));
    if (cfg.start_page > total_pages) {
      safeErrorPrintLn(winux::i18n::format(
          "command.pr.error.page_exceeds",
          "pr: starting page number {} exceeds page count {}", cfg.start_page,
          total_pages));
      return all_ok ? 0 : 1;
    }

    int page_num = 1;
    size_t row = 0;
    while (row < max_lines) {
      const size_t end =
          std::min(row + static_cast<size_t>(lines_per_page), max_lines);
      if (page_num >= cfg.start_page) {
        if (extremities_on(cfg)) {
          print_page_header(cfg, page_num, "", date_str);
        }
        int used = 0;
        for (; row < end; ++row) {
          std::vector<ColumnCell> cells(ncols);
          for (int c = 0; c < ncols; ++c) {
            if (row < file_lines[c].size()) {
              ColumnCell& cell = cells[static_cast<size_t>(c)];
              cell.present = true;
              cell.number = start_num + static_cast<long>(row);
              cell.text = prepare_cell_text(cfg, L, file_lines[c][row],
                                            cfg.number_lines_set);
            }
          }
          emit_column_row(L, cells, start_num + static_cast<long>(row));
          ++used;
          if (cfg.double_space) {
            safePrintLn("");
            ++used;
          }
        }
        if (extremities_on(cfg) && !cfg.omit_pagination) {
          for (int pad = page_lines_total - header_block_lines(cfg) - used;
               pad > 0; --pad) {
            safePrintLn("");
          }
          print_page_trailer(cfg);
        }
      } else {
        row = end;
      }
      ++page_num;
    }
    return all_ok ? 0 : 1;
  }

  // [GNU] init_parameters fails up front when the page width leaves less
  // than one character per column.
  if (cfg.columns > 1 && make_column_layout(cfg, cfg.columns, false).colw < 1) {
    safeErrorPrintLn(winux::i18n::format("command.pr.error.page_width_narrow",
                                         "pr: page width too narrow"));
    return 1;
  }

  // [GNU] Without -m each file is paginated independently: it gets its own
  // header, its own Page 1, and the start-page selection applies per file
  // (pr.c calls print_files(1, &file_names[i]) per input file).
  for (const auto& file : files) {
    auto lines_result = read_lines(file);
    if (!lines_result) {
      all_ok = false;
      if (!cfg.no_file_warnings) {
        cp::report_error(lines_result, L"pr");
      }
      continue;
    }
    // [GNU] Standard input uses the current date and an empty header name.
    const std::string header_name = (file == "-") ? "" : file;
    const std::string date_str =
        format_date_header(cfg.date_format, file_header_time(file));
    paginate_file(cfg, *lines_result, header_name, date_str);
  }

  return all_ok ? 0 : 1;
}

}  // namespace pr_pipeline

REGISTER_COMMAND(
    pr, "pr", "pr [OPTION]... [FILE]...",
    "Convert text files for printing.\n"
    "\n"
    "Note: This is a simplified implementation. Advanced features\n"
    "like multi-column layout and complex pagination are not fully\n"
    "supported.",
    "  pr file.txt\n"
    "  pr -n file.txt\n"
    "  pr -o 4 file.txt\n"
    "  pr -l 60 file.txt",
    "lp(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", PR_OPTIONS) {
  using namespace pr_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"pr");
    return 1;
  }

  return run(*cfg_result);
}
