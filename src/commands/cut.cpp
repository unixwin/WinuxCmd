/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implemention for cut.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr CUT_OPTIONS = std::array{
    OPTION("-b", "", "select only these bytes [NOT SUPPORT]", STRING_TYPE),
    OPTION("-c", "", "select only these characters [NOT SUPPORT]", STRING_TYPE),
    OPTION("-d", "--delimiter", "use DELIM instead of TAB for field delimiter",
           STRING_TYPE),
    OPTION("-f", "--fields", "select only these fields", STRING_TYPE),
    OPTION("-s", "--only-delimited",
           "do not print lines not containing delimiter"),
    OPTION("", "--output-delimiter",
           "use STRING as the output delimiter [NOT SUPPORT]", STRING_TYPE),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

namespace cut_pipeline {
namespace cp = core::pipeline;

struct Range {
  int start;
  int end;  // inclusive, INT_MAX means open-ended
};

struct Config {
  char delimiter = '\t';
  bool only_delimited = false;
  bool zero_terminated = false;
  std::vector<Range> ranges;
  std::vector<std::string> files;
};

auto parse_range_token(std::string_view tok) -> cp::Result<Range> {
  auto pos = tok.find('-');
  if (pos == std::string_view::npos) {
    int v = 0;
    auto [ptr, ec] = std::from_chars(tok.data(), tok.data() + tok.size(), v);
    if (ec != std::errc() || ptr != tok.data() + tok.size() || v <= 0) {
      return std::unexpected("invalid range");
    }
    return Range{v, v};
  }

  std::string_view left = tok.substr(0, pos);
  std::string_view right = tok.substr(pos + 1);

  int start = 1;
  int end = std::numeric_limits<int>::max();

  if (!left.empty()) {
    auto [ptr, ec] =
        std::from_chars(left.data(), left.data() + left.size(), start);
    if (ec != std::errc() || ptr != left.data() + left.size() || start <= 0) {
      return std::unexpected("invalid range");
    }
  }

  if (!right.empty()) {
    auto [ptr, ec] =
        std::from_chars(right.data(), right.data() + right.size(), end);
    if (ec != std::errc() || ptr != right.data() + right.size() || end <= 0) {
      return std::unexpected("invalid range");
    }
  }

  if (start > end) return std::unexpected("invalid range");
  return Range{start, end};
}

auto parse_fields(std::string_view list) -> cp::Result<std::vector<Range>> {
  if (list.empty()) return std::unexpected("missing fields list");

  std::vector<Range> ranges;
  size_t start = 0;
  while (start <= list.size()) {
    size_t pos = list.find(',', start);
    std::string_view tok = (pos == std::string_view::npos)
                               ? list.substr(start)
                               : list.substr(start, pos - start);
    auto r = parse_range_token(tok);
    if (!r) return std::unexpected(r.error());
    ranges.push_back(*r);
    if (pos == std::string_view::npos) break;
    start = pos + 1;
  }
  return ranges;
}

auto is_selected(int index, const std::vector<Range>& ranges) -> bool {
  for (const auto& r : ranges) {
    if (index >= r.start && index <= r.end) return true;
  }
  return false;
}

auto split_records(std::string_view content, char record_delim)
    -> std::vector<std::string> {
  std::vector<std::string> out;
  size_t start = 0;
  for (size_t i = 0; i < content.size(); ++i) {
    if (content[i] == record_delim) {
      out.emplace_back(content.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < content.size()) out.emplace_back(content.substr(start));
  return out;
}

auto read_source(std::string_view path) -> cp::Result<std::string> {
  if (path == "-") {
    return std::string(std::istreambuf_iterator<char>(std::cin),
                       std::istreambuf_iterator<char>());
  }
  std::ifstream in(std::string(path), std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + std::string(path) + "'");
  }
  return std::string(std::istreambuf_iterator<char>(in),
                     std::istreambuf_iterator<char>());
}

auto is_unsupported_used(const CommandContext<CUT_OPTIONS.size()>& ctx)
    -> std::optional<std::string_view> {
  if (!ctx.get<std::string>("-b", "").empty()) return "-b is [NOT SUPPORT]";
  if (!ctx.get<std::string>("-c", "").empty()) return "-c is [NOT SUPPORT]";
  if (!ctx.get<std::string>("--output-delimiter", "").empty())
    return "--output-delimiter is [NOT SUPPORT]";
  return std::nullopt;
}

auto build_config(const CommandContext<CUT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  std::string delim = ctx.get<std::string>("--delimiter", "");
  if (delim.empty()) delim = ctx.get<std::string>("-d", "");
  if (!delim.empty()) {
    if (delim.size() != 1) return std::unexpected("delimiter must be one char");
    cfg.delimiter = delim[0];
  }

  cfg.only_delimited =
      ctx.get<bool>("--only-delimited", false) || ctx.get<bool>("-s", false);
  cfg.zero_terminated =
      ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false);

  std::string fields = ctx.get<std::string>("--fields", "");
  if (fields.empty()) fields = ctx.get<std::string>("-f", "");
  auto ranges = parse_fields(fields);
  if (!ranges) return std::unexpected(ranges.error());
  cfg.ranges = *ranges;

  for (auto p : ctx.positionals) {
    std::string file_arg(p);
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
  if (cfg.files.empty()) cfg.files.emplace_back("-");

  return cfg;
}

auto cut_line(std::string_view line, const Config& cfg) -> std::string {
  std::vector<std::string_view> fields;
  size_t start = 0;
  bool has_delim = false;
  for (size_t i = 0; i < line.size(); ++i) {
    if (line[i] == cfg.delimiter) {
      has_delim = true;
      fields.emplace_back(line.substr(start, i - start));
      start = i + 1;
    }
  }
  fields.emplace_back(line.substr(start));

  if (!has_delim && cfg.only_delimited) return {};

  std::string out;
  bool first = true;
  for (int idx = 1; idx <= static_cast<int>(fields.size()); ++idx) {
    if (is_selected(idx, cfg.ranges)) {
      if (!first) out.push_back(cfg.delimiter);
      out.append(fields[idx - 1]);
      first = false;
    }
  }
  return out;
}

auto run_file(const std::string& path, const Config& cfg) -> int {
  auto content = read_source(path);
  if (!content) {
    cp::report_error(content, L"cut");
    return 1;
  }

  char record_delim = cfg.zero_terminated ? '\0' : '\n';

  std::vector<std::string> records;
  size_t start = 0;
  for (size_t i = 0; i < content->size(); ++i) {
    if ((*content)[i] == record_delim) {
      records.push_back(content->substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < content->size()) {
    records.push_back(content->substr(start));
  }

  for (const auto& rec : records) {
    auto out = cut_line(rec, cfg);
    if (out.empty() && cfg.only_delimited &&
        rec.find(cfg.delimiter) == std::string::npos) {
      continue;
    }
    safePrint(out);
    if (cfg.zero_terminated) {
      safePrint(char{'\0'});
    } else {
      safePrint("\n");
    }
  }
  return 0;
}

auto run(const Config& cfg) -> int {
  for (const auto& f : cfg.files) {
    int rc = run_file(f, cfg);
    if (rc != 0) return rc;
  }
  return 0;
}

}  // namespace cut_pipeline

REGISTER_COMMAND(cut, "cut", "cut OPTION... [FILE]...",
                 "Print selected parts of lines from each FILE to standard "
                 "output.",
                 "  cut -f1,3 file.txt\n"
                 "  cut -d, -f2 data.csv\n"
                 "  cut -z -f1 -d: list.txt",
                 "paste(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 CUT_OPTIONS) {
  using namespace cut_pipeline;

  if (auto unsupported = is_unsupported_used(ctx); unsupported.has_value()) {
    cp::report_custom_error(L"cut", utf8_to_wstring(*unsupported));
    return 2;
  }

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"cut");
    return 1;
  }

  return run(*cfg);
}
