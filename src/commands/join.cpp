// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for join.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
// *** SIMPLIFIED IMPLEMENTATION - Some features may not be fully supported ***

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr JOIN_OPTIONS = std::array{
    // [GNU]
    OPTION("-1", "", "join on this FIELD of file 1", STRING_TYPE),
    // [GNU]
    OPTION("-2", "", "join on this FIELD of file 2", STRING_TYPE),
    // [GNU]
    OPTION("-t", "", "use CHAR as input and output field separator",
           STRING_TYPE),
    // [GNU]
    OPTION("-e", "--empty", "replace missing input fields with EMPTY",
           STRING_TYPE),
    // [GNU]
    OPTION("-o", "--output", "use specified output format", STRING_TYPE),
    // [GNU]
    OPTION("-j", "", "equivalent to -1 FIELD -2 FIELD", STRING_TYPE),
    // [GNU]
    OPTION("-a", "", "print unpairable lines from file 1 or 2", STRING_TYPE),
    // [GNU]
    OPTION("-v", "", "print only unpairable lines from file 1 or 2",
           STRING_TYPE),
    // [GNU]
    OPTION("-i", "--ignore-case", "ignore differences in case", BOOL_TYPE),
    // [GNU]
    OPTION("", "--check-order", "check that the input is correctly sorted",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--nocheck-order",
           "do not check that the input is correctly sorted", BOOL_TYPE),
    // [GNU]
    OPTION("", "--header", "treat the first line in each file as headers",
           BOOL_TYPE),
    // [GNU]
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline",
           BOOL_TYPE),
    // [EXTENSION] GNU join has no -k/--key; FIELD[.CHAR] selects the join
    // field of both files and optionally starts the key at character CHAR
    // of that field (1-based), in the style of 'sort -k'.
    OPTION("-k", "--key", "join on FIELD[.CHAR] of both files", STRING_TYPE)};

namespace join_pipeline {
namespace cp = core::pipeline;

struct OutputField {
  int file = 0;
  int field = 0;
};

struct ParsedLine {
  SmallVector<std::string, 64> fields;
  std::string key;
};

struct Config {
  int field1 = 1;
  int field2 = 1;
  // 1-based character offset within the join field at which the comparison
  // key starts; only set via -k/--key FIELD.CHAR.
  int key_char1 = 1;
  int key_char2 = 1;
  bool explicit_separator = false;
  bool whole_line = false;
  bool ignore_case = false;
  bool check_order = false;
  bool nocheck_order = false;
  bool header = false;
  bool zero_terminated = false;
  char separator = ' ';
  std::string empty_field;
  std::string output_format;
  bool output_auto = false;
  std::vector<OutputField> output_fields;
  bool print_unpaired1 = false;
  bool print_unpaired2 = false;
  bool only_unpaired = false;
  SmallVector<std::string, 64> files;
};

auto parse_positive_field(const std::string& text) -> cp::Result<int> {
  int value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec == std::errc::result_out_of_range) {
    // [GNU] out-of-range field numbers are silently clamped, matching
    // xstrtoimax's PTRDIFF_MAX clamp (uutils #13376)
    return std::numeric_limits<int>::max();
  }
  if (ec != std::errc() || ptr != text.data() + text.size() || value <= 0) {
    return std::unexpected("invalid field number: '" + text + "'");
  }
  return value;
}

auto split_fields(const std::string& line, const Config& cfg)
    -> SmallVector<std::string, 64> {
  SmallVector<std::string, 64> fields;

  if (cfg.whole_line) {
    fields.push_back(line);
    return fields;
  }

  if (!cfg.explicit_separator) {
    size_t pos = 0;
    while (pos < line.size()) {
      while (pos < line.size() &&
             (line[pos] == ' ' || line[pos] == '\t' ||
              (cfg.zero_terminated && line[pos] == '\n'))) {
        ++pos;
      }

      if (pos >= line.size()) {
        break;
      }

      size_t start = pos;
      while (pos < line.size() && line[pos] != ' ' && line[pos] != '\t' &&
             !(cfg.zero_terminated && line[pos] == '\n')) {
        ++pos;
      }
      fields.push_back(line.substr(start, pos - start));
    }
    return fields;
  }

  std::string current;
  for (char c : line) {
    if (c == cfg.separator) {
      fields.push_back(current);
      current.clear();
      continue;
    }
    current.push_back(c);
  }
  fields.push_back(current);

  return fields;
}

auto normalize_key(std::string key, bool ignore_case) -> std::string {
  if (!ignore_case) return key;

  for (auto& ch : key) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return key;
}

auto parse_line(const std::string& line, const Config& cfg, int field_num,
                int char_num) -> ParsedLine {
  ParsedLine parsed;
  parsed.fields = split_fields(line, cfg);
  if (field_num <= static_cast<int>(parsed.fields.size())) {
    const auto& field = parsed.fields[static_cast<size_t>(field_num - 1)];
    size_t offset = std::min(field.size(), static_cast<size_t>(char_num - 1));
    parsed.key = field.substr(offset);
  }
  parsed.key = normalize_key(parsed.key, cfg.ignore_case);

  return parsed;
}

auto trim_text_record(std::string line, char delimiter) -> std::string {
  if (delimiter == '\n' && !line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  return line;
}

auto join_input_open_error(std::string_view filename) -> std::string {
  std::error_code ec;
  auto status = std::filesystem::status(std::filesystem::u8path(filename), ec);
  if (!ec && status.type() == std::filesystem::file_type::directory) {
    return std::string(filename) + ": Is a directory";
  }
  return std::string(filename) + ": No such file or directory";
}

auto output_separator(const Config& cfg) -> std::string {
  if (!cfg.explicit_separator) return " ";
  return std::string(1, cfg.separator);
}

auto parse_file_number(const std::string& value, std::string_view option)
    -> cp::Result<int> {
  if (value == "1") return 1;
  if (value == "2") return 2;
  // [GNU] -a/-v share the field-number diagnostic.
  return std::unexpected(
      winux::i18n::format("common.error.invalid_field_number",
                          "invalid field number: '{}'", value));
}

auto parse_output_format(const std::string& format)
    -> cp::Result<std::vector<OutputField>> {
  std::vector<OutputField> fields;
  std::string token;

  auto flush_token = [&]() -> cp::Result<int> {
    if (token.empty()) return 0;
    if (token == "0") {
      fields.push_back(OutputField{0, 0});
      token.clear();
      return 0;
    }

    auto dot = token.find('.');
    if (dot == std::string::npos || dot == 0 || dot + 1 >= token.size()) {
      return std::unexpected("invalid output field list");
    }

    std::string file_part = token.substr(0, dot);
    std::string field_part = token.substr(dot + 1);
    if (file_part != "1" && file_part != "2") {
      return std::unexpected("invalid output file number");
    }

    auto field = parse_positive_field(field_part);
    if (!field) {
      return std::unexpected(field.error());
    }

    fields.push_back(OutputField{file_part == "1" ? 1 : 2, *field});
    token.clear();
    return 0;
  };

  for (char ch : format) {
    if (ch == ',' || ch == ' ' || ch == '\t') {
      auto result = flush_token();
      if (!result) {
        return std::unexpected(result.error());
      }
      continue;
    }
    token.push_back(ch);
  }

  auto result = flush_token();
  if (!result) {
    return std::unexpected(result.error());
  }

  if (fields.empty()) {
    return std::unexpected("empty output field list");
  }

  return fields;
}

auto build_config(const CommandContext<JOIN_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  // Don't override the default separator (space) from Config struct
  // cfg.separator = '\t';  // Removed - use default space separator

  // [GNU] set_join_field(): assigning a join field twice with incompatible
  // values is an error.  The diagnostic prints the zero-based field index.
  bool field1_seen = false;
  bool field2_seen = false;
  auto set_field = [&](int which, int value) -> cp::Result<void> {
    int& slot = which == 1 ? cfg.field1 : cfg.field2;
    bool& seen = which == 1 ? field1_seen : field2_seen;
    if (seen && slot != value) {
      return std::unexpected(winux::i18n::format(
          "command.join.error.incompatible_fields",
          "incompatible join fields {}, {}", std::to_string(slot - 1),
          std::to_string(value - 1)));
    }
    seen = true;
    slot = value;
    return {};
  };

  // [EXTENSION] -k/--key FIELD[.CHAR]: like "-1 FIELD -2 FIELD", with an
  // optional 1-based character offset applied to the comparison key of both
  // files.  -1/-2/-j may still override the field afterwards.
  auto key_opt = ctx.get<std::string>("-k", "");
  if (!key_opt.empty()) {
    auto dot = key_opt.find('.');
    auto field = parse_positive_field(key_opt.substr(0, dot));
    if (!field) return std::unexpected(field.error());
    if (auto r = set_field(1, *field); !r) return std::unexpected(r.error());
    if (auto r = set_field(2, *field); !r) return std::unexpected(r.error());
    if (dot != std::string::npos) {
      auto char_num = parse_positive_field(key_opt.substr(dot + 1));
      if (!char_num) return std::unexpected(char_num.error());
      cfg.key_char1 = *char_num;
      cfg.key_char2 = *char_num;
    }
  }

  auto field1_opt = ctx.get<std::string>("-1", "");
  if (!field1_opt.empty()) {
    auto field = parse_positive_field(field1_opt);
    if (!field) return std::unexpected(field.error());
    if (auto r = set_field(1, *field); !r) return std::unexpected(r.error());
  }

  auto field2_opt = ctx.get<std::string>("-2", "");
  if (!field2_opt.empty()) {
    auto field = parse_positive_field(field2_opt);
    if (!field) return std::unexpected(field.error());
    if (auto r = set_field(2, *field); !r) return std::unexpected(r.error());
  }

  auto j_opt = ctx.get<std::string>("-j", "");
  if (!j_opt.empty()) {
    auto field = parse_positive_field(j_opt);
    if (!field) return std::unexpected(field.error());
    if (auto r = set_field(1, *field); !r) return std::unexpected(r.error());
    if (auto r = set_field(2, *field); !r) return std::unexpected(r.error());
  }

  if (ctx.has("-t")) {
    auto sep_opt = ctx.get<std::string>("-t", "");
    cfg.explicit_separator = true;
    if (sep_opt.empty()) {
      cfg.whole_line = true;
    } else if (sep_opt == "\\0") {
      cfg.separator = '\0';
    } else {
      if (sep_opt.size() != 1) {
        return std::unexpected("separator must be a single character");
      }
      cfg.separator = sep_opt[0];
    }
  }

  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-i", false);
  cfg.check_order = ctx.get<bool>("--check-order", false) &&
                    !ctx.get<bool>("--nocheck-order", false);
  cfg.nocheck_order = ctx.get<bool>("--nocheck-order", false);
  cfg.header = ctx.get<bool>("--header", false);
  cfg.zero_terminated =
      ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false);

  auto empty_opt = ctx.get<std::string>("--empty", "");
  if (empty_opt.empty()) {
    empty_opt = ctx.get<std::string>("-e", "");
  }
  cfg.empty_field = empty_opt;

  auto output_opt = ctx.get<std::string>("--output", "");
  if (output_opt.empty()) {
    output_opt = ctx.get<std::string>("-o", "");
  }
  cfg.output_format = output_opt;
  if (!cfg.output_format.empty()) {
    if (cfg.output_format == "auto") {
      cfg.output_auto = true;
    } else {
      auto fields = parse_output_format(cfg.output_format);
      if (!fields) {
        return std::unexpected(fields.error());
      }
      cfg.output_fields = std::move(*fields);
    }
  }

  for (const auto& value : ctx.get_all<std::string>("-a")) {
    auto file_number = parse_file_number(value, "-a");
    if (!file_number) return std::unexpected(file_number.error());
    if (*file_number == 1) {
      cfg.print_unpaired1 = true;
    } else {
      cfg.print_unpaired2 = true;
    }
  }

  for (const auto& value : ctx.get_all<std::string>("-v")) {
    auto file_number = parse_file_number(value, "-v");
    if (!file_number) return std::unexpected(file_number.error());
    cfg.only_unpaired = true;
    if (*file_number == 1) {
      cfg.print_unpaired1 = true;
    } else {
      cfg.print_unpaired2 = true;
    }
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

  // Keep these diagnostics as English fallbacks here; they are routed
  // through i18n::translate_error / i18n::format at the print site so the
  // handler can still recognize them in any locale.
  if (cfg.files.empty()) {
    return std::unexpected(std::string("missing operand"));
  }
  if (cfg.files.size() < 2) {
    // [GNU] join.c: "missing operand after %s" quotes argv[argc - 1], i.e.
    // the last command-line argument as given (options are not reordered).
    return std::unexpected("missing operand after '" +
                           std::string(ctx.raw_args.back()) + "'");
  }
  if (cfg.files.size() > 2) {
    // [GNU] join.c: "extra operand %s" uses quoteaf (plain '...' quotes).
    return std::unexpected("extra operand '" + cfg.files[2] + "'");
  }

  return cfg;
}

auto read_lines(const std::string& filename, char delimiter)
    -> cp::Result<SmallVector<std::string, 1024>> {
  SmallVector<std::string, 1024> lines;

  if (filename == "-") {
    std::string content;
    {
      std::ostringstream oss;
      oss << std::cin.rdbuf();
      content = oss.str();
    }
    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        lines.emplace_back(
            trim_text_record(content.substr(start, i - start), delimiter));
        start = i + 1;
      }
    }
    if (start < content.size()) {
      lines.emplace_back(trim_text_record(content.substr(start), delimiter));
    }
  } else {
    auto f = file_io::open_binary_file(filename);
    if (!f) {
      return std::unexpected(join_input_open_error(filename));
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        std::string line =
            trim_text_record(content.substr(start, i - start), delimiter);
        // Skip UTF-8 BOM if present at the beginning of the first line
        if (lines.empty() && line.size() >= 3 &&
            static_cast<unsigned char>(line[0]) == 0xEF &&
            static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF) {
          line = line.substr(3);
        }
        lines.push_back(line);
        start = i + 1;
      }
    }
    if (start < content.size()) {
      std::string line = trim_text_record(content.substr(start), delimiter);
      if (lines.empty() && line.size() >= 3 &&
          static_cast<unsigned char>(line[0]) == 0xEF &&
          static_cast<unsigned char>(line[1]) == 0xBB &&
          static_cast<unsigned char>(line[2]) == 0xBF) {
        line = line.substr(3);
      }
      lines.push_back(line);
    }

    if (f.fail() && !f.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  return lines;
}

// [GNU] join.c: in the default check mode a disorder warning is issued only
// after an unpairable line has been seen; --check-order makes the first
// disorder fatal.  Returns false when processing must stop (fatal disorder).
struct OrderState {
  bool seen_unpairable = false;
  bool issued[2] = {false, false};
  bool disorder = false;
  size_t next_check[2] = {0, 0};
};

auto check_line_order(OrderState& state, int which, size_t idx, size_t start,
                      const std::vector<ParsedLine>& parsed,
                      const SmallVector<std::string, 1024>& raw_lines,
                      const std::string& fname, bool check_order,
                      bool nocheck_order) -> bool {
  size_t& nc = state.next_check[which - 1];
  for (; nc <= idx; ++nc) {
    if (nc > start && !state.issued[which - 1] && !nocheck_order &&
        (check_order || state.seen_unpairable) &&
        parsed[nc].key < parsed[nc - 1].key) {
      state.issued[which - 1] = true;
      // [GNU] "join: FILE:LINE: is not sorted: LINE"
      const auto message =
          winux::i18n::format("command.join.error.file_not_sorted",
                              "join: {}:{}: is not sorted: {}", fname,
                              std::to_string(nc + 1), raw_lines[nc]);
      if (check_order) {
        // [GNU] --check-order: fatal at read time, before further output.
        safeErrorPrintLn(message);
        return false;
      }
      safeErrorPrintLn(message);
      state.disorder = true;
    }
  }
  return true;
}

auto field_value(const ParsedLine* line, int field, const Config& cfg)
    -> std::string {
  if (!line || field <= 0 || field > static_cast<int>(line->fields.size())) {
    return cfg.empty_field;
  }
  return line->fields[static_cast<size_t>(field - 1)];
}

auto join_key_value(const ParsedLine* line1, const ParsedLine* line2,
                    const Config& cfg) -> std::string {
  if (line1 && cfg.field1 <= static_cast<int>(line1->fields.size())) {
    return line1->fields[static_cast<size_t>(cfg.field1 - 1)];
  }
  if (line2 && cfg.field2 <= static_cast<int>(line2->fields.size())) {
    return line2->fields[static_cast<size_t>(cfg.field2 - 1)];
  }
  return cfg.empty_field;
}

auto build_auto_output_fields(const std::vector<ParsedLine>& lines1,
                              const std::vector<ParsedLine>& lines2,
                              const Config& cfg) -> std::vector<OutputField> {
  std::vector<OutputField> fields;
  fields.push_back(OutputField{0, 0});

  size_t count1 = lines1.empty() ? 0 : lines1[0].fields.size();
  size_t count2 = lines2.empty() ? 0 : lines2[0].fields.size();

  for (size_t i = 1; i <= count1; ++i) {
    if (static_cast<int>(i) != cfg.field1) {
      fields.push_back(OutputField{1, static_cast<int>(i)});
    }
  }
  for (size_t i = 1; i <= count2; ++i) {
    if (static_cast<int>(i) != cfg.field2) {
      fields.push_back(OutputField{2, static_cast<int>(i)});
    }
  }

  return fields;
}

auto build_output_line(const ParsedLine* line1, const ParsedLine* line2,
                       const Config& cfg,
                       const std::vector<OutputField>& output_fields)
    -> std::string {
  std::vector<std::string> out_fields;

  if (!output_fields.empty()) {
    for (const auto& spec : output_fields) {
      if (spec.file == 0) {
        out_fields.push_back(join_key_value(line1, line2, cfg));
      } else if (spec.file == 1) {
        out_fields.push_back(field_value(line1, spec.field, cfg));
      } else {
        out_fields.push_back(field_value(line2, spec.field, cfg));
      }
    }
  } else {
    out_fields.push_back(join_key_value(line1, line2, cfg));

    if (line1) {
      for (size_t i = 1; i <= line1->fields.size(); ++i) {
        if (static_cast<int>(i) != cfg.field1) {
          out_fields.push_back(line1->fields[i - 1]);
        }
      }
    }

    if (line2) {
      for (size_t i = 1; i <= line2->fields.size(); ++i) {
        if (static_cast<int>(i) != cfg.field2) {
          out_fields.push_back(line2->fields[i - 1]);
        }
      }
    }
  }

  std::string output;
  const auto sep = output_separator(cfg);
  for (size_t i = 0; i < out_fields.size(); ++i) {
    if (i > 0) output += sep;
    output += out_fields[i];
  }
  return output;
}

void print_record(const std::string& text, char delimiter) {
  safePrint(text);
  safePrint(std::string_view(&delimiter, 1));
}

auto run(const Config& cfg) -> int {
  const char record_delim = cfg.zero_terminated ? '\0' : '\n';
  const std::string& file1 = cfg.files[0];
  const std::string& file2 = cfg.files[1];

  // [GNU] join.c: "both files cannot be standard input".
  if (file1 == "-" && file2 == "-") {
    safeErrorPrintLn(
        winux::i18n::translate("command.join.error.both_stdin",
                               "join: both files cannot be standard input"));
    return 1;
  }

  auto lines1_result = read_lines(file1, record_delim);
  if (!lines1_result) {
    cp::report_error(lines1_result, L"join");
    return 1;
  }

  auto lines2_result = read_lines(file2, record_delim);
  if (!lines2_result) {
    cp::report_error(lines2_result, L"join");
    return 1;
  }

  const auto& lines1 = *lines1_result;
  const auto& lines2 = *lines2_result;

  std::vector<ParsedLine> parsed1;
  std::vector<ParsedLine> parsed2;
  parsed1.reserve(lines1.size());
  parsed2.reserve(lines2.size());
  for (const auto& line : lines1) {
    parsed1.push_back(parse_line(line, cfg, cfg.field1, cfg.key_char1));
  }
  for (const auto& line : lines2) {
    parsed2.push_back(parse_line(line, cfg, cfg.field2, cfg.key_char2));
  }

  const size_t data_start1 = cfg.header && !parsed1.empty() ? 1 : 0;
  const size_t data_start2 = cfg.header && !parsed2.empty() ? 1 : 0;

  auto output_fields = cfg.output_auto
                           ? build_auto_output_fields(parsed1, parsed2, cfg)
                           : cfg.output_fields;

  // [GNU] join.c: the header pair is printed when either file has a first
  // line; the missing side contributes nothing.
  if (cfg.header && (!parsed1.empty() || !parsed2.empty())) {
    const ParsedLine* h1 = parsed1.empty() ? nullptr : &parsed1.front();
    const ParsedLine* h2 = parsed2.empty() ? nullptr : &parsed2.front();
    print_record(build_output_line(h1, h2, cfg, output_fields), record_delim);
  }

  // [GNU] join.c: sequential merge.  Each input line is order-checked when
  // the merge cursor first reaches it; a line only joins with a block of
  // equal keys the cursor is positioned on, so out-of-order records can be
  // skipped as unpairable instead of being joined.
  OrderState order;
  order.next_check[0] = data_start1;
  order.next_check[1] = data_start2;

  auto check1 = [&](size_t idx) -> bool {
    return check_line_order(order, 1, idx, data_start1, parsed1, lines1, file1,
                            cfg.check_order, cfg.nocheck_order);
  };
  auto check2 = [&](size_t idx) -> bool {
    return check_line_order(order, 2, idx, data_start2, parsed2, lines2, file2,
                            cfg.check_order, cfg.nocheck_order);
  };

  const size_t n1 = parsed1.size();
  const size_t n2 = parsed2.size();
  size_t i1 = data_start1;
  size_t i2 = data_start2;

  while (i1 < n1 && i2 < n2) {
    if (!check1(i1) || !check2(i2)) return 1;

    const int diff = parsed1[i1].key.compare(parsed2[i2].key);
    if (diff < 0) {
      if (cfg.print_unpaired1) {
        print_record(
            build_output_line(&parsed1[i1], nullptr, cfg, output_fields),
            record_delim);
      }
      // [GNU] join.c: the next line is read (and order-checked) before
      // seen_unpairable is set, so a disorder immediately following the
      // first unpairable line is not yet reported.
      ++i1;
      if (i1 < n1 && !check1(i1)) return 1;
      order.seen_unpairable = true;
      continue;
    }
    if (diff > 0) {
      if (cfg.print_unpaired2) {
        print_record(
            build_output_line(nullptr, &parsed2[i2], cfg, output_fields),
            record_delim);
      }
      ++i2;
      if (i2 < n2 && !check2(i2)) return 1;
      order.seen_unpairable = true;
      continue;
    }

    // Collect the block of equal keys from each file.
    const auto& key = parsed1[i1].key;
    size_t e1 = i1 + 1;
    while (e1 < n1) {
      if (!check1(e1)) return 1;
      if (parsed1[e1].key != key) break;
      ++e1;
    }
    size_t e2 = i2 + 1;
    while (e2 < n2) {
      if (!check2(e2)) return 1;
      if (parsed2[e2].key != key) break;
      ++e2;
    }

    if (!cfg.only_unpaired) {
      for (size_t a = i1; a < e1; ++a) {
        for (size_t b = i2; b < e2; ++b) {
          print_record(
              build_output_line(&parsed1[a], &parsed2[b], cfg, output_fields),
              record_delim);
        }
      }
    }
    i1 = e1;
    i2 = e2;
  }

  // [GNU] Drain the tail of whichever file still has records: print
  // unpairable lines when requested and keep checking input order.
  // Tail lines do not set seen_unpairable (GNU quirk).
  if ((cfg.print_unpaired1 || !cfg.nocheck_order) && i1 < n1) {
    if (cfg.print_unpaired1) {
      print_record(build_output_line(&parsed1[i1], nullptr, cfg, output_fields),
                   record_delim);
    }
    ++i1;
    for (; i1 < n1; ++i1) {
      if (!check1(i1)) return 1;
      if (cfg.print_unpaired1) {
        print_record(
            build_output_line(&parsed1[i1], nullptr, cfg, output_fields),
            record_delim);
      }
      if (order.issued[0] && !cfg.print_unpaired1) break;
    }
  }

  if ((cfg.print_unpaired2 || !cfg.nocheck_order) && i2 < n2) {
    if (cfg.print_unpaired2) {
      print_record(build_output_line(nullptr, &parsed2[i2], cfg, output_fields),
                   record_delim);
    }
    ++i2;
    for (; i2 < n2; ++i2) {
      if (!check2(i2)) return 1;
      if (cfg.print_unpaired2) {
        print_record(
            build_output_line(nullptr, &parsed2[i2], cfg, output_fields),
            record_delim);
      }
      if (order.issued[1] && !cfg.print_unpaired2) break;
    }
  }

  if (order.disorder) {
    // [GNU] "join: input is not in sorted order"
    safeErrorPrintLn(winux::i18n::translate(
        "command.join.error.not_sorted", "join: input is not in sorted order"));
    return 1;
  }
  return 0;
}

}  // namespace join_pipeline

REGISTER_COMMAND(
    join, "join", "join [OPTION]... FILE1 FILE2",
    "For each pair of input lines with identical join fields, write a line to\n"
    "standard output. The default join field is the first, delimited by "
    "blanks.\n"
    "\n"
    "Supports GNU-compatible blank-delimited fields, custom output lists,\n"
    "unpairable-line output, and case-insensitive joins.",
    "  join file1 file2\n"
    "  join -j 1 file1 file2        # join on first field\n"
    "  join -t ',' file1 file2     # use comma as separator\n"
    "  join -1 2 -2 1 file1 file2  # join on field 2 of file1 and field 1 of "
    "file2",
    "comm(1), paste(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    JOIN_OPTIONS) {
  using namespace join_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error().starts_with("missing operand")) {
      // [GNU] "join: missing operand" / "join: missing operand after 'FILE'"
      safeErrorPrint("join: ");
      safeErrorPrintLn(winux::i18n::translate_error(cfg_result.error()));
      safeErrorPrintLn(winux::i18n::format(
          "common.try_help", "Try '{} --help' for more information.", "join"));
      return 1;
    }
    if (cfg_result.error().starts_with("extra operand '")) {
      safeErrorPrint("join: ");
      safeErrorPrintLn(winux::i18n::translate_error(cfg_result.error()));
      safeErrorPrintLn(winux::i18n::format(
          "common.try_help", "Try '{} --help' for more information.", "join"));
      return 1;
    }
    cp::report_error(cfg_result, L"join");
    return 1;
  }

  return run(*cfg_result);
}
