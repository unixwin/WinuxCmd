// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU] -b, --before
    OPTION("-b", "--before",
           "attach the separator before instead of after each record"),
    // [GNU] -r, --regex
    OPTION("-r", "--regex", "treat the separator as a regular expression"),
    // [GNU] -s, --separator
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
    // [GNU] An empty separator is accepted for the literal split: it
    // never occurs in the input, so the whole input is a single
    // record (verified against the pinned oracle, coreutils 9.4).
    // Only the regex form rejects it, with this same diagnostic
    // (#1444).
    if (cfg.regex && cfg.separator.empty())
      return std::unexpected("separator cannot be empty");
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
  return file_io::read_all_input(file);
}

auto normalize_newline_delimited_text(std::string_view content) -> std::string {
  std::string normalized;
  normalized.reserve(content.size());

  for (size_t i = 0; i < content.size(); ++i) {
    if (content[i] == '\r') {
      if (i + 1 == content.size()) {
        continue;
      }
      if (content[i + 1] == '\n') {
        continue;
      }
    }
    normalized.push_back(content[i]);
  }

  return normalized;
}

auto split_literal_records(std::string_view content, std::string_view separator,
                           bool before) -> std::vector<std::string> {
  std::vector<std::string> records;
  // [GNU] An empty separator never occurs in the input, so the whole
  // input is a single record (#1444). (find() of an empty needle
  // would match at every position and never advance.)
  if (separator.empty()) {
    if (!content.empty()) records.emplace_back(content);
    return records;
  }
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
  // [GNU] tac.c compiles the -r separator as a basic regular expression
  // (BRE): `\|` is alternation and a bare `|` is a literal character.
  // Extended (ERE) inverts both (issue #1361).
  auto sep =
      portable_regex::compile(portable_regex::Syntax::Basic, separator);
  if (!sep) {
    return std::unexpected("invalid regular expression");
  }

  size_t record_start = 0;
  for (const auto& match : sep.pattern.find_all(content)) {
    size_t pos = match.begin;
    size_t len = match.end - match.begin;
    if (len == 0)
      return std::unexpected("separator regex matches empty string");
    if (before) {
      records.emplace_back(content.substr(record_start, pos - record_start));
      record_start = pos;
    } else {
      size_t record_end = pos + len;
      records.emplace_back(
          content.substr(record_start, record_end - record_start));
      record_start = record_end;
    }
  }
  if (record_start < content.size())
    records.emplace_back(content.substr(record_start));
  return records;
}

auto output_reversed_records(const std::vector<std::string>& records) -> void {
  for (auto it = records.rbegin(); it != records.rend(); ++it) {
    safePrint(*it);
    // #1368: once stdout can no longer take writes, further records would
    // only pile into a dead sink; GNU tac stops at the first failed write.
    if (is_stdout_pipe_closed() || is_stdout_write_failed()) break;
  }
}

auto run(const Config& cfg) -> int {
  // [GNU] tac.c: an unreadable operand is diagnosed but does not stop the
  // remaining files; the exit status only turns nonzero once every operand
  // has been processed.
  bool any_error = false;
  for (const auto& file : cfg.files) {
    auto content = read_source(file);
    if (!content) {
      cp::report_error(content, L"tac");
      any_error = true;
      continue;
    }

    cp::Result<std::vector<std::string>> records =
        cfg.regex ? split_regex_records(*content, cfg.separator, cfg.before)
                  : cp::Result<std::vector<std::string>>{split_literal_records(
                        *content, cfg.separator, cfg.before)};
    if (!records) {
      cp::report_error(records, L"tac");
      any_error = true;
      continue;
    }
    output_reversed_records(*records);
    if (is_stdout_pipe_closed() || is_stdout_write_failed()) break;
  }

  // [GNU] tac.c full_write() failure reaches write_error() (system.h:960):
  // one "tac: write error: <strerror>" diagnostic, exit 1 (#1368). A closed
  // downstream pipe keeps the quiet SIGPIPE-style status.
  if (int write_error_rc = report_stdout_write_error("tac", 1)) {
    return write_error_rc;
  }
  return any_error ? 1 : 0;
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
