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
 *  - File: nl.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for nl.
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

auto constexpr NL_OPTIONS = std::array{
    OPTION("-b", "--body-numbering", "use STYLE for numbering body lines",
           STRING_TYPE),
    OPTION("-d", "--section-delimiter", "use CC for logical page delimiters",
           STRING_TYPE),
    OPTION("-f", "--footer-numbering", "use STYLE for numbering footer lines",
           STRING_TYPE),
    OPTION("-h", "--header-numbering", "use STYLE for numbering header lines",
           STRING_TYPE),
    OPTION("-i", "--line-increment", "line number increment at each line",
           STRING_TYPE),
    OPTION("-l", "--join-blank-lines",
           "group NUMBER empty lines as one numbered line", STRING_TYPE),
    OPTION("-n", "--number-format", "use FORMAT for line numbers", STRING_TYPE),
    OPTION("-p", "--no-renumber", "do not reset line numbers at logical pages",
           BOOL_TYPE),
    OPTION("-s", "--number-separator",
           "add STRING after (possible) line number", STRING_TYPE),
    OPTION("-v", "--starting-line-number",
           "first line number on each logical page", STRING_TYPE),
    OPTION("-w", "--number-width", "width of line numbers", STRING_TYPE)};

namespace nl_pipeline {
namespace cp = core::pipeline;

enum class Section { Header, Body, Footer };

struct Config {
  std::string body_numbering = "t";
  std::string header_numbering = "n";
  std::string footer_numbering = "n";
  int line_increment = 1;
  int join_blank_lines = 1;
  std::string number_format = "rn";
  bool no_renumber = false;
  std::string section_delimiter = "\\:";
  std::string separator = "\t";
  int starting_number = 1;
  int number_width = 6;
  SmallVector<std::string, 64> files;
};

auto parse_int(const std::string& text, std::string_view error)
    -> cp::Result<int> {
  try {
    size_t pos = 0;
    int value = std::stoi(text, &pos);
    if (pos != text.size()) {
      return std::unexpected(std::string(error));
    }
    return value;
  } catch (...) {
    return std::unexpected(std::string(error));
  }
}

auto validate_numbering_style(const std::string& style,
                              std::string_view section) -> cp::Result<int> {
  if (style == "t" || style == "a" || style == "n") {
    return 0;
  }
  if (style.starts_with("p")) {
    try {
      std::regex unused(style.substr(1), std::regex_constants::basic);
    } catch (const std::regex_error&) {
      return std::unexpected("invalid " + std::string(section) +
                             " numbering style");
    }
    return 0;
  }
  return std::unexpected("invalid " + std::string(section) +
                         " numbering style");
}

auto section_token(const Config& cfg, int repeats) -> std::string {
  std::string token;
  for (int i = 0; i < repeats; ++i) {
    token += cfg.section_delimiter;
  }
  return token;
}

auto build_config(const CommandContext<NL_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  auto body_opt = ctx.get<std::string>("--body-numbering", "");
  if (body_opt.empty()) {
    body_opt = ctx.get<std::string>("-b", "");
  }
  if (!body_opt.empty()) {
    cfg.body_numbering = body_opt;
    auto valid = validate_numbering_style(cfg.body_numbering, "body");
    if (!valid) return std::unexpected(valid.error());
  }

  auto header_opt = ctx.get<std::string>("--header-numbering", "");
  if (header_opt.empty()) {
    header_opt = ctx.get<std::string>("-h", "");
  }
  if (!header_opt.empty()) {
    cfg.header_numbering = header_opt;
    auto valid = validate_numbering_style(cfg.header_numbering, "header");
    if (!valid) return std::unexpected(valid.error());
  }

  auto footer_opt = ctx.get<std::string>("--footer-numbering", "");
  if (footer_opt.empty()) {
    footer_opt = ctx.get<std::string>("-f", "");
  }
  if (!footer_opt.empty()) {
    cfg.footer_numbering = footer_opt;
    auto valid = validate_numbering_style(cfg.footer_numbering, "footer");
    if (!valid) return std::unexpected(valid.error());
  }

  auto increment_opt = ctx.get<std::string>("--line-increment", "");
  if (increment_opt.empty()) {
    increment_opt = ctx.get<std::string>("-i", "");
  }
  if (!increment_opt.empty()) {
    auto value = parse_int(increment_opt, "invalid line increment");
    if (!value) return std::unexpected(value.error());
    cfg.line_increment = *value;
  }

  auto join_blank_opt = ctx.get<std::string>("--join-blank-lines", "");
  if (join_blank_opt.empty()) {
    join_blank_opt = ctx.get<std::string>("-l", "");
  }
  if (!join_blank_opt.empty()) {
    auto value = parse_int(join_blank_opt, "invalid blank line count");
    if (!value) return std::unexpected(value.error());
    if (*value <= 0) {
      return std::unexpected("blank line count must be positive");
    }
    cfg.join_blank_lines = *value;
  }

  auto format_opt = ctx.get<std::string>("--number-format", "");
  if (format_opt.empty()) {
    format_opt = ctx.get<std::string>("-n", "");
  }
  if (!format_opt.empty()) {
    if (format_opt != "ln" && format_opt != "rn" && format_opt != "rz") {
      return std::unexpected("invalid line number format");
    }
    cfg.number_format = format_opt;
  }

  cfg.no_renumber =
      ctx.get<bool>("--no-renumber", false) || ctx.get<bool>("-p", false);

  auto delimiter_opt = ctx.get<std::string>("--section-delimiter", "");
  if (delimiter_opt.empty() && ctx.has("-d")) {
    delimiter_opt = ctx.get<std::string>("-d", "");
  }
  if (ctx.has("--section-delimiter") || ctx.has("-d")) {
    if (delimiter_opt.empty()) {
      cfg.section_delimiter.clear();
    } else if (delimiter_opt.size() == 1) {
      cfg.section_delimiter = delimiter_opt + ":";
    } else {
      cfg.section_delimiter = delimiter_opt;
    }
  }

  auto separator_opt = ctx.get<std::string>("--number-separator", "");
  if (separator_opt.empty()) {
    separator_opt = ctx.get<std::string>("-s", "");
  }
  if (ctx.has("--number-separator") || ctx.has("-s")) {
    cfg.separator = separator_opt;
  }

  auto start_opt = ctx.get<std::string>("--starting-line-number", "");
  if (start_opt.empty()) {
    start_opt = ctx.get<std::string>("-v", "");
  }
  if (!start_opt.empty()) {
    auto value = parse_int(start_opt, "invalid starting line number");
    if (!value) return std::unexpected(value.error());
    cfg.starting_number = *value;
  }

  auto width_opt = ctx.get<std::string>("--number-width", "");
  if (width_opt.empty()) {
    width_opt = ctx.get<std::string>("-w", "");
  }
  if (!width_opt.empty()) {
    auto value = parse_int(width_opt, "invalid line number width");
    if (!value) return std::unexpected(value.error());
    if (*value <= 0) {
      return std::unexpected("line number width must be positive");
    }
    cfg.number_width = *value;
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

auto should_number_line(const std::string& style, const std::string& line,
                        int& blank_count, int join_blank_lines) -> bool {
  if (style == "n") {
    blank_count = 0;
    return false;
  }

  if (style == "t") {
    blank_count = 0;
    return !line.empty();
  }

  if (style.starts_with("p")) {
    blank_count = 0;
    std::regex pattern(style.substr(1), std::regex_constants::basic);
    return std::regex_search(line, pattern);
  }

  if (!line.empty()) {
    blank_count = 0;
    return true;
  }

  ++blank_count;
  if (blank_count == join_blank_lines) {
    blank_count = 0;
    return true;
  }

  return false;
}

auto style_for_section(const Config& cfg, Section section)
    -> const std::string& {
  if (section == Section::Header) return cfg.header_numbering;
  if (section == Section::Footer) return cfg.footer_numbering;
  return cfg.body_numbering;
}

auto format_line_number(int line_number, const Config& cfg) -> std::string {
  char num_buf[64];

  if (cfg.number_format == "ln") {
    snprintf(num_buf, sizeof(num_buf), "%-*d", cfg.number_width, line_number);
  } else if (cfg.number_format == "rz") {
    snprintf(num_buf, sizeof(num_buf), "%0*d", cfg.number_width, line_number);
  } else {
    snprintf(num_buf, sizeof(num_buf), "%*d", cfg.number_width, line_number);
  }

  return num_buf;
}

auto print_data_line(const std::string& line, const Config& cfg,
                     Section section, int& line_number, int& blank_count) {
  bool should_number = should_number_line(style_for_section(cfg, section), line,
                                          blank_count, cfg.join_blank_lines);

  if (should_number) {
    safePrint(format_line_number(line_number, cfg));
    safePrint(cfg.separator);
    safePrintLn(line);
    line_number += cfg.line_increment;
    return;
  }

  safePrintLn(cfg.separator + line);
}

auto handle_section_delimiter(const std::string& line, const Config& cfg,
                              Section& section, int& line_number,
                              int& blank_count) -> bool {
  if (cfg.section_delimiter.empty()) {
    return false;
  }

  if (line == section_token(cfg, 3)) {
    section = Section::Header;
  } else if (line == section_token(cfg, 2)) {
    section = Section::Body;
  } else if (line == section_token(cfg, 1)) {
    section = Section::Footer;
  } else {
    return false;
  }

  blank_count = 0;
  if (!cfg.no_renumber) {
    line_number = cfg.starting_number;
  }
  safePrintLn("");
  return true;
}

auto process_line(const std::string& line, const Config& cfg, Section& section,
                  int& line_number, int& blank_count) {
  if (handle_section_delimiter(line, cfg, section, line_number, blank_count)) {
    return;
  }

  print_data_line(line, cfg, section, line_number, blank_count);
}

auto run(const Config& cfg) -> int {
  int line_number = cfg.starting_number;
  int blank_count = 0;
  Section section = Section::Body;

  for (const auto& file : cfg.files) {
    if (file == "-") {
      // Read from stdin
      std::string line;
      while (std::getline(std::cin, line)) {
        process_line(line, cfg, section, line_number, blank_count);
      }
    } else {
      // Read from file
      std::ifstream f(file, std::ios::binary);
      if (!f) {
        auto err = std::string("cannot open '") + file + "' for reading";
        cp::Result<int> result = std::unexpected(std::string_view(err));
        cp::report_error(result, L"nl");
        return 1;
      }

      bool first_line = true;
      std::string line;
      while (std::getline(f, line)) {
        // Skip UTF-8 BOM if present at the beginning of the first line
        if (first_line && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
          line = line.substr(3);
        }
        first_line = false;

        process_line(line, cfg, section, line_number, blank_count);
      }

      if (f.fail() && !f.eof()) {
        cp::Result<int> result = std::unexpected("error reading from file");
        cp::report_error(result, L"nl");
        return 1;
      }
    }
  }

  return 0;
}

}  // namespace nl_pipeline

REGISTER_COMMAND(
    nl, "nl", "nl [OPTION]... [FILE]...",
    "Number lines of files.\n"
    "\n"
    "Write each FILE to standard output, with line numbers added.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "Supports GNU-compatible numbering formats, logical page delimiters,\n"
    "renumbering controls, and blank-line grouping.",
    "  nl file.txt\n"
    "  nl -b a file.txt          # number all lines\n"
    "  nl -i 5 file.txt          # increment by 5\n"
    "  nl -s ': ' file.txt       # use custom separator\n"
    "  nl -v 10 file.txt         # start from 10\n"
    "  nl -w 3 file.txt         # 3-digit numbers",
    "cat(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", NL_OPTIONS) {
  using namespace nl_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"nl");
    return 1;
  }

  return run(*cfg_result);
}
