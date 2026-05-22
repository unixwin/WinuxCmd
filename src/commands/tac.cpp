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
 *  - File: tac.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for tac.
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

auto constexpr TAC_OPTIONS = std::array{
    OPTION("-b", "--before",
           "attach the separator before instead of after each record"),
    OPTION("-r", "--regex", "treat the separator as a regular expression"),
    OPTION("-s", "--separator", "use STRING as the record separator",
           STRING_TYPE),
};

namespace tac_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool before = false;
  bool regex = false;
  std::string separator = "\n";
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<TAC_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.before = ctx.get<bool>("--before", false) || ctx.get<bool>("-b", false);
  cfg.regex = ctx.get<bool>("--regex", false) || ctx.get<bool>("-r", false);
  if (ctx.has("--separator") || ctx.has("-s")) {
    cfg.separator = ctx.get<std::string>("--separator", "");
    if (cfg.separator.empty()) cfg.separator = ctx.get<std::string>("-s", "");
    if (cfg.separator.empty()) cfg.separator.push_back('\0');
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

auto read_source(const std::string& file) -> cp::Result<std::string> {
  if (file == "-") {
    std::string content;
    content.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
    return content;
  }

  std::ifstream f(file, std::ios::binary);
  if (!f) {
    return std::unexpected(std::string("cannot open '") + file +
                           "' for reading");
  }
  std::string content;
  content.assign(std::istreambuf_iterator<char>(f),
                 std::istreambuf_iterator<char>());
  if (f.fail() && !f.eof()) return std::unexpected("error reading from file");
  return content;
}

auto split_literal_records(std::string_view content, std::string_view separator,
                           bool before) -> std::vector<std::string> {
  std::vector<std::string> records;
  size_t record_start = 0;
  size_t search_start = 0;

  while (search_start <= content.size()) {
    size_t pos = content.find(separator, search_start);
    if (pos == std::string_view::npos) break;

    if (before) {
      records.emplace_back(content.substr(record_start, pos - record_start));
      record_start = pos;
    } else {
      size_t record_end = pos + separator.size();
      records.emplace_back(
          content.substr(record_start, record_end - record_start));
      record_start = record_end;
    }
    search_start = pos + separator.size();
  }

  if (record_start < content.size()) {
    records.emplace_back(content.substr(record_start));
  } else if (before && record_start == content.size() && !content.empty()) {
    records.emplace_back();
  }
  return records;
}

auto split_regex_records(std::string_view content, const std::string& separator,
                         bool before) -> cp::Result<std::vector<std::string>> {
  std::vector<std::string> records;
  try {
    std::regex sep(separator);
    std::string text(content);
    size_t record_start = 0;
    for (auto it = std::sregex_iterator(text.begin(), text.end(), sep);
         it != std::sregex_iterator(); ++it) {
      size_t pos = static_cast<size_t>(it->position());
      size_t len = static_cast<size_t>(it->length());
      if (len == 0)
        return std::unexpected("separator regex matches empty string");
      if (before) {
        records.emplace_back(text.substr(record_start, pos - record_start));
        record_start = pos;
      } else {
        size_t record_end = pos + len;
        records.emplace_back(
            text.substr(record_start, record_end - record_start));
        record_start = record_end;
      }
    }
    if (record_start < text.size())
      records.emplace_back(text.substr(record_start));
    return records;
  } catch (const std::regex_error&) {
    return std::unexpected("invalid regular expression");
  }
}

auto output_reversed_records(const std::vector<std::string>& records) -> void {
  for (auto it = records.rbegin(); it != records.rend(); ++it) {
    safePrint(*it);
  }
}

auto run(const Config& cfg) -> int {
  for (const auto& file : cfg.files) {
    auto content = read_source(file);
    if (!content) {
      cp::report_error(content, L"tac");
      return 1;
    }

    cp::Result<std::vector<std::string>> records =
        cfg.regex ? split_regex_records(*content, cfg.separator, cfg.before)
                  : cp::Result<std::vector<std::string>>{split_literal_records(
                        *content, cfg.separator, cfg.before)};
    if (!records) {
      cp::report_error(records, L"tac");
      return 1;
    }
    output_reversed_records(*records);
  }

  return 0;
}

}  // namespace tac_pipeline

REGISTER_COMMAND(
    tac, "tac", "tac [OPTION]... [FILE]...",
    "Concatenate and print files in reverse.\n"
    "\n"
    "Write each FILE to standard output, last line first.\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Records are separated by newline by default. Use -s to choose another "
    "separator,\n"
    "-b to attach separators before records, and -r to treat the separator as "
    "a regex.\n",
    "  tac file.txt\n"
    "  echo -e 'line1\\nline2\\nline3' | tac\n"
    "  tac -s : file.txt",
    "cat(1), rev(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", TAC_OPTIONS) {
  using namespace tac_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"tac");
    return 1;
  }

  return run(*cfg_result);
}
