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
    // [GNU] -e has no long form in GNU join; "--empty" is invented (#1411).
    OPTION("-e", "", "replace missing input fields with EMPTY", STRING_TYPE),
    // [GNU]
    // [GNU] -o has no long form in GNU join; "--output" is invented (#1355).
    OPTION("-o", "", "use specified output format", STRING_TYPE),
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

// [GNU] join is byte-oriented: records reach key splitting, comparison
// and output exactly as read — a trailing '\r' is ordinary content, and
// a leading UTF-8 BOM is not skipped either (issue #1354).

auto join_input_open_error(std::string_view filename) -> std::string {
  std::error_code ec;
  auto status = std::filesystem::status(
      std::filesystem::u8path(native_path::normalize_api_operand(filename)),
      ec);
  if (!ec && status.type() == std::filesystem::file_type::directory) {
    // [GNU] fopen on a directory succeeds; the failure surfaces at
    // read time as "join: read error: Is a directory" (join.c:497)
    // (audit #1462).
    return "read error: Is a directory";
  }
  return std::string(filename) + ": No such file or directory";
}

auto output_separator(const Config& cfg) -> std::string {
  // [GNU] with -t '' the tab character IS '\n', and it is also the
  // output separator (join.c:602, 668) (audit #1462).
  if (cfg.whole_line) return "\n";
  if (!cfg.explicit_separator) return " ";
  return std::string(1, cfg.separator);
}

auto parse_file_number(const std::string& value, std::string_view option)
    -> cp::Result<int> {
  // [GNU] the argument goes through xstrtol and only the VALUE is
  // checked against 1/2 (join.c:1040-1055), so '01' is accepted;
  // the old code compared the exact strings "1"/"2" (audit #1462).
  int parsed = 0;
  auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (ec == std::errc() && ptr == value.data() + value.size() &&
      (parsed == 1 || parsed == 2)) {
    return parsed;
  }
  return std::unexpected(
      winux::i18n::format("common.error.invalid_field_number",
                          "invalid field number: '{}'", value));
}

auto parse_output_format(const std::string& format)
    -> cp::Result<std::vector<OutputField>> {
  // [GNU] add_field_list/decode_field_spec (join.c:850-914): items
  // are split on comma/blank/tab and EVERY item is decoded --
  // empty items (from '-o \"\"', a trailing separator, or doubled
  // separators) are fatal, with the offending item quoted. The
  // old parser silently skipped empty tokens and used generic
  // messages (audit #1462).
  std::vector<OutputField> fields;
  size_t start = 0;
  while (true) {
    const size_t pos = format.find_first_of(", \t", start);
    const std::string token =
        format.substr(start, pos == std::string::npos ? pos : pos - start);
    if (token.empty() || (token[0] != '0' && token[0] != '1' &&
                          token[0] != '2')) {
      return std::unexpected("invalid file number in field spec: '" + token +
                             "'");
    }
    if (token[0] == '0') {
      if (token.size() != 1) {
        return std::unexpected("invalid field specifier: '" + token + "'");
      }
      fields.push_back(OutputField{0, 0});
    } else {
      if (token.size() < 2 || token[1] != '.') {
        return std::unexpected("invalid field specifier: '" + token + "'");
      }
      auto field = parse_positive_field(token.substr(2));
      if (!field) {
        return std::unexpected(field.error());
      }
      fields.push_back(OutputField{token[0] - '0', *field});
    }
    if (pos == std::string::npos) break;
    start = pos + 1;
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

  // [GNU] -1/-2/-j are applied as they are ENCOUNTERED (getopt
  // order), and set_join_field reports the already-set value
  // first (join.c:917-924, 1064-1095): 'join -j 3 -1 2' reports
  // "incompatible join fields 2, 1". The old code always applied
  // -1, then -2, then -j (audit #1462).
  auto apply_join_field_option = [&](const std::string& which,
                                     const std::string& value)
      -> cp::Result<void> {
    auto field = parse_positive_field(value);
    if (!field) return std::unexpected(field.error());
    if (which == "-1" || which == "-j") {
      if (auto r = set_field(1, *field); !r) {
        return std::unexpected(r.error());
      }
    }
    if (which == "-2" || which == "-j") {
      if (auto r = set_field(2, *field); !r) {
        return std::unexpected(r.error());
      }
    }
    return {};
  };
  bool saw_join_field_option = false;
  if (ctx.metas) {
    for (const auto& occurrence : ctx.options.occurrences()) {
      if (occurrence.index >= ctx.metas->size()) continue;
      const auto& meta = (*ctx.metas)[occurrence.index];
      if (meta.short_name != "-1" && meta.short_name != "-2" &&
          meta.short_name != "-j") {
        continue;
      }
      saw_join_field_option = true;
      const auto* value = std::get_if<std::string>(&occurrence.value);
      auto r = apply_join_field_option(meta.short_name,
                                       value ? *value : "");
      if (!r) return std::unexpected(r.error());
    }
  }
  if (!saw_join_field_option) {
    for (const char* name : {"-1", "-2", "-j"}) {
      auto opt = ctx.get<std::string>(name, "");
      if (ctx.has(name)) {
        auto r = apply_join_field_option(name, opt);
        if (!r) return std::unexpected(r.error());
      }
    }
  }

  // [GNU] -t (join.c case 't'): the tab is a single character;
  // '' means '\n' (whole line) and the two-character spelling
  // "\\0" means NUL. A different second -t is "incompatible
  // tabs"; a longer value is "multi-character tab '<arg>'".
  {
    bool tab_seen = false;
    char tab_char = 0;
    for (const auto& sep_opt : ctx.get_all<std::string>("-t")) {
      char newtab = 0;
      if (sep_opt.empty()) {
        newtab = '\n';
      } else if (sep_opt == "\\0") {
        newtab = '\0';
      } else if (sep_opt.size() == 1) {
        newtab = sep_opt[0];
      } else {
        return std::unexpected("multi-character tab '" + sep_opt + "'");
      }
      if (tab_seen && tab_char != newtab) {
        return std::unexpected("incompatible tabs");
      }
      tab_seen = true;
      tab_char = newtab;
    }
    if (tab_seen) {
      cfg.explicit_separator = true;
      cfg.separator = tab_char;
      cfg.whole_line = tab_char == '\n';
    }
  }

  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-i", false);
  // [GNU] --check-order/--nocheck-order set one state in argv
  // order; the LAST one wins (join.c:1125-1131). The old code let
  // --nocheck-order always win (audit #1462).
  if (ctx.metas) {
    for (const auto& occurrence : ctx.options.occurrences()) {
      if (occurrence.index >= ctx.metas->size()) continue;
      const auto& meta = (*ctx.metas)[occurrence.index];
      if (meta.long_name == "--check-order") {
        cfg.check_order = true;
        cfg.nocheck_order = false;
      } else if (meta.long_name == "--nocheck-order") {
        cfg.check_order = false;
        cfg.nocheck_order = true;
      }
    }
  } else {
    cfg.check_order = ctx.get<bool>("--check-order", false) &&
                      !ctx.get<bool>("--nocheck-order", false);
    cfg.nocheck_order = ctx.get<bool>("--nocheck-order", false);
  }
  cfg.header = ctx.get<bool>("--header", false);
  cfg.zero_terminated =
      ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false);

  // [GNU] a second, DIFFERENT -e value is fatal (join.c:1057-1062);
  // an empty first value still counts as set.
  {
    bool empty_field_seen = false;
    for (const auto& value : ctx.get_all<std::string>("-e")) {
      if (empty_field_seen && value != cfg.empty_field) {
        return std::unexpected(
            "conflicting empty-field replacement strings");
      }
      empty_field_seen = true;
      cfg.empty_field = value;
    }
  }

  // [GNU] every -o appends to the output list (add_field_list);
  // a real list takes precedence over 'auto' in prjoin
  // (join.c:1097-1105, 614-650). The old code kept only the last
  // -o value (audit #1462).
  {
    std::vector<std::string> real_lists;
    bool saw_auto = false;
    for (const auto& value : ctx.get_all<std::string>("-o")) {
      if (value == "auto") {
        saw_auto = true;
      } else {
        real_lists.push_back(value);
      }
    }
    if (!real_lists.empty()) {
      std::string joined;
      for (size_t i = 0; i < real_lists.size(); ++i) {
        if (i) joined += ',';
        joined += real_lists[i];
      }
      cfg.output_format = joined;
      auto fields = parse_output_format(joined);
      if (!fields) {
        return std::unexpected(fields.error());
      }
      cfg.output_fields = std::move(*fields);
    } else if (saw_auto) {
      cfg.output_auto = true;
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
    file_io::set_stdin_binary();
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
    auto f = file_io::open_binary_file(filename);
    if (!f) {
      return std::unexpected(join_input_open_error(filename));
    }

    std::string content((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());

    size_t start = 0;
    for (size_t i = 0; i < content.size(); ++i) {
      if (content[i] == delimiter) {
        std::string line = content.substr(start, i - start);
        lines.push_back(line);
        start = i + 1;
      }
    }
    if (start < content.size()) {
      std::string line = content.substr(start);
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
    parsed1.push_back(parse_line(line, cfg, cfg.field1));
  }
  for (const auto& line : lines2) {
    parsed2.push_back(parse_line(line, cfg, cfg.field2));
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
