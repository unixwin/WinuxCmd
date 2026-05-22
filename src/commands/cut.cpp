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
    OPTION("-b", "--bytes", "select only these bytes", STRING_TYPE),
    OPTION("-c", "--characters", "select only these characters", STRING_TYPE),
    OPTION("-d", "--delimiter", "use DELIM instead of TAB for field delimiter",
           STRING_TYPE),
    OPTION("-f", "--fields", "select only these fields", STRING_TYPE),
    OPTION("", "--complement",
           "complement the set of selected bytes, characters or fields"),
    OPTION("-n", "--no-partial",
           "do not split multibyte characters in byte mode"),
    OPTION("-s", "--only-delimited",
           "do not print lines not containing delimiter"),
    OPTION("-O", "--output-delimiter", "use STRING as the output delimiter",
           STRING_TYPE),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline")};

namespace cut_pipeline {
namespace cp = core::pipeline;

struct Range {
  int start;
  int end;  // inclusive, INT_MAX means open-ended
};

enum class Mode { bytes, characters, fields };

struct Config {
  Mode mode = Mode::fields;
  char delimiter = '\t';
  std::string output_delimiter;
  bool output_delimiter_set = false;
  bool complement = false;
  bool no_partial = false;
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

auto utf8_sequence_length(unsigned char byte) -> size_t {
  if ((byte & 0b1000'0000) == 0) return 1;
  if ((byte & 0b1110'0000) == 0b1100'0000) return 2;
  if ((byte & 0b1111'0000) == 0b1110'0000) return 3;
  if ((byte & 0b1111'1000) == 0b1111'0000) return 4;
  return 1;
}

auto utf8_codepoint_end(std::string_view line, size_t start) -> size_t {
  size_t len = utf8_sequence_length(static_cast<unsigned char>(line[start]));
  if (start + len > line.size()) return start + 1;

  for (size_t i = 1; i < len; ++i) {
    auto byte = static_cast<unsigned char>(line[start + i]);
    if ((byte & 0b1100'0000) != 0b1000'0000) return start + 1;
  }
  return start + len;
}

auto is_whole_span_selected(size_t first_byte, size_t last_byte,
                            const std::vector<Range>& ranges) -> bool {
  for (const auto& r : ranges) {
    if (first_byte >= static_cast<size_t>(r.start) &&
        last_byte <= static_cast<size_t>(r.end)) {
      return true;
    }
  }
  return false;
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

auto build_config(const CommandContext<CUT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  if (ctx.has("--delimiter")) {
    std::string delim = ctx.get<std::string>("--delimiter", "");
    if (delim.size() > 1) return std::unexpected("delimiter must be one char");
    cfg.delimiter = delim.empty() ? '\0' : delim[0];
  }
  cfg.output_delimiter.clear();
  std::string out_delim = ctx.get<std::string>("--output-delimiter", "");
  cfg.output_delimiter_set = ctx.has("--output-delimiter");
  if (cfg.output_delimiter_set) cfg.output_delimiter = out_delim;

  cfg.only_delimited =
      ctx.get<bool>("--only-delimited", false) || ctx.get<bool>("-s", false);
  cfg.complement = ctx.get<bool>("--complement", false);
  cfg.no_partial =
      ctx.get<bool>("--no-partial", false) || ctx.get<bool>("-n", false);
  cfg.zero_terminated =
      ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false);

  std::string bytes = ctx.get<std::string>("--bytes", "");
  if (bytes.empty()) bytes = ctx.get<std::string>("-b", "");
  std::string characters = ctx.get<std::string>("--characters", "");
  if (characters.empty()) characters = ctx.get<std::string>("-c", "");
  std::string fields = ctx.get<std::string>("--fields", "");
  if (fields.empty()) fields = ctx.get<std::string>("-f", "");

  int mode_count = 0;
  if (!bytes.empty()) ++mode_count;
  if (!characters.empty()) ++mode_count;
  if (!fields.empty()) ++mode_count;
  if (mode_count == 0) return std::unexpected("missing list");
  if (mode_count > 1) {
    return std::unexpected("only one of -b, -c or -f may be specified");
  }

  std::string_view list = fields;
  if (!bytes.empty()) {
    cfg.mode = Mode::bytes;
    list = bytes;
  } else if (!characters.empty()) {
    cfg.mode = Mode::characters;
    list = characters;
  } else {
    cfg.mode = Mode::fields;
  }
  if (cfg.mode != Mode::fields &&
      (ctx.has("--delimiter") || cfg.only_delimited)) {
    return std::unexpected(
        "-d and -s may be specified only when operating on fields");
  }
  if (cfg.no_partial && cfg.mode != Mode::bytes) {
    return std::unexpected("-n may be specified only when operating on bytes");
  }

  auto ranges = parse_fields(list);
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

auto cut_fields(std::string_view line, const Config& cfg) -> std::string {
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

  if (!has_delim) {
    if (cfg.only_delimited) return {};
    return std::string(line);
  }

  std::string out;
  bool first = true;
  std::string_view delimiter = cfg.output_delimiter_set
                                   ? std::string_view(cfg.output_delimiter)
                                   : std::string_view(&cfg.delimiter, 1);
  for (int idx = 1; idx <= static_cast<int>(fields.size()); ++idx) {
    bool selected = is_selected(idx, cfg.ranges);
    if (cfg.complement) selected = !selected;
    if (selected) {
      if (!first) out.append(delimiter);
      out.append(fields[idx - 1]);
      first = false;
    }
  }
  return out;
}

auto cut_bytes_or_characters(std::string_view line, const Config& cfg)
    -> std::string {
  std::string out;
  bool previous_selected = false;
  bool wrote_any = false;

  for (size_t offset = 0; offset < line.size();) {
    size_t end = cfg.no_partial ? utf8_codepoint_end(line, offset) : offset + 1;
    size_t first_byte = offset + 1;
    size_t last_byte = end;
    bool selected =
        cfg.no_partial
            ? is_whole_span_selected(first_byte, last_byte, cfg.ranges)
            : is_selected(static_cast<int>(first_byte), cfg.ranges);
    if (cfg.complement) selected = !selected;
    if (!selected) {
      previous_selected = false;
      offset = end;
      continue;
    }

    if (wrote_any && !previous_selected && cfg.output_delimiter_set) {
      out.append(cfg.output_delimiter);
    }
    out.append(line.substr(offset, end - offset));
    wrote_any = true;
    previous_selected = true;
    offset = end;
  }

  return out;
}

auto cut_line(std::string_view line, const Config& cfg) -> std::string {
  if (cfg.mode == Mode::fields) return cut_fields(line, cfg);
  return cut_bytes_or_characters(line, cfg);
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

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"cut");
    return 1;
  }

  return run(*cfg);
}
