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
 *  - File: csplit.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for csplit.
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

auto constexpr CSPLIT_OPTIONS = std::array{
    OPTION("-f", "--prefix", "use PREFIX instead of 'xx'", STRING_TYPE),
    OPTION("-b", "--suffix-format", "use sprintf FORMAT instead of %02d",
           STRING_TYPE),
    OPTION("-n", "--digits", "use specified number of digits", STRING_TYPE),
    OPTION("-k", "--keep-files", "do not remove output files on errors",
           BOOL_TYPE),
    OPTION("-s", "--quiet", "do not print counts of output file sizes",
           BOOL_TYPE),
    OPTION("-q", "--silent", "do not print counts of output file sizes",
           BOOL_TYPE),
    OPTION("-z", "--elide-empty-files", "remove empty output files", BOOL_TYPE),
    OPTION("", "--suppress-matched",
           "suppress lines matching PATTERN from output", BOOL_TYPE)};

namespace csplit_pipeline {
namespace cp = core::pipeline;

auto resolve_input_file(const CommandContext<CSPLIT_OPTIONS.size()>& ctx)
    -> cp::Result<std::string> {
  if (ctx.positionals.empty()) {
    return std::unexpected("missing input file");
  }

  std::string file_arg = std::string(ctx.positionals[0]);
  if (contains_wildcard(file_arg)) {
    auto glob_result = glob_expand(file_arg);
    if (glob_result.expanded && !glob_result.files.empty()) {
      if (glob_result.files.size() != 1) {
        return std::unexpected("wildcard input must match exactly one file");
      }
      return wstring_to_utf8(glob_result.files[0]);
    }
  }

  return file_arg;
}

struct Config {
  std::string prefix = "xx";
  std::string suffix_format;
  bool suffix_format_specified = false;
  int digits = 2;
  bool keep_files = false;
  bool quiet = false;
  bool elide_empty = false;
  bool suppress_matched = false;
  std::string input_file;
  SmallVector<std::string, 64> patterns;
};

auto build_config(const CommandContext<CSPLIT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  auto prefix_opt = ctx.get<std::string>("--prefix", "");
  if (prefix_opt.empty()) {
    prefix_opt = ctx.get<std::string>("-f", "");
  }
  if (!prefix_opt.empty()) {
    cfg.prefix = prefix_opt;
  }

  auto suffix_opt = ctx.get<std::string>("--suffix-format", "");
  if (suffix_opt.empty()) {
    suffix_opt = ctx.get<std::string>("-b", "");
  }
  if (!suffix_opt.empty()) {
    cfg.suffix_format = suffix_opt;
    cfg.suffix_format_specified = true;
  }

  auto digits_opt = ctx.get<std::string>("--digits", "");
  if (digits_opt.empty()) {
    digits_opt = ctx.get<std::string>("-n", "");
  }
  if (!digits_opt.empty()) {
    try {
      cfg.digits = std::stoi(digits_opt);
      if (cfg.digits < 0 || cfg.digits > 32) {
        return std::unexpected("digits value must be between 0 and 32");
      }
    } catch (...) {
      return std::unexpected("invalid digits value");
    }
  }

  cfg.keep_files =
      ctx.get<bool>("--keep-files", false) || ctx.get<bool>("-k", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) || ctx.get<bool>("-s", false) ||
              ctx.get<bool>("--silent", false) || ctx.get<bool>("-q", false);
  cfg.elide_empty =
      ctx.get<bool>("--elide-empty-files", false) || ctx.get<bool>("-z", false);
  cfg.suppress_matched = ctx.get<bool>("--suppress-matched", false);

  auto input_result = resolve_input_file(ctx);
  if (!input_result) {
    return std::unexpected(input_result.error());
  }
  cfg.input_file = *input_result;

  for (size_t i = 1; i < ctx.positionals.size(); ++i) {
    cfg.patterns.push_back(std::string(ctx.positionals[i]));
  }

  if (cfg.input_file.empty()) {
    return std::unexpected("missing input file");
  }

  if (cfg.patterns.empty()) {
    return std::unexpected("missing pattern");
  }

  return cfg;
}

auto read_records(const std::string& filename)
    -> cp::Result<std::vector<std::string>> {
  std::string input;
  if (filename == "-") {
    input.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
    if (std::cin.fail() && !std::cin.eof()) {
      return std::unexpected("error reading from file");
    }
  } else {
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
    }
    input.assign(std::istreambuf_iterator<char>(f),
                 std::istreambuf_iterator<char>());
    if (f.fail() && !f.eof()) {
      return std::unexpected("error reading from file");
    }
  }

  std::vector<std::string> lines;
  for (size_t pos = 0; pos < input.size();) {
    size_t next = input.find('\n', pos);
    size_t end = next == std::string::npos ? input.size() : next + 1;
    lines.push_back(input.substr(pos, end - pos));
    pos = end;
  }
  return lines;
}

auto line_for_regex(std::string_view line) -> std::string {
  while (!line.empty() && (line.back() == '\n' || line.back() == '\r')) {
    line.remove_suffix(1);
  }
  return std::string(line);
}

struct ParsedPattern {
  enum class Kind { LineNumber, Regex };

  Kind kind = Kind::LineNumber;
  bool skip = false;
  size_t line_number = 0;
  std::string regex_text;
  int offset = 0;
};

struct RepeatSpec {
  bool has_repeat = false;
  bool until_exhausted = false;
  size_t count = 0;
};

auto parse_offset(std::string_view text) -> cp::Result<int> {
  if (text.empty()) return 0;
  if (text[0] != '+' && text[0] != '-') {
    return std::unexpected("invalid pattern offset");
  }
  try {
    size_t consumed = 0;
    int offset = std::stoi(std::string(text), &consumed);
    if (consumed != text.size()) {
      return std::unexpected("invalid pattern offset");
    }
    return offset;
  } catch (...) {
    return std::unexpected("invalid pattern offset");
  }
}

auto parse_pattern(const std::string& text) -> cp::Result<ParsedPattern> {
  if (text.empty()) return std::unexpected("empty pattern");

  if (text[0] == '/' || text[0] == '%') {
    char delimiter = text[0];
    bool escaped = false;
    size_t close = std::string::npos;
    for (size_t i = 1; i < text.size(); ++i) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (text[i] == '\\') {
        escaped = true;
        continue;
      }
      if (text[i] == delimiter) {
        close = i;
        break;
      }
    }
    if (close == std::string::npos) {
      return std::unexpected("missing pattern delimiter");
    }

    auto offset = parse_offset(std::string_view(text).substr(close + 1));
    if (!offset) return std::unexpected(offset.error());

    ParsedPattern pattern;
    pattern.kind = ParsedPattern::Kind::Regex;
    pattern.skip = delimiter == '%';
    pattern.regex_text = text.substr(1, close - 1);
    pattern.offset = *offset;
    return pattern;
  }

  try {
    size_t consumed = 0;
    size_t line_number = std::stoull(text, &consumed, 10);
    if (consumed != text.size() || line_number == 0) {
      return std::unexpected("invalid line number");
    }
    ParsedPattern pattern;
    pattern.kind = ParsedPattern::Kind::LineNumber;
    pattern.line_number = line_number;
    return pattern;
  } catch (...) {
    return std::unexpected("invalid pattern");
  }
}

auto parse_repeat(const std::string& text) -> cp::Result<RepeatSpec> {
  RepeatSpec repeat;
  if (text.size() < 3 || text.front() != '{' || text.back() != '}') {
    return repeat;
  }

  repeat.has_repeat = true;
  std::string inner = text.substr(1, text.size() - 2);
  if (inner == "*") {
    repeat.until_exhausted = true;
    return repeat;
  }

  try {
    size_t consumed = 0;
    repeat.count = std::stoull(inner, &consumed, 10);
    if (consumed != inner.size()) {
      return std::unexpected("invalid repeat count");
    }
    return repeat;
  } catch (...) {
    return std::unexpected("invalid repeat count");
  }
}

struct Segment {
  size_t begin = 0;
  size_t end = 0;
};

struct PatternApplication {
  bool found = false;
  bool skip = false;
  Segment output;
  size_t next_start = 0;
};

auto apply_pattern(const std::vector<std::string>& lines,
                   const ParsedPattern& pattern, size_t current, bool repeated,
                   bool suppress_matched) -> cp::Result<PatternApplication> {
  PatternApplication result;
  result.skip = pattern.skip;
  result.output.begin = current;

  if (pattern.kind == ParsedPattern::Kind::LineNumber) {
    size_t boundary =
        repeated ? current + pattern.line_number : pattern.line_number - 1;
    if (boundary > lines.size() || boundary < current) {
      return std::unexpected("line number out of range");
    }
    result.found = true;
    result.output.end = boundary;
    result.next_start = boundary;
    return result;
  }

  std::regex re;
  try {
    re = std::regex(pattern.regex_text);
  } catch (const std::regex_error&) {
    return std::unexpected("invalid regular expression");
  }
  for (size_t i = current; i < lines.size(); ++i) {
    if (!std::regex_search(line_for_regex(lines[i]), re)) continue;

    int64_t boundary_signed =
        static_cast<int64_t>(i) + static_cast<int64_t>(pattern.offset);
    if (boundary_signed < static_cast<int64_t>(current) ||
        boundary_signed > static_cast<int64_t>(lines.size())) {
      return std::unexpected("pattern offset out of range");
    }

    size_t boundary = static_cast<size_t>(boundary_signed);
    result.found = true;
    if (suppress_matched && !pattern.skip) {
      result.output.end = std::min(boundary, i);
      result.next_start = std::max(boundary, i + 1);
    } else {
      result.output.end = boundary;
      result.next_start = boundary;
    }
    return result;
  }

  return result;
}

auto line_byte_count(const std::vector<std::string>& lines, Segment segment)
    -> size_t {
  size_t bytes = 0;
  for (size_t i = segment.begin; i < segment.end; ++i) {
    bytes += lines[i].size();
  }
  return bytes;
}

auto make_filename(const Config& cfg, int file_number)
    -> cp::Result<std::string> {
  if (!cfg.suffix_format_specified) {
    char filename_buf[256];
    snprintf(filename_buf, sizeof(filename_buf), "%s%0*d", cfg.prefix.c_str(),
             cfg.digits, file_number);
    return std::string(filename_buf);
  }

  size_t conversions = 0;
  for (size_t i = 0; i < cfg.suffix_format.size(); ++i) {
    if (cfg.suffix_format[i] != '%') continue;
    if (i + 1 < cfg.suffix_format.size() && cfg.suffix_format[i + 1] == '%') {
      ++i;
      continue;
    }
    ++conversions;
    size_t j = i + 1;
    while (j < cfg.suffix_format.size() &&
           std::string_view("-+ #0123456789.").find(cfg.suffix_format[j]) !=
               std::string_view::npos) {
      ++j;
    }
    if (j >= cfg.suffix_format.size() ||
        std::string_view("diuoxX").find(cfg.suffix_format[j]) ==
            std::string_view::npos) {
      return std::unexpected("invalid suffix format");
    }
    i = j;
  }
  if (conversions != 1) {
    return std::unexpected("invalid suffix format");
  }

  char suffix_buf[256];
  int written = snprintf(suffix_buf, sizeof(suffix_buf),
                         cfg.suffix_format.c_str(), file_number);
  if (written < 0 || written >= static_cast<int>(sizeof(suffix_buf))) {
    return std::unexpected("invalid suffix format");
  }
  return cfg.prefix + std::string(suffix_buf);
}

auto write_outputs(const Config& cfg, const std::vector<std::string>& lines,
                   const std::vector<Segment>& segments) -> cp::Result<int> {
  std::vector<std::string> created_files;
  auto cleanup = [&] {
    if (cfg.keep_files) return;
    for (const auto& file : created_files) {
      std::error_code ec;
      std::filesystem::remove(file, ec);
    }
  };
  int file_count = 0;

  for (const auto& segment : segments) {
    size_t bytes = line_byte_count(lines, segment);
    if (bytes == 0 && cfg.elide_empty) continue;

    auto filename_result = make_filename(cfg, file_count);
    if (!filename_result) {
      cleanup();
      return std::unexpected(filename_result.error());
    }
    std::string filename = *filename_result;

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
      cleanup();
      return std::unexpected(std::string("cannot create '") + filename + "'");
    }
    for (size_t i = segment.begin; i < segment.end; ++i) {
      out.write(lines[i].data(), static_cast<std::streamsize>(lines[i].size()));
    }
    if (!out) {
      cleanup();
      return std::unexpected(std::string("error writing '") + filename + "'");
    }
    created_files.push_back(filename);

    if (!cfg.quiet) {
      safePrint(std::to_string(bytes));
      safePrint("\n");
    }
    ++file_count;
  }

  return 0;
}

auto run(const Config& cfg) -> int {
  auto lines_result = read_records(cfg.input_file);
  if (!lines_result) {
    cp::report_error(lines_result, L"csplit");
    return 1;
  }

  const auto& lines = *lines_result;
  std::vector<Segment> segments;
  size_t current = 0;

  for (size_t i = 0; i < cfg.patterns.size(); ++i) {
    std::string pattern_text = cfg.patterns[i];
    auto pattern_result = parse_pattern(pattern_text);
    if (!pattern_result) {
      cp::Result<int> error = std::unexpected(pattern_result.error());
      cp::report_error(error, L"csplit");
      return 1;
    }
    ParsedPattern pattern = *pattern_result;

    RepeatSpec repeat;
    if (i + 1 < cfg.patterns.size()) {
      auto repeat_result = parse_repeat(cfg.patterns[i + 1]);
      if (!repeat_result) {
        cp::Result<int> error = std::unexpected(repeat_result.error());
        cp::report_error(error, L"csplit");
        return 1;
      }
      repeat = *repeat_result;
      if (repeat.has_repeat) ++i;
    }

    size_t applications =
        repeat.has_repeat && !repeat.until_exhausted ? repeat.count + 1 : 1;
    bool repeated = false;
    for (size_t application = 0;; ++application) {
      auto applied = apply_pattern(lines, pattern, current, repeated,
                                   cfg.suppress_matched);
      if (!applied) {
        cp::Result<int> error = std::unexpected(applied.error());
        cp::report_error(error, L"csplit");
        return 1;
      }
      if (!applied->found) {
        if (repeat.until_exhausted && repeated) break;
        cp::Result<int> error = std::unexpected(std::string("pattern '") +
                                                pattern_text + "' not found");
        cp::report_error(error, L"csplit");
        return 1;
      }

      size_t old_current = current;
      if (!applied->skip) {
        segments.push_back(applied->output);
      }
      current = applied->next_start;
      repeated = true;

      if (repeat.until_exhausted && current != old_current) continue;
      if (application + 1 >= applications) break;
    }
  }

  segments.push_back(Segment{current, lines.size()});

  auto write_result = write_outputs(cfg, lines, segments);
  if (!write_result) {
    cp::report_error(write_result, L"csplit");
    return 1;
  }

  return 0;
}

}  // namespace csplit_pipeline

REGISTER_COMMAND(
    csplit, "csplit", "csplit [OPTION]... FILE PATTERN...",
    "Output pieces of FILE separated by PATTERN(s) to files 'xx00', 'xx01', "
    "...\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "PATTERN is a line number, or a /regex/ pattern.\n"
    "\n"
    "Note: This implementation supports common GNU line-number, regex,\n"
    "skip, repeat, suppress-matched, and empty-file behavior.",
    "  csplit file.txt '/pattern/'\n"
    "  csplit -f chapter file.txt '/Chapter/'\n"
    "  csplit -n 3 file.txt 100\n"
    "  csplit -z file.txt '/^Header$/' '/^Footer$/'",
    "split(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", CSPLIT_OPTIONS) {
  using namespace csplit_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"csplit");
    return 1;
  }

  return run(*cfg_result);
}
