// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU]
    OPTION("-b", "--body-numbering", "use STYLE for numbering body lines",
           STRING_TYPE),
    // [GNU]
    OPTION("-d", "--section-delimiter", "use CC for logical page delimiters",
           STRING_TYPE),
    // [GNU]
    OPTION("-f", "--footer-numbering", "use STYLE for numbering footer lines",
           STRING_TYPE),
    // [GNU]
    OPTION("-h", "--header-numbering", "use STYLE for numbering header lines",
           STRING_TYPE),
    // [GNU]
    OPTION("-i", "--line-increment", "line number increment at each line",
           STRING_TYPE),
    // [GNU]
    OPTION("-l", "--join-blank-lines",
           "group NUMBER empty lines as one numbered line", STRING_TYPE),
    // [GNU]
    OPTION("-n", "--number-format", "use FORMAT for line numbers", STRING_TYPE),
    // [GNU]
    OPTION("-p", "--no-renumber", "do not reset line numbers at logical pages",
           BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--number-separator",
           "add STRING after (possible) line number", STRING_TYPE),
    // [GNU]
    OPTION("-v", "--starting-line-number",
           "first line number on each logical page", STRING_TYPE),
    // [GNU]
    OPTION("-w", "--number-width", "width of line numbers", STRING_TYPE)};

namespace nl_pipeline {
namespace cp = core::pipeline;

enum class Section { Header, Body, Footer };

struct Config {
  std::string body_numbering = "t";
  std::string header_numbering = "n";
  std::string footer_numbering = "n";
  long long line_increment = 1;
  long long join_blank_lines = 1;
  std::string number_format = "rn";
  bool no_renumber = false;
  std::string section_delimiter = "\\:";
  std::string separator = "\t";
  long long starting_number = 1;
  long long number_width = 6;
  SmallVector<std::string, 64> files;
};

// [GNU] nl.c parses -i/-v with xdectointmax (any intmax_t value; a leading
// sign is allowed, including 0) and -l/-w with xdectoumax (minimum 1). Any
// trailing junk makes the operand invalid; exceeding the range appends the
// errno description to the diagnostic.
struct NlNumber {
  long long value = 0;
  bool overflow = false;
};

auto parse_nl_number(std::string_view text) -> std::optional<NlNumber> {
  size_t pos = 0;
  bool negative = false;
  if (pos < text.size() && (text[pos] == '+' || text[pos] == '-')) {
    negative = text[pos] == '-';
    ++pos;
  }
  const size_t digits_begin = pos;
  unsigned long long magnitude = 0;
  bool overflow = false;
  while (pos < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[pos])) != 0) {
    const unsigned long long digit =
        static_cast<unsigned long long>(text[pos] - '0');
    if (magnitude >
        (std::numeric_limits<unsigned long long>::max() - digit) / 10) {
      overflow = true;
    } else {
      magnitude = magnitude * 10 + digit;
    }
    ++pos;
  }
  if (pos == digits_begin || pos != text.size()) return std::nullopt;

  NlNumber result;
  constexpr auto kMax =
      static_cast<unsigned long long>(std::numeric_limits<long long>::max());
  if (overflow || (!negative && magnitude > kMax) ||
      (negative && magnitude > kMax + 1)) {
    result.overflow = true;
    return result;
  }
  result.value =
      negative ? (magnitude == kMax + 1 ? std::numeric_limits<long long>::min()
                                        : -static_cast<long long>(magnitude))
               : static_cast<long long>(magnitude);
  return result;
}

// Compose "invalid <what>: '<text>'" plus the GNU errno suffix that the
// failing conversion produced.
auto invalid_number_error(std::string_view key, std::string_view fallback,
                          std::string_view text,
                          std::string_view suffix_key = {},
                          std::string_view suffix_fallback = {})
    -> std::string {
  std::string msg =
      winux::i18n::format(key, std::string(fallback), std::string(text));
  if (!suffix_key.empty()) {
    msg += winux::i18n::translate(suffix_key, suffix_fallback);
  }
  return msg;
}

auto validate_numbering_style(const std::string& style,
                              std::string_view section) -> cp::Result<int> {
  if (style == "t" || style == "a" || style == "n") {
    return 0;
  }
  if (style.starts_with("p")) {
    auto pattern =
        portable_regex::compile(portable_regex::Syntax::Basic, style.substr(1));
    if (!pattern) {
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

// The last occurrence of an option wins in GNU getopt; an option given with
// an empty value still counts as given.
auto last_string_value(const CommandContext<NL_OPTIONS.size()>& ctx,
                       std::initializer_list<std::string_view> names)
    -> std::optional<std::string> {
  const auto occurrences = ctx.string_occurrences(names);
  if (occurrences.empty()) return std::nullopt;
  return occurrences.back().value;
}

auto build_config(const CommandContext<NL_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  // [GNU] numbering-style operands are diagnosed with the rejected value and
  // followed by "Try 'nl --help' for more information." (usage()).
  auto style_error = [](std::string_view section, std::string_view value) {
    return winux::i18n::format("command.nl.error.numbering_style",
                               "invalid {} numbering style: '{}'", section,
                               std::string(value)) +
           "\n" +
           winux::i18n::format("common.try_help",
                               "Try '{} --help' for more information.", "nl");
  };

  if (auto body_opt = last_string_value(ctx, {"-b", "--body-numbering"})) {
    cfg.body_numbering = *body_opt;
    auto valid = validate_numbering_style(cfg.body_numbering, "body");
    if (!valid) return std::unexpected(style_error("body", *body_opt));
  }

  if (auto header_opt = last_string_value(ctx, {"-h", "--header-numbering"})) {
    cfg.header_numbering = *header_opt;
    auto valid = validate_numbering_style(cfg.header_numbering, "header");
    if (!valid) return std::unexpected(style_error("header", *header_opt));
  }

  if (auto footer_opt = last_string_value(ctx, {"-f", "--footer-numbering"})) {
    cfg.footer_numbering = *footer_opt;
    auto valid = validate_numbering_style(cfg.footer_numbering, "footer");
    if (!valid) return std::unexpected(style_error("footer", *footer_opt));
  }

  if (auto increment_opt = last_string_value(ctx, {"-i", "--line-increment"})) {
    // [GNU] xdectointmax: zero and negative increments are legal ("nl -i 0"
    // repeats the same line number); only non-numeric input and overflow
    // are errors.
    auto value = parse_nl_number(*increment_opt);
    if (!value || value->overflow) {
      return std::unexpected(invalid_number_error(
          "command.nl.error.invalid_increment",
          "invalid line number increment: '{}'", *increment_opt,
          (value && value->overflow) ? "common.error.value_too_large"
                                     : std::string_view{},
          ": Value too large for defined data type"));
    }
    cfg.line_increment = value->value;
  }

  if (auto join_blank_opt =
          last_string_value(ctx, {"-l", "--join-blank-lines"})) {
    // [GNU] xdectoumax with minimum 1: 0, negative and overflowing counts
    // are all "Numerical result out of range".
    auto value = parse_nl_number(*join_blank_opt);
    if (!value) {
      return std::unexpected(invalid_number_error(
          "command.nl.error.invalid_join",
          "invalid line number of blank lines: '{}'", *join_blank_opt));
    }
    if (value->overflow || value->value < 1) {
      return std::unexpected(invalid_number_error(
          "command.nl.error.invalid_join",
          "invalid line number of blank lines: '{}'", *join_blank_opt,
          "common.error.result_out_of_range",
          ": Numerical result out of range"));
    }
    cfg.join_blank_lines = value->value;
  }

  if (auto format_opt = last_string_value(ctx, {"-n", "--number-format"})) {
    if (*format_opt != "ln" && *format_opt != "rn" && *format_opt != "rz") {
      return std::unexpected(
          winux::i18n::format("command.nl.error.numbering_format",
                              "invalid line numbering format: '{}'",
                              *format_opt) +
          "\n" +
          winux::i18n::format("common.try_help",
                              "Try '{} --help' for more information.", "nl"));
    }
    cfg.number_format = *format_opt;
  }

  cfg.no_renumber =
      ctx.get<bool>("--no-renumber", false) || ctx.get<bool>("-p", false);

  if (auto delimiter_opt =
          last_string_value(ctx, {"-d", "--section-delimiter"})) {
    if (delimiter_opt->empty()) {
      cfg.section_delimiter.clear();
    } else if (delimiter_opt->size() == 1) {
      cfg.section_delimiter = *delimiter_opt + ":";
    } else {
      cfg.section_delimiter = *delimiter_opt;
    }
  }

  if (auto separator_opt =
          last_string_value(ctx, {"-s", "--number-separator"})) {
    cfg.separator = *separator_opt;
  }

  if (auto start_opt =
          last_string_value(ctx, {"-v", "--starting-line-number"})) {
    auto value = parse_nl_number(*start_opt);
    if (!value || value->overflow) {
      return std::unexpected(invalid_number_error(
          "command.nl.error.invalid_start",
          "invalid starting line number: '{}'", *start_opt,
          (value && value->overflow) ? "common.error.value_too_large"
                                     : std::string_view{},
          ": Value too large for defined data type"));
    }
    cfg.starting_number = value->value;
  }

  if (auto width_opt = last_string_value(ctx, {"-w", "--number-width"})) {
    // [GNU] xdectoumax with minimum 1: 0, negative and overflowing widths
    // are all "Numerical result out of range".
    auto value = parse_nl_number(*width_opt);
    if (!value) {
      return std::unexpected(invalid_number_error(
          "command.nl.error.invalid_width",
          "invalid line number field width: '{}'", *width_opt));
    }
    if (value->overflow || value->value < 1) {
      return std::unexpected(
          invalid_number_error("command.nl.error.invalid_width",
                               "invalid line number field width: '{}'",
                               *width_opt, "common.error.result_out_of_range",
                               ": Numerical result out of range"));
    }
    cfg.number_width = value->value;
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
                        int& blank_count, long long join_blank_lines) -> bool {
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
    auto pattern =
        portable_regex::compile(portable_regex::Syntax::Basic, style.substr(1));
    return pattern && !pattern.pattern.find_all(line).empty();
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

auto format_line_number(long long line_number, const Config& cfg)
    -> std::string {
  char num_buf[64];
  // snprintf field widths are int; widths above INT_MAX saturate.
  const int width =
      cfg.number_width > static_cast<long long>(std::numeric_limits<int>::max())
          ? std::numeric_limits<int>::max()
          : static_cast<int>(cfg.number_width);

  if (cfg.number_format == "ln") {
    snprintf(num_buf, sizeof(num_buf), "%-*lld", width, line_number);
  } else if (cfg.number_format == "rz") {
    snprintf(num_buf, sizeof(num_buf), "%0*lld", width, line_number);
  } else {
    snprintf(num_buf, sizeof(num_buf), "%*lld", width, line_number);
  }

  return num_buf;
}

auto unnumbered_prefix(const Config& cfg) -> std::string {
  const long long width =
      std::min(cfg.number_width,
               static_cast<long long>(std::numeric_limits<int>::max()));
  return std::string(static_cast<size_t>(width) + cfg.separator.size(), ' ');
}

auto print_data_line(const std::string& line, const Config& cfg,
                     Section section, long long& line_number,
                     int& blank_count) {
  bool should_number = should_number_line(style_for_section(cfg, section), line,
                                          blank_count, cfg.join_blank_lines);

  if (should_number) {
    safePrint(format_line_number(line_number, cfg));
    safePrint(cfg.separator);
    safePrintLn(line);
    line_number += cfg.line_increment;
    return;
  }

  safePrint(unnumbered_prefix(cfg));
  safePrintLn(line);
}

auto handle_section_delimiter(const std::string& line, const Config& cfg,
                              Section& section, long long& line_number,
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
                  long long& line_number, int& blank_count) {
  if (handle_section_delimiter(line, cfg, section, line_number, blank_count)) {
    return;
  }

  print_data_line(line, cfg, section, line_number, blank_count);
}

auto run(const Config& cfg) -> int {
  auto nl_input_open_error = [](std::string_view path) -> std::string {
    std::error_code ec;
    auto status = std::filesystem::status(std::filesystem::u8path(path), ec);
    if (!ec && status.type() == std::filesystem::file_type::directory) {
      return std::string("cannot open '") + std::string(path) +
             "' for reading: Is a directory";
    }
    return std::string("cannot open '") + std::string(path) +
           "' for reading: No such file or directory";
  };

  long long line_number = cfg.starting_number;
  int blank_count = 0;
  Section section = Section::Body;

  for (const auto& file : cfg.files) {
    if (file == "-") {
      // [GNU] closed stdin (<&-) errors "nl: -: Bad file descriptor".
      if (file_io::stdin_is_bad()) {
        safeErrorPrintLn("nl: -: Bad file descriptor");
        return 1;
      }
      // Read from stdin
      std::string line;
      while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        process_line(line, cfg, section, line_number, blank_count);
      }
    } else {
      // Read from file
      std::ifstream f(native_path::normalize_api_operand(file),
                      std::ios::binary);
      if (!f) {
        auto err = nl_input_open_error(file);
        cp::Result<int> result = std::unexpected(err);
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
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }

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
