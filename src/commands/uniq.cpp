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
 *  - File: uniq.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implemention for uniq.
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
 * - @a -D, @a --all-repeated: Print all duplicate lines [IMPLEMENTED]
 * - @a -f, @a --skip-fields: Avoid comparing the first N fields [IMPLEMENTED]
 * - @a -i, @a --ignore-case: Ignore differences in case [IMPLEMENTED]
 * - @a -s, @a --skip-chars: Avoid comparing the first N characters
 * [IMPLEMENTED]
 * - @a -u, @a --unique: Only print unique lines [IMPLEMENTED]
 * - @a -w, @a --check-chars: Compare no more than N characters [IMPLEMENTED]
 * - @a -z, @a --zero-terminated: Line delimiter is NUL, not newline
 * [IMPLEMENTED]
 * - @a --group: Show all items, separating groups [NOT SUPPORT]
 */
auto constexpr UNIQ_OPTIONS = std::array{
    OPTION("-c", "--count", "prefix lines by the number of occurrences"),
    OPTION("-d", "--repeated", "only print duplicate lines"),
    OPTION("-D", "--all-repeated", "print all duplicate lines"),
    OPTION("-f", "--skip-fields", "avoid comparing the first N fields",
           INT_TYPE),
    OPTION("-i", "--ignore-case", "ignore differences in case"),
    OPTION("-s", "--skip-chars", "avoid comparing the first N characters",
           INT_TYPE),
    OPTION("-u", "--unique", "only print unique lines"),
    OPTION("-w", "--check-chars", "compare no more than N characters",
           INT_TYPE),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline"),
    OPTION("", "--group", "show all items, separating groups [NOT SUPPORT]")};

namespace uniq_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool show_count = false;
  bool repeated_only = false;
  bool all_repeated = false;
  bool unique_only = false;
  bool ignore_case = false;
  int skip_fields = 0;
  int skip_chars = 0;
  int check_chars = -1;
  char delimiter = '\n';
  std::string input = "-";
  std::string output = "-";
};

auto read_all(std::istream& in) -> std::string { return read_text_stream(in); }

auto read_source(std::string_view path) -> cp::Result<std::string> {
  if (path == "-") return read_all(std::cin);

  std::ifstream in(std::string(path), std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + std::string(path) + "'");
  }
  return read_all(in);
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

auto to_lower_ascii(std::string_view s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
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
  if (cfg.ignore_case) return to_lower_ascii(key);
  return std::string(key);
}

auto is_unsupported_used(const CommandContext<UNIQ_OPTIONS.size()>& ctx)
    -> std::optional<std::string_view> {
  if (ctx.get<bool>("--group", false)) return "--group is [NOT SUPPORT]";
  return std::nullopt;
}

auto build_config(const CommandContext<UNIQ_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.show_count =
      ctx.get<bool>("--count", false) || ctx.get<bool>("-c", false);
  cfg.repeated_only =
      ctx.get<bool>("--repeated", false) || ctx.get<bool>("-d", false);
  cfg.all_repeated =
      ctx.get<bool>("--all-repeated", false) || ctx.get<bool>("-D", false);
  cfg.unique_only =
      ctx.get<bool>("--unique", false) || ctx.get<bool>("-u", false);
  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-i", false);

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

  if (ctx.positionals.size() > 2) {
    return std::unexpected("extra operand '" + std::string(ctx.positionals[2]) +
                           "'");
  }
  if (ctx.positionals.size() >= 1) cfg.input = std::string(ctx.positionals[0]);
  if (ctx.positionals.size() == 2) cfg.output = std::string(ctx.positionals[1]);

  return cfg;
}

auto should_emit(size_t count, const Config& cfg) -> bool {
  if (!cfg.repeated_only && !cfg.unique_only && !cfg.all_repeated) return true;
  if (cfg.repeated_only && count > 1) return true;
  if (cfg.all_repeated && count > 1) return true;
  if (cfg.unique_only && count == 1) return true;
  return false;
}

auto emit_one(std::ostream& out, std::string_view line, size_t count,
              const Config& cfg, bool show_count_override = false) -> void {
  if (cfg.show_count && !show_count_override) {
    out << std::setw(7) << count << " ";
  }
  out << line;
  out << cfg.delimiter;
}

auto run(const Config& cfg) -> int {
  auto content = read_source(cfg.input);
  if (!content) {
    cp::report_error(content, L"uniq");
    return 1;
  }

  auto records = split_records(*content, cfg.delimiter);
  std::ostream* out = &std::cout;
  std::ofstream file_out;
  if (cfg.output != "-") {
    file_out.open(cfg.output, std::ios::binary | std::ios::trunc);
    if (!file_out.is_open()) {
      cp::report_custom_error(L"uniq", L"cannot open output file");
      return 1;
    }
    out = &file_out;
  }

  if (records.empty()) return 0;

  size_t i = 0;
  while (i < records.size()) {
    size_t j = i + 1;
    const auto key = comparison_key(records[i], cfg);
    while (j < records.size() && comparison_key(records[j], cfg) == key) {
      ++j;
    }

    const size_t count = j - i;
    if (should_emit(count, cfg)) {
      if (cfg.all_repeated && count > 1) {
        for (size_t k = i; k < j; ++k) {
          emit_one(*out, records[k], count, cfg, true);
        }
      } else {
        emit_one(*out, records[i], count, cfg);
      }
    }
    i = j;
  }

  out->flush();
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
