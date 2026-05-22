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
 *  - File: join.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
    OPTION("-1", "", "join on this FIELD of file 1", STRING_TYPE),
    OPTION("-2", "", "join on this FIELD of file 2", STRING_TYPE),
    OPTION("-t", "", "use CHAR as input and output field separator",
           STRING_TYPE),
    OPTION("-e", "--empty", "replace missing input fields with EMPTY",
           STRING_TYPE),
    OPTION("-o", "--output", "use specified output format", STRING_TYPE),
    OPTION("-j", "", "equivalent to -1 FIELD -2 FIELD", STRING_TYPE),
    OPTION("-a", "", "print unpairable lines from file 1 or 2", STRING_TYPE),
    OPTION("-v", "", "print only unpairable lines from file 1 or 2",
           STRING_TYPE),
    OPTION("-i", "--ignore-case", "ignore differences in case", BOOL_TYPE),
    OPTION("", "--check-order", "check that the input is correctly sorted",
           BOOL_TYPE),
    OPTION("", "--nocheck-order",
           "do not check that the input is correctly sorted", BOOL_TYPE),
    OPTION("", "--header", "treat the first line in each file as headers",
           BOOL_TYPE),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline",
           BOOL_TYPE)};

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
  bool explicit_separator = false;
  bool whole_line = false;
  bool ignore_case = false;
  bool check_order = false;
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

auto parse_positive_field(const std::string& text, std::string_view option)
    -> cp::Result<int> {
  try {
    size_t pos = 0;
    int value = std::stoi(text, &pos);
    if (pos != text.size() || value <= 0) {
      return std::unexpected("invalid field number for " + std::string(option));
    }
    return value;
  } catch (...) {
    return std::unexpected("invalid field number for " + std::string(option));
  }
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

auto parse_line(const std::string& line, const Config& cfg, int field_num)
    -> ParsedLine {
  ParsedLine parsed;
  parsed.fields = split_fields(line, cfg);
  if (field_num <= static_cast<int>(parsed.fields.size())) {
    parsed.key = parsed.fields[static_cast<size_t>(field_num - 1)];
  }
  parsed.key = normalize_key(parsed.key, cfg.ignore_case);

  return parsed;
}

auto output_separator(const Config& cfg) -> std::string {
  if (!cfg.explicit_separator) return " ";
  return std::string(1, cfg.separator);
}

auto parse_file_number(const std::string& value, std::string_view option)
    -> cp::Result<int> {
  if (value == "1") return 1;
  if (value == "2") return 2;
  return std::unexpected("invalid file number for " + std::string(option));
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

    auto field = parse_positive_field(field_part, "-o");
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

  auto field1_opt = ctx.get<std::string>("-1", "");
  if (!field1_opt.empty()) {
    auto field = parse_positive_field(field1_opt, "-1");
    if (!field) return std::unexpected(field.error());
    cfg.field1 = *field;
  }

  auto field2_opt = ctx.get<std::string>("-2", "");
  if (!field2_opt.empty()) {
    auto field = parse_positive_field(field2_opt, "-2");
    if (!field) return std::unexpected(field.error());
    cfg.field2 = *field;
  }

  auto j_opt = ctx.get<std::string>("-j", "");
  if (!j_opt.empty()) {
    auto field = parse_positive_field(j_opt, "-j");
    if (!field) return std::unexpected(field.error());
    cfg.field1 = *field;
    cfg.field2 = cfg.field1;
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

  if (cfg.files.size() < 2) {
    return std::unexpected("missing operand after '" +
                           (cfg.files.empty() ? std::string() : cfg.files[0]) +
                           "'");
  }
  if (cfg.files.size() > 2) {
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
        lines.emplace_back(content.substr(start, i - start));
        start = i + 1;
      }
    }
    if (start < content.size()) {
      lines.emplace_back(content.substr(start));
    }
  } else {
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        std::string line = content.substr(start, i - start);
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
      std::string line = content.substr(start);
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

auto check_sorted_keys(const std::vector<ParsedLine>& lines, size_t start)
    -> cp::Result<void> {
  for (size_t i = start + 1; i < lines.size(); ++i) {
    if (lines[i - 1].key > lines[i].key) {
      return std::unexpected("input is not in sorted order");
    }
  }
  return {};
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
    parsed1.push_back(parse_line(line, cfg, cfg.field1));
  }
  for (const auto& line : lines2) {
    parsed2.push_back(parse_line(line, cfg, cfg.field2));
  }

  const size_t data_start1 = cfg.header && !parsed1.empty() ? 1 : 0;
  const size_t data_start2 = cfg.header && !parsed2.empty() ? 1 : 0;

  if (cfg.check_order) {
    auto sorted1 = check_sorted_keys(parsed1, data_start1);
    if (!sorted1) {
      cp::report_error(sorted1, L"join");
      return 1;
    }
    auto sorted2 = check_sorted_keys(parsed2, data_start2);
    if (!sorted2) {
      cp::report_error(sorted2, L"join");
      return 1;
    }
  }

  auto output_fields = cfg.output_auto
                           ? build_auto_output_fields(parsed1, parsed2, cfg)
                           : cfg.output_fields;

  if (cfg.header && !parsed1.empty() && !parsed2.empty()) {
    print_record(build_output_line(&parsed1.front(), &parsed2.front(), cfg,
                                   output_fields),
                 record_delim);
  }

  // Build index for file2
  std::unordered_map<std::string, SmallVector<size_t, 16>> file2_index;
  for (size_t i = data_start2; i < parsed2.size(); ++i) {
    file2_index[parsed2[i].key].push_back(i);
  }

  std::vector<bool> matched2(parsed2.size(), false);

  for (size_t i = data_start1; i < parsed1.size(); ++i) {
    const auto& line1 = parsed1[i];
    auto it = file2_index.find(line1.key);
    if (it != file2_index.end()) {
      for (size_t idx : it->second) {
        matched2[idx] = true;
        if (!cfg.only_unpaired) {
          print_record(
              build_output_line(&line1, &parsed2[idx], cfg, output_fields),
              record_delim);
        }
      }
    } else if (cfg.print_unpaired1) {
      print_record(build_output_line(&line1, nullptr, cfg, output_fields),
                   record_delim);
    }
  }

  if (cfg.print_unpaired2) {
    for (size_t i = data_start2; i < parsed2.size(); ++i) {
      if (!matched2[i]) {
        print_record(
            build_output_line(nullptr, &parsed2[i], cfg, output_fields),
            record_delim);
      }
    }
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
    cp::report_error(cfg_result, L"join");
    return 1;
  }

  return run(*cfg_result);
}
