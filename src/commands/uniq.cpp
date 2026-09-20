// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for uniq.
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

/**
 * @brief UNIQ command options definition
 *
 * This array defines all the options supported by the uniq command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -c, @a --count: Prefix lines by the number of occurrences [IMPLEMENTED]
 * - @a -d, @a --repeated: Only print duplicate lines [IMPLEMENTED]
 * - @a -D, @a --all-repeated[=METHOD]: Print all duplicate lines
 *
 * [IMPLEMENTED]
 * - @a -f, @a --skip-fields: Avoid comparing the first N
 * fields [IMPLEMENTED]
 * - @a -i, @a --ignore-case: Ignore differences in case [IMPLEMENTED]
 * - @a -s, @a --skip-chars: Avoid comparing the first N characters
 * [IMPLEMENTED]
 * - @a -u, @a --unique: Only print unique lines [IMPLEMENTED]
 * - @a -w, @a --check-chars: Compare no more than N characters [IMPLEMENTED]
 * - @a -z, @a --zero-terminated: Line delimiter is NUL, not newline
 * [IMPLEMENTED]
 * - @a --group[=METHOD]: Show all items, separating groups [IMPLEMENTED]
 */
auto constexpr UNIQ_OPTIONS = std::array{
    // [GNU] option
    OPTION("-c", "--count", "prefix lines by the number of occurrences"),
    // [GNU] option
    OPTION("-d", "--repeated", "only print duplicate lines"),
    // [GNU] option
    OPTION("-D", "--all-repeated", "print all duplicate lines",
           OPTIONAL_STRING_TYPE),
    // [GNU] option
    OPTION("-f", "--skip-fields", "avoid comparing the first N fields",
           INT_TYPE),
    // [GNU] option
    OPTION("-i", "--ignore-case", "ignore differences in case"),
    // [GNU] option
    OPTION("-s", "--skip-chars", "avoid comparing the first N characters",
           INT_TYPE),
    // [GNU] option
    OPTION("-u", "--unique", "only print unique lines"),
    // [GNU] option
    OPTION("-w", "--check-chars", "compare no more than N characters",
           INT_TYPE),
    // [GNU] option
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline"),
    // [GNU] option
    OPTION("", "--group", "show all items, separating groups",
           OPTIONAL_STRING_TYPE)};

namespace uniq_pipeline {
namespace cp = core::pipeline;

enum class GroupMode { none, separate, prepend, append, both };

struct Config {
  bool show_count = false;
  bool repeated_only = false;
  bool all_repeated = false;
  bool unique_only = false;
  bool ignore_case = false;
  bool group_all = false;
  bool output_unique = true;
  bool output_first_repeated = true;
  bool output_later_repeated = false;
  GroupMode group_mode = GroupMode::none;
  int skip_fields = 0;
  int skip_chars = 0;
  int check_chars = -1;
  char delimiter = '\n';
  std::string input = "-";
  std::string output = "-";
};

auto read_all(std::istream& in) -> std::string { return read_text_stream(in); }

auto read_source(std::string_view path) -> cp::Result<std::string> {
  if (path == "-") {
    // [GNU] closed stdin (<&-) is a read error, not an empty stream.
    if (file_io::stdin_is_bad()) {
      return std::unexpected("error reading '-': Bad file descriptor");
    }
    return read_all(std::cin);
  }

  auto content = file_io::read_all_file(path);
  if (!content) {
    const std::string prefix =
        "cannot open '" + std::string(path) + "' for reading: ";
    if (content.error().starts_with(prefix)) {
      return std::unexpected(std::string(path) + ": " +
                             content.error().substr(prefix.size()));
    }
    return std::unexpected(content.error());
  }
  return *content;
}

auto split_records(std::string_view content, char delimiter)
    -> std::vector<std::string> {
  SmallVector<std::string, 4096> out;
  size_t start = 0;
  for (size_t i = 0; i < content.size(); ++i) {
    if (content[i] == delimiter) {
      out.emplace_back(content.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < content.size()) {
    out.emplace_back(content.substr(start));
  }
  return std::vector<std::string>(out.begin(), out.end());
}

auto skip_n_fields(std::string_view line, int n) -> std::string_view {
  if (n <= 0) return line;

  size_t i = 0;
  int fields = 0;
  while (i < line.size() && fields < n) {
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i])) != 0) {
      ++i;
    }
    if (i >= line.size()) break;
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i])) == 0) {
      ++i;
    }
    ++fields;
  }
  return line.substr(i);
}

auto comparison_key(std::string_view line, const Config& cfg) -> std::string {
  auto key = skip_n_fields(line, cfg.skip_fields);
  size_t start =
      std::min<size_t>(key.size(), static_cast<size_t>(cfg.skip_chars));
  key = key.substr(start);
  if (cfg.check_chars >= 0) {
    key = key.substr(0, static_cast<size_t>(cfg.check_chars));
  }
  if (cfg.ignore_case) return ascii_lower_copy(key);
  return std::string(key);
}

auto is_unsupported_used(const CommandContext<UNIQ_OPTIONS.size()>& ctx)
    -> std::optional<std::string_view> {
  return std::nullopt;
}

auto parse_group_mode(std::string_view method, GroupMode default_mode)
    -> cp::Result<GroupMode> {
  if (method.empty()) return default_mode;
  if (method == "separate") return GroupMode::separate;
  if (method == "prepend") return GroupMode::prepend;
  if (method == "append") return GroupMode::append;
  if (method == "both") return GroupMode::both;
  return std::unexpected("invalid grouping method " + std::string(method));
}

auto parse_all_repeated_mode(std::string_view method) -> cp::Result<GroupMode> {
  if (method.empty() || method == "none") return GroupMode::none;
  if (method == "prepend") return GroupMode::prepend;
  if (method == "separate") return GroupMode::separate;
  return std::unexpected("invalid argument " + std::string(method) +
                         " for --all-repeated");
}

auto build_config(const CommandContext<UNIQ_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.show_count =
      ctx.get<bool>("--count", false) || ctx.get<bool>("-c", false);
  cfg.repeated_only =
      ctx.get<bool>("--repeated", false) || ctx.get<bool>("-d", false);
  cfg.all_repeated = ctx.has("--all-repeated") || ctx.has("-D");
  cfg.unique_only =
      ctx.get<bool>("--unique", false) || ctx.get<bool>("-u", false);
  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-i", false);

  if (cfg.repeated_only) cfg.output_unique = false;
  if (cfg.all_repeated) {
    cfg.output_unique = false;
    cfg.output_later_repeated = true;
  }
  if (cfg.unique_only) cfg.output_first_repeated = false;

  cfg.skip_fields = ctx.get<int>("--skip-fields", 0);
  if (cfg.skip_fields == 0) cfg.skip_fields = ctx.get<int>("-f", 0);

  cfg.skip_chars = ctx.get<int>("--skip-chars", 0);
  if (cfg.skip_chars == 0) cfg.skip_chars = ctx.get<int>("-s", 0);

  cfg.check_chars = ctx.get<int>("--check-chars", -1);
  if (cfg.check_chars < 0) cfg.check_chars = ctx.get<int>("-w", -1);

  cfg.delimiter =
      (ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false))
          ? '\0'
          : '\n';

  if (cfg.skip_fields < 0 || cfg.skip_chars < 0 || cfg.check_chars < -1) {
    return std::unexpected("negative counts are not allowed");
  }

  if (ctx.has("--group")) {
    auto group_mode = parse_group_mode(ctx.get<std::string>("--group", ""),
                                       GroupMode::separate);
    if (!group_mode) return std::unexpected(group_mode.error());
    cfg.group_all = true;
    cfg.group_mode = *group_mode;
  } else if (ctx.has("--all-repeated")) {
    auto group_mode =
        parse_all_repeated_mode(ctx.get<std::string>("--all-repeated", ""));
    if (!group_mode) return std::unexpected(group_mode.error());
    cfg.group_mode = *group_mode;
  }

  const bool output_option_used = cfg.show_count || cfg.repeated_only ||
                                  cfg.all_repeated || cfg.unique_only;
  if (cfg.group_all && output_option_used) {
    return std::unexpected("--group is mutually exclusive with -c/-d/-D/-u");
  }
  if (cfg.show_count && cfg.output_later_repeated) {
    return std::unexpected(
        "printing all duplicated lines and repeat counts is meaningless");
  }

  if (ctx.positionals.size() > 2) {
    return std::unexpected("extra operand '" + std::string(ctx.positionals[2]) +
                           "'");
  }
  if (ctx.positionals.size() >= 1) cfg.input = std::string(ctx.positionals[0]);
  if (ctx.positionals.size() == 2) cfg.output = std::string(ctx.positionals[1]);

  return cfg;
}

auto should_emit(size_t count, const Config& cfg) -> bool {
  if (cfg.group_all) return true;
  if (count == 1) return cfg.output_unique;
  return cfg.output_first_repeated || cfg.output_later_repeated;
}

auto emit_one(std::ostream& out, std::string_view line, size_t count,
              const Config& cfg, bool show_count_override = false) -> void {
  if (cfg.show_count && !show_count_override) {
    out << std::setw(7) << count << " ";
  }
  out << line;
  out << cfg.delimiter;
}

auto emit_group_separator(std::ostream& out, const Config& cfg) -> void {
  out << cfg.delimiter;
}

auto run(const Config& cfg) -> int {
  std::ifstream input_file;
  std::istream* input = &std::cin;
  if (cfg.input != "-") {
    auto input_operand = native_path::make_api_path_operand(cfg.input);
    if (input_operand.had_trailing_separator &&
        native_path::attributes_are_regular_file(
            native_path::operand_target_attributes_w(input_operand))) {
      cp::report_custom_error(L"uniq",
                              utf8_to_wstring(cfg.input + ": Not a directory"));
      return 1;
    }
    input_file = file_io::open_binary_file(cfg.input);
    if (!input_file.is_open()) {
      auto content = read_source(cfg.input);
      if (!content) {
        cp::report_error(content, L"uniq");
        return 1;
      }
      cp::report_custom_error(L"uniq", L"cannot open input");
      return 1;
    }
    input = &input_file;
  } else if (file_io::stdin_is_bad()) {
    // [GNU] closed stdin (<&-) errors "uniq: error reading '-'".
    cp::report_custom_error(L"uniq", L"error reading '-': Bad file descriptor");
    return 1;
  }

  std::ostream* out = &std::cout;
  std::ofstream file_out;
  int stdout_mode = -1;
  if (cfg.output != "-") {
    file_out = file_io::create_binary_file(cfg.output);
    if (!file_out.is_open()) {
      cp::report_custom_error(L"uniq", L"cannot open output file");
      return 1;
    }
    out = &file_out;
  } else {
    // Keep record bytes unchanged when stdout is a Windows pipe.
    stdout_mode = _setmode(_fileno(stdout), _O_BINARY);
  }

  bool first_emitted_group = false;
  std::vector<std::string> group;
  std::string key;
  auto emit_group = [&](const std::vector<std::string>& records) {
    const size_t count = records.size();
    if (should_emit(count, cfg)) {
      // [GNU] uniq.c: a separator precedes the group for prepend/both, and
      // (once a group has been printed) also for append/separate, so groups
      // are separated by exactly one blank line even under --group=both.
      if (cfg.group_mode == GroupMode::prepend ||
          cfg.group_mode == GroupMode::both ||
          (first_emitted_group && (cfg.group_mode == GroupMode::append ||
                                   cfg.group_mode == GroupMode::separate))) {
        emit_group_separator(*out, cfg);
      }

      if (cfg.group_all) {
        for (const auto& record : records) {
          emit_one(*out, record, count, cfg, true);
        }
      } else if (count == 1) {
        emit_one(*out, records.front(), count, cfg);
      } else {
        if (cfg.output_first_repeated) {
          emit_one(*out, records.front(), count, cfg);
        }
        if (cfg.output_later_repeated) {
          for (size_t k = 1; k < records.size(); ++k) {
            emit_one(*out, records[k], count, cfg, true);
          }
        }
      }

      first_emitted_group = true;
    }
  };

  std::string line;
  while (std::getline(*input, line, cfg.delimiter)) {
    auto line_key = comparison_key(line, cfg);
    if (!group.empty() && line_key != key) {
      emit_group(group);
      group.clear();
    }
    if (group.empty()) key = std::move(line_key);
    group.push_back(std::move(line));
    line.clear();
  }
  if (input->bad()) {
    cp::report_custom_error(L"uniq", L"error reading input");
    if (stdout_mode != -1) _setmode(_fileno(stdout), stdout_mode);
    return 1;
  }
  if (!group.empty()) emit_group(group);

  // [GNU] append/both end the output with a single trailing separator.
  if (first_emitted_group && (cfg.group_mode == GroupMode::append ||
                              cfg.group_mode == GroupMode::both)) {
    emit_group_separator(*out, cfg);
  }

  out->flush();
  if (stdout_mode != -1) _setmode(_fileno(stdout), stdout_mode);
  return 0;
}

}  // namespace uniq_pipeline

REGISTER_COMMAND(
    uniq, "uniq", "uniq [OPTION]... [INPUT [OUTPUT]]",
    "Filter adjacent matching lines from INPUT (or standard input),\n"
    "writing to OUTPUT (or standard output).",
    "  uniq data.txt\n"
    "  sort a.txt | uniq -c\n"
    "  uniq -i -d words.txt",
    "sort(1), grep(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", UNIQ_OPTIONS) {
  using namespace uniq_pipeline;

  if (auto unsupported = is_unsupported_used(ctx); unsupported.has_value()) {
    cp::report_custom_error(L"uniq", utf8_to_wstring(*unsupported));
    return 2;
  }

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"uniq");
    return 1;
  }

  return run(*cfg);
}
