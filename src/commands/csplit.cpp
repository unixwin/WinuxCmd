// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU]
    OPTION("-f", "--prefix", "use PREFIX instead of 'xx'", STRING_TYPE),
    // [GNU]
    OPTION("-b", "--suffix-format", "use sprintf FORMAT instead of %02d",
           STRING_TYPE),
    // [GNU]
    OPTION("-n", "--digits", "use specified number of digits", STRING_TYPE),
    // [GNU]
    OPTION("-k", "--keep-files", "do not remove output files on errors",
           BOOL_TYPE),
    // [GNU]
    OPTION("-s", "--quiet", "do not print counts of output file sizes",
           BOOL_TYPE),
    // [GNU]
    OPTION("-q", "--silent", "do not print counts of output file sizes",
           BOOL_TYPE),
    // [GNU]
    OPTION("-z", "--elide-empty-files", "remove empty output files", BOOL_TYPE),
    // [GNU]
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
    int digits = 0;
    auto [ptr, ec] = std::from_chars(
        digits_opt.data(), digits_opt.data() + digits_opt.size(), digits);
    if (ec != std::errc() || ptr != digits_opt.data() + digits_opt.size()) {
      return std::unexpected("invalid digits value");
    }
    cfg.digits = digits;
    if (cfg.digits < 0 || cfg.digits > 32) {
      return std::unexpected("digits value must be between 0 and 32");
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
  int magnitude = 0;
  auto digits = text.substr(1);
  if (digits.empty()) return std::unexpected("invalid pattern offset");
  auto [ptr, ec] =
      std::from_chars(digits.data(), digits.data() + digits.size(), magnitude);
  if (ec != std::errc() || ptr != digits.data() + digits.size()) {
    return std::unexpected("invalid pattern offset");
  }
  return text[0] == '-' ? -magnitude : magnitude;
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

  size_t line_number = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), line_number);
  if (ec != std::errc()) {
    return std::unexpected("invalid pattern");
  }
  if (ptr != text.data() + text.size() || line_number == 0) {
    return std::unexpected("invalid line number");
  }

  ParsedPattern pattern;
  pattern.kind = ParsedPattern::Kind::LineNumber;
  pattern.line_number = line_number;
  return pattern;
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

  auto [ptr, ec] =
      std::from_chars(inner.data(), inner.data() + inner.size(), repeat.count);
  if (ec != std::errc() || ptr != inner.data() + inner.size()) {
    return std::unexpected("invalid repeat count");
  }
  return repeat;
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
  size_t matched_line = 0;
  // Next index a regexp scan examines. GNU tracks the scan position
  // independently of the output position: a matched line is consumed even
  // when it becomes the first line of the next output file, and with a
  // positive offset the lines up to the boundary are consumed as well.
  size_t scan_next = 0;
};

auto apply_pattern(const std::vector<std::string>& lines,
                   const ParsedPattern& pattern,
                   const std::string& pattern_text, size_t current, size_t scan,
                   bool repeated, bool suppress_matched, size_t repetition)
    -> cp::Result<PatternApplication> {
  auto out_of_range = [&]() -> cp::Result<PatternApplication> {
    std::string message = "'" + pattern_text + "': line number out of range";
    // GNU appends the repetition number when the failure happens during a
    // {N}/{*} repeat: "csplit: '3': line number out of range on repetition 3".
    if (repetition > 0) {
      message += " on repetition " + std::to_string(repetition);
    }
    return std::unexpected(message);
  };

  PatternApplication result;
  result.skip = pattern.skip;
  result.output.begin = current;
  result.scan_next = scan;

  if (pattern.kind == ParsedPattern::Kind::LineNumber) {
    size_t boundary =
        repeated ? current + pattern.line_number : pattern.line_number - 1;
    // Without --suppress-matched GNU requires the boundary line itself to
    // exist: after writing the segment it checks that more input remains.
    // With --suppress-matched a boundary at EOF is allowed because the
    // suppressed-line removal simply finds nothing left to drop.
    if (boundary < current || boundary > lines.size() ||
        (!suppress_matched && boundary == lines.size())) {
      return out_of_range();
    }
    result.found = true;
    result.matched_line = boundary;
    result.output.end = boundary;
    result.next_start = boundary;
    // After a line-count pattern GNU leaves the boundary line available to
    // the next regexp scan; --suppress-matched removes it, so the scan then
    // resumes after the dropped line.
    result.scan_next =
        suppress_matched && boundary < lines.size() ? boundary + 1 : boundary;
    return result;
  }

  auto re = portable_regex::compile(portable_regex::Syntax::Basic,
                                    pattern.regex_text);
  if (!re) {
    return std::unexpected("invalid regular expression");
  }
  for (size_t i = scan; i < lines.size(); ++i) {
    if (re.pattern.find_all(line_for_regex(lines[i])).empty()) continue;

    int64_t boundary_signed =
        static_cast<int64_t>(i) + static_cast<int64_t>(pattern.offset);
    if (boundary_signed < static_cast<int64_t>(current) ||
        boundary_signed > static_cast<int64_t>(lines.size())) {
      return out_of_range();
    }

    size_t boundary = static_cast<size_t>(boundary_signed);
    result.found = true;
    result.matched_line = i;
    result.output.end = boundary;
    result.next_start = boundary;
    result.scan_next = std::max(i, boundary) + 1;
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

// GNU csplit streams each finalized segment into its own output file as
// soon as the pattern that ends it has been applied. Mirroring that (instead
// of batching all segments up front) preserves GNU's observable behavior on
// error paths: sizes of already-written files are printed, and files created
// before the failure are deleted unless -k/--keep-files is given.
class OutputWriter {
 public:
  OutputWriter(const Config& cfg, const std::vector<std::string>& lines,
               const std::vector<bool>& suppress)
      : cfg_(cfg), lines_(lines), suppress_(suppress) {}

  // Write one finalized segment. Returns false-worthy errors via Result; a
  // return of true means the segment was either written or elided.
  auto write_segment(Segment segment) -> cp::Result<bool> {
    size_t bytes = 0;
    for (size_t i = segment.begin; i < segment.end && i < suppress_.size();
         ++i) {
      if (suppress_[i]) continue;
      bytes += lines_[i].size();
    }
    if (bytes == 0 && cfg_.elide_empty) return true;

    auto filename_result = make_filename(cfg_, file_count_);
    if (!filename_result) {
      return std::unexpected(filename_result.error());
    }
    std::string filename = *filename_result;

    std::ofstream out(filename, std::ios::binary);
    if (!out) {
      return std::unexpected(std::string("cannot create '") + filename + "'");
    }
    for (size_t i = segment.begin; i < segment.end && i < suppress_.size();
         ++i) {
      if (suppress_[i]) continue;
      out.write(lines_[i].data(),
                static_cast<std::streamsize>(lines_[i].size()));
    }
    if (!out) {
      return std::unexpected(std::string("error writing '") + filename + "'");
    }
    created_files_.push_back(filename);

    if (!cfg_.quiet) {
      safePrint(std::to_string(bytes));
      safePrint("\n");
    }
    ++file_count_;
    return true;
  }

  // GNU keeps files created before a fatal error only with -k/--keep-files.
  void cleanup() {
    if (cfg_.keep_files) return;
    for (const auto& file : created_files_) {
      std::error_code ec;
      std::filesystem::remove(file, ec);
    }
  }

  // GNU's in-progress output file is materialized when a pattern fails:
  //  - regex "match not found" and line-number overflow flush everything
  //    after the previous boundary (GNU streams lines out as it scans);
  //  - a regex offset that crosses a split start flushes nothing (GNU keeps
  //    the lines buffered while looking for the match boundary).
  auto materialize_in_progress(Segment segment, bool flush_all)
      -> cp::Result<bool> {
    if (!flush_all) segment.end = segment.begin;
    return write_segment(segment);
  }

 private:
  const Config& cfg_;
  const std::vector<std::string>& lines_;
  const std::vector<bool>& suppress_;
  std::vector<std::string> created_files_;
  int file_count_ = 0;
};

auto run(const Config& cfg) -> int {
  auto lines_result = read_records(cfg.input_file);
  if (!lines_result) {
    cp::report_error(lines_result, L"csplit");
    return 1;
  }

  const auto& lines = *lines_result;
  std::vector<bool> suppress(lines.size(), false);
  size_t current = 0;

  OutputWriter writer(cfg, lines, suppress);
  auto finish_with_error = [&](const std::string& message) {
    writer.cleanup();
    cp::Result<int> error = std::unexpected(message);
    cp::report_error(error, L"csplit");
    return 1;
  };

  // [GNU] csplit parses the whole pattern list before splitting: a bad
  // pattern or regexp aborts before any output file is created.
  struct BoundPattern {
    std::string text;
    ParsedPattern pattern;
    RepeatSpec repeat;
  };
  std::vector<BoundPattern> plan;
  // [GNU] Line-number patterns must be non-decreasing: a smaller value is a
  // fatal parse-time error, an equal value only warns and still runs.
  std::optional<size_t> last_line_number;
  for (size_t i = 0; i < cfg.patterns.size(); ++i) {
    std::string pattern_text = cfg.patterns[i];
    auto pattern_result = parse_pattern(pattern_text);
    if (!pattern_result) {
      cp::Result<int> error = std::unexpected(pattern_result.error());
      cp::report_error(error, L"csplit");
      return 1;
    }
    BoundPattern bound;
    bound.text = pattern_text;
    bound.pattern = *pattern_result;

    if (bound.pattern.kind == ParsedPattern::Kind::LineNumber &&
        last_line_number.has_value()) {
      if (bound.pattern.line_number < *last_line_number) {
        cp::Result<int> error = std::unexpected(
            "line number '" + std::to_string(bound.pattern.line_number) +
            "' is smaller than preceding line number, " +
            std::to_string(*last_line_number));
        cp::report_error(error, L"csplit");
        return 1;
      }
      if (bound.pattern.line_number == *last_line_number) {
        safeErrorPrint("csplit: warning: line number '" +
                       std::to_string(bound.pattern.line_number) +
                       "' is the same as preceding line number\n");
      }
    }
    if (bound.pattern.kind == ParsedPattern::Kind::LineNumber) {
      last_line_number = bound.pattern.line_number;
    }

    if (bound.pattern.kind == ParsedPattern::Kind::Regex) {
      auto re = portable_regex::compile(portable_regex::Syntax::Basic,
                                        bound.pattern.regex_text);
      if (!re) {
        std::string message =
            "'" + pattern_text + "': invalid regular expression";
        if (!re.error.empty()) message += ": " + re.error;
        cp::Result<int> error = std::unexpected(message);
        cp::report_error(error, L"csplit");
        return 1;
      }
    }

    if (i + 1 < cfg.patterns.size()) {
      auto repeat_result = parse_repeat(cfg.patterns[i + 1]);
      if (!repeat_result) {
        cp::Result<int> error = std::unexpected(repeat_result.error());
        cp::report_error(error, L"csplit");
        return 1;
      }
      bound.repeat = *repeat_result;
      if (bound.repeat.has_repeat) ++i;
    }
    plan.push_back(std::move(bound));
  }

  size_t scan = 0;  // next index examined by regexp matching
  for (const auto& bound : plan) {
    const std::string& pattern_text = bound.text;
    const ParsedPattern& pattern = bound.pattern;
    const RepeatSpec& repeat = bound.repeat;

    size_t applications =
        repeat.has_repeat && !repeat.until_exhausted ? repeat.count + 1 : 1;
    bool repeated = false;
    for (size_t application = 0;; ++application) {
      auto applied = apply_pattern(lines, pattern, pattern_text, current, scan,
                                   repeated, cfg.suppress_matched, application);
      if (!applied) {
        // GNU flushes its in-progress output file before dying:
        // line-number patterns and regexes with a non-negative offset
        // stream scanned lines straight into the file, so an out-of-range
        // error still prints everything read so far; a regex with a
        // negative offset buffers the scanned lines, leaving the file
        // empty (size 0) on the same error.
        if (!pattern.skip) {
          const bool stream_to_file =
              pattern.kind == ParsedPattern::Kind::LineNumber ||
              pattern.offset >= 0;
          auto in_progress = writer.materialize_in_progress(
              Segment{current, lines.size()}, stream_to_file);
          if (!in_progress) {
            return finish_with_error(in_progress.error());
          }
        }
        return finish_with_error(applied.error());
      }
      if (!applied->found) {
        if (repeat.until_exhausted) {
          // A {*}-style repeat ends the whole split at the first failed
          // application: GNU flushes the remaining input into the
          // in-progress file (dropping it entirely for %skip% patterns) and
          // exits without running any further patterns or creating a final
          // file afterwards.
          if (!pattern.skip) {
            auto in_progress = writer.materialize_in_progress(
                Segment{current, lines.size()}, true);
            if (!in_progress) {
              return finish_with_error(in_progress.error());
            }
          }
          return 0;
        }
        // GNU streams everything scanned so far into the in-progress file
        // before reporting that the pattern never matched.
        if (!pattern.skip) {
          auto in_progress = writer.materialize_in_progress(
              Segment{current, lines.size()}, true);
          if (!in_progress) {
            return finish_with_error(in_progress.error());
          }
        }
        std::string message = "'" + pattern_text + "': match not found";
        if (application > 0) {
          message += " on repetition " + std::to_string(application);
        }
        return finish_with_error(message);
      }

      // [GNU] --suppress-matched drops the line at the split boundary (the
      // first line of the next output file), which is not necessarily the
      // line the regexp matched when an offset is given.
      if (cfg.suppress_matched && applied->next_start < suppress.size()) {
        suppress[applied->next_start] = true;
      }

      if (!applied->skip) {
        auto written = writer.write_segment(applied->output);
        if (!written) {
          return finish_with_error(written.error());
        }
      }
      current = applied->next_start;
      scan = applied->scan_next;
      repeated = true;

      // {*}-style repeats run until an application fails; regexp scans
      // always make progress (scan_next strictly increases) and line
      // numbers keep growing, so the loop cannot spin forever.
      if (repeat.until_exhausted) continue;
      if (application + 1 >= applications) break;
    }
  }

  // GNU always flushes a final output file with the remaining lines, even
  // when that remainder is empty.
  auto written = writer.write_segment(Segment{current, lines.size()});
  if (!written) {
    return finish_with_error(written.error());
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
    if (cfg_result.error() == "missing input file" ||
        cfg_result.error() == "missing pattern") {
      safeErrorPrint("Try 'csplit --help' for more information.\n");
    }
    return 1;
  }

  return run(*cfg_result);
}
