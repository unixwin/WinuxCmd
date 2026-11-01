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
    // [GNU] An empty separator is accepted for the literal split,
    // where it means a single NUL byte (tac.c:540; see
    // split_literal_records). Only the regex form rejects it, with
    // this same diagnostic (#1444).
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
  // [GNU] An empty -s is a separator of length 1 whose byte is NUL:
  // match_length = *separator ? strlen (separator) : 1 (tac.c:540).
  // Verified against the 9.4 binary: 'a\0b\0c' with -s '' prints
  // 'cb\0a\0'. The earlier "never occurs" reading (#1444) holds
  // only for NUL-free input (audit #1462).
  std::string nul_separator(1, '\0');
  if (separator.empty()) separator = nul_separator;
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
    return std::unexpected("Invalid regular expression");
  }

  // [GNU #1447] Separator matches are discovered right to left, the
  // way tac.c's backward re_search finds them: at each step, the match
  // with the largest start in the not-yet-claimed prefix, longest at
  // that start, lying inside the prefix (match_at on the prefix view
  // reproduces the size-limited search, anchors included).  Records
  // are the segments between consecutive match ends -- or match
  // starts with -b.  A zero-length match is an ordinary separator
  // under this model: the next search starts strictly left of it, so
  // the walk always makes progress and a pattern that can match the
  // empty string (x*, .*, ...) is accepted, as in GNU.
  const auto &pattern = sep.pattern;
  std::vector<std::pair<size_t, size_t>> matches;  // rightmost first
  size_t cursor = content.size();
  while (cursor > 0) {
    const std::string_view prefix = content.substr(0, cursor);
    std::optional<portable_regex::Match> found;
    for (size_t start = cursor; start-- > 0;) {
      if (auto match = pattern.match_at(prefix, start)) {
        found = match;
        break;
      }
    }
    if (!found) break;
    matches.emplace_back(found->begin, found->end);
    cursor = found->begin;
  }

  if (matches.empty()) {
    if (!content.empty()) records.emplace_back(content);
    return records;
  }

  // Walk the boundaries left to right (matches are stored rightmost
  // first, and their ends -- resp. starts -- ascend in that walk).
  // Empty segments print as nothing, so they are skipped.
  size_t pos = 0;
  for (size_t i = matches.size(); i-- > 0;) {
    const size_t boundary = before ? matches[i].first : matches[i].second;
    if (boundary > pos) {
      records.emplace_back(content.substr(pos, boundary - pos));
    }
    pos = boundary;
  }
  if (pos < content.size()) {
    records.emplace_back(content.substr(pos));
  }
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
  // [GNU] the -r separator is compiled ONCE, before any file is
  // touched (tac.c:534): a bad pattern aborts with the regex
  // compiler's diagnostic even when the first operand is missing.
  // The old code compiled lazily per file and reported a generic
  // lowercase message per file (audit #1462). (For patterns other
  // than the common ones the regerror wording may still differ
  // from portable_regex's taxonomy; '[' gives GNU's exact
  // "Invalid regular expression".)
  if (cfg.regex) {
    auto compiled =
        portable_regex::compile(portable_regex::Syntax::Basic, cfg.separator);
    if (!compiled) {
      cp::Result<int> result = std::unexpected("Invalid regular expression");
      cp::report_error(result, L"tac");
      return 1;
    }
  }

  // [GNU] tac.c: an unreadable operand is diagnosed but does not stop the
  // remaining files; the exit status only turns nonzero once every operand
  // has been processed.
  bool any_error = false;
  for (const auto& file : cfg.files) {
    auto content = read_source(file);
    if (!content) {
      // [GNU] tac's own open wording is "failed to open %s for
      // reading" (tac.c:460); the shared reader produces head/tail's
      // "cannot open ..." form, so translate the prefix.
      std::string message = content.error();
      if (message.starts_with("cannot open ")) {
        message.replace(0, 12, "failed to open ");
        cp::Result<std::string> mapped = std::unexpected(message);
        cp::report_error(mapped, L"tac");
      } else {
        cp::report_error(content, L"tac");
      }
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
