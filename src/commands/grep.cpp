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
 *  - File: grep.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implemention for grep.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
/// @TODO:1.Stream reading. 2.Replace filesystem.
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
 * @brief GREP command options definition
 *
 * This array defines all the options supported by the grep command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 *
 * - @a -E, @a --extended-regexp: PATTERNS are extended regular expressions
 * [IMPLEMENTED]
 * - @a -F, @a --fixed-strings: PATTERNS are strings [IMPLEMENTED]
 * - @a -G, @a --basic-regexp: PATTERNS are basic regular expressions
 * [IMPLEMENTED]
 * - @a -P, @a --perl-regexp: PATTERNS are Perl regular expressions [NOT
 * SUPPORT]
 * - @a -e, @a --regexp: Use PATTERNS for matching [IMPLEMENTED]
 * - @a -f, @a --file: Take PATTERNS from FILE [IMPLEMENTED]
 * - @a -i, @a --ignore-case: Ignore case distinctions in patterns and data
 * [IMPLEMENTED]
 * - @a --no-ignore-case: Do not ignore case distinctions (default)
 * [IMPLEMENTED]
 * - @a -w, @a --word-regexp: Match only whole words [IMPLEMENTED]
 * - @a -x, @a --line-regexp: Match only whole lines [IMPLEMENTED]
 * - @a -z, @a --null-data: A data line ends in 0 byte, not newline
 * [IMPLEMENTED]
 * - @a -s, @a --no-messages: Suppress error messages [IMPLEMENTED]
 * - @a -v, @a --invert-match: Select non-matching lines [IMPLEMENTED]
 * - @a -m, @a --max-count: Stop after NUM selected lines [IMPLEMENTED]
 * - @a -b, @a --byte-offset: Print the byte offset with output lines
 * [IMPLEMENTED]
 * - @a -n, @a --line-number: Print line number with output lines [IMPLEMENTED]
 * - @a --line-buffered: Flush output on every line [IMPLEMENTED]
 * - @a -H, @a --with-filename: Print file name with output lines [IMPLEMENTED]
 * - @a -h, @a --no-filename: Suppress the file name prefix on output
 * [IMPLEMENTED]
 * - @a --label: Use LABEL as the standard input file name prefix [IMPLEMENTED]
 * - @a -o, @a --only-matching: Show only nonempty parts of lines that match
 * [IMPLEMENTED]
 * - @a -q, @a --quiet: Suppress all normal output [IMPLEMENTED]
 * - @a --silent: Suppress all normal output [IMPLEMENTED]
 * - @a --binary-files: Assume that binary files are TYPE [NOT SUPPORT]
 * - @a -a, @a --text: Equivalent to --binary-files=text [NOT SUPPORT]
 * - @a -I: Equivalent to --binary-files=without-match [NOT SUPPORT]
 * - @a -d, @a --directories: How to handle directories: read, recurse, skip
 * [IMPLEMENTED]
 * - @a -D, @a --devices: How to handle devices/FIFOs/sockets [NOT SUPPORT]
 * - @a -r, @a --recursive: Like --directories=recurse [IMPLEMENTED]
 * - @a -R, @a --dereference-recursive: Like -r but follow symlinks
 *
 * [IMPLEMENTED]
 * - @a --include: Search only files that match GLOB
 * [IMPLEMENTED]
 * - @a --exclude: Skip files that match GLOB [IMPLEMENTED]
 * - @a --exclude-from: Skip files from patterns in FILE [NOT SUPPORT]
 * - @a --exclude-dir: Skip directories that match GLOB [IMPLEMENTED]
 * - @a
 * -L, @a --files-without-match: Print only names of FILEs with no selected
 * lines [IMPLEMENTED]
 * - @a -l, @a --files-with-matches: Print only names of FILEs with selected
 * lines [IMPLEMENTED]
 * - @a -c, @a --count: Print only a count of selected lines per FILE
 * [IMPLEMENTED]
 * - @a -T, @a --initial-tab: Make tabs line up (if needed) [IMPLEMENTED]
 * - @a -Z, @a --null: Print 0 byte after FILE name [IMPLEMENTED]
 * - @a -B, @a --before-context: Print NUM lines of leading context
 * [IMPLEMENTED]
 * - @a -A, @a --after-context: Print NUM lines of trailing context
 * [IMPLEMENTED]
 * - @a -C, @a --context: Print NUM lines of output context [IMPLEMENTED]
 * - @a --group-separator: Print separator between groups [IMPLEMENTED]
 * - @a --no-group-separator: Do not print group separator [IMPLEMENTED]
 * - @a --color: Highlight matching strings [IMPLEMENTED]
 * - @a --colour: Highlight matching strings [IMPLEMENTED]
 * - @a -U, @a --binary: Do not strip CR at EOL [IMPLEMENTED]
 */
auto constexpr GREP_OPTIONS = std::array{
    OPTION("-E", "--extended-regexp",
           "PATTERNS are extended regular expressions"),
    OPTION("-F", "--fixed-strings", "PATTERNS are strings"),
    OPTION("-G", "--basic-regexp", "PATTERNS are basic regular expressions"),
    OPTION("-P", "--perl-regexp",
           "PATTERNS are Perl regular expressions [NOT SUPPORT]"),
    OPTION("-e", "--regexp", "use PATTERNS for matching", STRING_TYPE),
    OPTION("-f", "--file", "take PATTERNS from FILE", STRING_TYPE),
    OPTION("-i", "--ignore-case",
           "ignore case distinctions in patterns and data"),
    OPTION("", "--no-ignore-case", "do not ignore case distinctions (default)"),
    OPTION("-w", "--word-regexp", "match only whole words"),
    OPTION("-x", "--line-regexp", "match only whole lines"),
    OPTION("-z", "--null-data", "a data line ends in 0 byte, not newline"),
    OPTION("-s", "--no-messages", "suppress error messages"),
    OPTION("-v", "--invert-match", "select non-matching lines"),
    OPTION("-m", "--max-count", "stop after NUM selected lines", INT_TYPE),
    OPTION("-b", "--byte-offset", "print the byte offset with output lines"),
    OPTION("-n", "--line-number", "print line number with output lines"),
    OPTION("", "--line-buffered", "flush output on every line"),
    OPTION("-H", "--with-filename", "print file name with output lines"),
    OPTION("-h", "--no-filename", "suppress the file name prefix on output"),
    OPTION("", "--label", "use LABEL as the standard input file name prefix",
           STRING_TYPE),
    OPTION("-o", "--only-matching",
           "show only nonempty parts of lines that match"),
    OPTION("-q", "--quiet", "suppress all normal output"),
    OPTION("", "--silent", "suppress all normal output"),
    OPTION("", "--binary-files",
           "assume that binary files are TYPE [NOT SUPPORT]", STRING_TYPE),
    OPTION("-a", "--text", "equivalent to --binary-files=text [NOT SUPPORT]"),
    OPTION("-I", "",
           "equivalent to --binary-files=without-match [NOT SUPPORT]"),
    OPTION("-d", "--directories",
           "how to handle directories: read, recurse, skip", STRING_TYPE),
    OPTION("-D", "--devices",
           "how to handle devices/FIFOs/sockets [NOT SUPPORT]", STRING_TYPE),
    OPTION("-r", "--recursive", "like --directories=recurse"),
    OPTION("-R", "--dereference-recursive", "like -r but follow symlinks"),
    OPTION("", "--include", "search only files that match GLOB", STRING_TYPE),
    OPTION("", "--exclude", "skip files that match GLOB", STRING_TYPE),
    OPTION("", "--exclude-from",
           "skip files from patterns in FILE [NOT SUPPORT]", STRING_TYPE),
    OPTION("", "--exclude-dir", "skip directories that match GLOB",
           STRING_TYPE),
    OPTION("-L", "--files-without-match",
           "print only names of FILEs with no selected lines"),
    OPTION("-l", "--files-with-matches",
           "print only names of FILEs with selected lines"),
    OPTION("-c", "--count", "print only a count of selected lines per FILE"),
    OPTION("-T", "--initial-tab", "make tabs line up (if needed)"),
    OPTION("-Z", "--null", "print 0 byte after FILE name"),
    OPTION("-B", "--before-context", "print NUM lines of leading context",
           INT_TYPE),
    OPTION("-A", "--after-context", "print NUM lines of trailing context",
           INT_TYPE),
    OPTION("-C", "--context", "print NUM lines of output context", INT_TYPE),
    OPTION("", "--group-separator", "print separator between groups",
           STRING_TYPE),
    OPTION("", "--no-group-separator", "do not print group separator"),
    OPTION(
        "", "--color",
        "highlight matching strings; WHEN can be 'always', 'never', or 'auto'",
        STRING_TYPE),
    OPTION(
        "", "--colour",
        "highlight matching strings; WHEN can be 'always', 'never', or 'auto'",
        STRING_TYPE),
    OPTION("-U", "--binary", "do not strip CR at EOL")};

namespace grep_pipeline {
namespace cp = core::pipeline;

enum class PatternMode { BasicRegex, ExtendedRegex, Fixed };

struct MatchPiece {
  size_t begin = 0;
  size_t end = 0;
};

struct Pattern {
  std::string raw;
  std::string lowered;
  std::optional<std::regex> regex;
};

struct Config {
  PatternMode mode = PatternMode::BasicRegex;
  bool ignore_case = false;
  bool word_regexp = false;
  bool line_regexp = false;
  bool null_data = false;
  bool no_messages = false;
  bool invert_match = false;
  int max_count = -1;
  bool byte_offset = false;
  bool line_number = false;
  bool with_filename = false;
  bool no_filename = false;
  std::string label;
  bool only_matching = false;
  bool quiet = false;
  std::string directories = "read";
  bool files_without_match = false;
  bool files_with_matches = false;
  bool count_only = false;
  bool null_after_filename = false;
  bool recursive = false;
  bool dereference_recursive = false;
  SmallVector<Pattern, 32> patterns;
  SmallVector<std::string, 64> files;
  bool has_error = false;
  int before_context = 0;
  int after_context = 0;
  bool color = false;
  std::string include;
  std::string exclude;
  std::string exclude_dir;
  std::string group_separator = "--";
  bool no_group_separator = false;
  bool initial_tab = false;
};

auto to_lower_ascii(std::string_view s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

auto split_lines(std::string_view s) -> std::vector<std::string> {
  std::vector<std::string> parts;
  parts.reserve(s.size() / 40);  // Reserve for ~40 chars per line
  size_t start = 0;
  while (start <= s.size()) {
    size_t pos = s.find('\n', start);
    if (pos == std::string_view::npos) {
      parts.emplace_back(s.substr(start));
      break;
    }
    parts.emplace_back(s.substr(start, pos - start));
    start = pos + 1;
  }
  return parts;
}

auto split_records(std::string_view s, char delim)
    -> std::vector<std::pair<size_t, size_t>> {
  std::vector<std::pair<size_t, size_t>> out;
  out.reserve(s.size() / 40);  // Reserve for ~40 chars per record
  size_t start = 0;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == delim) {
      out.emplace_back(start, i + 1);
      start = i + 1;
    }
  }
  if (start < s.size()) {
    out.emplace_back(start, s.size());
  }
  return out;
}

auto is_word_char(unsigned char c) -> bool {
  return std::isalnum(c) || c == '_';
}

auto word_boundary_ok(std::string_view line, size_t begin, size_t end) -> bool {
  bool left_ok = (begin == 0) ||
                 !is_word_char(static_cast<unsigned char>(line[begin - 1]));
  bool right_ok = (end >= line.size()) ||
                  !is_word_char(static_cast<unsigned char>(line[end]));
  return left_ok && right_ok;
}

auto compile_pattern(PatternMode mode, bool ignore_case, std::string_view raw)
    -> cp::Result<Pattern> {
  Pattern p;
  p.raw = std::string(raw);
  p.lowered = to_lower_ascii(raw);

  if (mode == PatternMode::Fixed) return p;

  try {
    auto flags = mode == PatternMode::ExtendedRegex
                     ? std::regex_constants::extended
                     : std::regex_constants::basic;
    if (ignore_case) flags |= std::regex::icase;
    p.regex.emplace(p.raw, flags);
  } catch (const std::regex_error&) {
    return std::unexpected("invalid regular expression: " + std::string(raw));
  }

  return p;
}

auto load_patterns_from_file(const std::string& path)
    -> cp::Result<std::vector<std::string>> {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open pattern file '" + path + "'");
  }

  std::string buf((std::istreambuf_iterator<char>(in)),
                  std::istreambuf_iterator<char>());
  return split_lines(buf);
}

auto is_unsupported_used(const CommandContext<GREP_OPTIONS.size()>& ctx)
    -> std::optional<std::string> {
  if (ctx.get<bool>("--perl-regexp", false) || ctx.get<bool>("-P", false))
    return "--perl-regexp is [NOT SUPPORT]";
  if (!ctx.get<std::string>("--binary-files", "").empty() ||
      ctx.get<bool>("--text", false) || ctx.get<bool>("-a", false) ||
      ctx.get<bool>("-I", false))
    return "binary file mode options are [NOT SUPPORT]";
  if (!ctx.get<std::string>("--devices", "").empty() ||
      !ctx.get<std::string>("-D", "").empty())
    return "--devices is [NOT SUPPORT]";
  if (!ctx.get<std::string>("--exclude-from", "").empty())
    return "--exclude-from is [NOT SUPPORT]";
  return std::nullopt;
}

auto build_config(const CommandContext<GREP_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  if (auto unsupported = is_unsupported_used(ctx); unsupported.has_value()) {
    return std::unexpected(*unsupported);
  }

  cfg.mode = PatternMode::BasicRegex;
  if (ctx.get<bool>("--fixed-strings", false) || ctx.get<bool>("-F", false))
    cfg.mode = PatternMode::Fixed;
  if (ctx.get<bool>("--extended-regexp", false) || ctx.get<bool>("-E", false))
    cfg.mode = PatternMode::ExtendedRegex;
  if (ctx.get<bool>("--basic-regexp", false) || ctx.get<bool>("-G", false))
    cfg.mode = PatternMode::BasicRegex;

  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-i", false);
  if (ctx.get<bool>("--no-ignore-case", false)) cfg.ignore_case = false;

  cfg.word_regexp =
      ctx.get<bool>("--word-regexp", false) || ctx.get<bool>("-w", false);
  cfg.line_regexp =
      ctx.get<bool>("--line-regexp", false) || ctx.get<bool>("-x", false);
  cfg.null_data =
      ctx.get<bool>("--null-data", false) || ctx.get<bool>("-z", false);
  cfg.no_messages =
      ctx.get<bool>("--no-messages", false) || ctx.get<bool>("-s", false);
  cfg.invert_match =
      ctx.get<bool>("--invert-match", false) || ctx.get<bool>("-v", false);
  cfg.max_count = ctx.get<int>("--max-count", -1);
  if (cfg.max_count < 0) cfg.max_count = ctx.get<int>("-m", -1);
  cfg.byte_offset =
      ctx.get<bool>("--byte-offset", false) || ctx.get<bool>("-b", false);
  cfg.line_number =
      ctx.get<bool>("--line-number", false) || ctx.get<bool>("-n", false);
  cfg.with_filename =
      ctx.get<bool>("--with-filename", false) || ctx.get<bool>("-H", false);
  cfg.no_filename =
      ctx.get<bool>("--no-filename", false) || ctx.get<bool>("-h", false);
  cfg.label = ctx.get<std::string>("--label", "");
  cfg.only_matching =
      ctx.get<bool>("--only-matching", false) || ctx.get<bool>("-o", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) ||
              ctx.get<bool>("--silent", false) || ctx.get<bool>("-q", false);
  cfg.files_without_match = ctx.get<bool>("--files-without-match", false) ||
                            ctx.get<bool>("-L", false);
  cfg.files_with_matches = ctx.get<bool>("--files-with-matches", false) ||
                           ctx.get<bool>("-l", false);
  cfg.count_only =
      ctx.get<bool>("--count", false) || ctx.get<bool>("-c", false);
  cfg.null_after_filename =
      ctx.get<bool>("--null", false) || ctx.get<bool>("-Z", false);

  cfg.recursive = ctx.get<bool>("--recursive", false) ||
                  ctx.get<bool>("-r", false) ||
                  ctx.get<bool>("--dereference-recursive", false) ||
                  ctx.get<bool>("-R", false);
  cfg.dereference_recursive = ctx.get<bool>("--dereference-recursive", false) ||
                              ctx.get<bool>("-R", false);
  cfg.directories = ctx.get<std::string>("--directories", "");
  if (cfg.directories.empty()) cfg.directories = ctx.get<std::string>("-d", "");
  if (cfg.directories.empty())
    cfg.directories = cfg.recursive ? "recurse" : "read";

  cfg.before_context = ctx.get<int>("-B", 0);
  cfg.after_context = ctx.get<int>("-A", 0);
  int context = ctx.get<int>("-C", 0);
  if (context > 0) {
    cfg.before_context = context;
    cfg.after_context = context;
  }

  std::string color_opt = ctx.get<std::string>("--color", "");
  if (color_opt.empty()) color_opt = ctx.get<std::string>("--colour", "");
  cfg.color = (color_opt == "always" || color_opt == "auto");

  cfg.include = ctx.get<std::string>("--include", "");
  cfg.exclude = ctx.get<std::string>("--exclude", "");
  cfg.exclude_dir = ctx.get<std::string>("--exclude-dir", "");

  cfg.group_separator = ctx.get<std::string>("--group-separator", "--");
  cfg.no_group_separator = ctx.get<bool>("--no-group-separator", false);
  cfg.initial_tab =
      ctx.get<bool>("--initial-tab", false) || ctx.get<bool>("-T", false);

  SmallVector<std::string, 32> raw_patterns;
  std::string p_e = ctx.get<std::string>("--regexp", "");
  if (p_e.empty()) p_e = ctx.get<std::string>("-e", "");
  if (!p_e.empty()) {
    auto from_e = split_lines(p_e);
    for (const auto& p : from_e) raw_patterns.push_back(p);
  }

  std::string p_file = ctx.get<std::string>("--file", "");
  if (p_file.empty()) p_file = ctx.get<std::string>("-f", "");
  if (!p_file.empty()) {
    auto file_patterns = load_patterns_from_file(p_file);
    if (!file_patterns) return std::unexpected(file_patterns.error());
    for (const auto& p : *file_patterns) raw_patterns.push_back(p);
  }

  std::vector<std::string> positionals;
  for (auto p : ctx.positionals) positionals.emplace_back(p);

  if (raw_patterns.empty()) {
    if (positionals.empty()) {
      return std::unexpected("missing PATTERNS");
    }
    auto split = split_lines(positionals.front());
    for (const auto& p : split) raw_patterns.push_back(p);
    positionals.erase(positionals.begin(),
                      positionals.begin() + 1);  // Remove first element
  }

  if (raw_patterns.empty()) {
    return std::unexpected("missing PATTERNS");
  }

  for (const auto& rp : raw_patterns) {
    auto c = compile_pattern(cfg.mode, cfg.ignore_case, rp);
    if (!c) return std::unexpected(c.error());
    cfg.patterns.push_back(*c);
  }

  for (const auto& p : positionals) cfg.files.push_back(std::string(p));
  if (cfg.files.empty()) {
    if (cfg.directories == "recurse") {
      cfg.files.push_back(".");
    } else {
      cfg.files.push_back("-");
    }
  }

  return cfg;
}

auto record_name_for_output(std::string_view input_name, const Config& cfg)
    -> std::string {
  if (input_name == "-") {
    if (!cfg.label.empty()) return cfg.label;
    return "(standard input)";
  }
  return std::string(input_name);
}

auto collect_matches_in_line(std::string_view line, const Config& cfg)
    -> std::vector<MatchPiece> {
  std::vector<MatchPiece> out;
  std::string lowered_line;

  for (const auto& p : cfg.patterns) {
    if (cfg.mode == PatternMode::Fixed) {
      if (cfg.line_regexp) {
        bool eq = cfg.ignore_case ? (to_lower_ascii(line) == p.lowered)
                                  : (line == p.raw);
        if (eq) out.push_back(MatchPiece{0, line.size()});
        continue;
      }

      size_t cursor = 0;
      while (true) {
        size_t pos = std::string_view::npos;
        size_t len = 0;
        if (cfg.ignore_case) {
          if (lowered_line.empty() && !line.empty()) {
            lowered_line = to_lower_ascii(line);
          }
          pos = lowered_line.find(p.lowered, cursor);
          len = p.lowered.size();
        } else {
          pos = line.find(p.raw, cursor);
          len = p.raw.size();
        }

        if (pos == std::string_view::npos) break;
        MatchPiece m{pos, pos + len};
        if (!cfg.word_regexp || word_boundary_ok(line, m.begin, m.end)) {
          out.push_back(m);
        }
        cursor = m.begin + 1;
      }
      continue;
    }

    if (!p.regex.has_value()) continue;

    if (cfg.line_regexp) {
      if (std::regex_match(line.begin(), line.end(), *p.regex)) {
        out.push_back(MatchPiece{0, line.size()});
      }
      continue;
    }

    std::string line_copy(line);
    for (auto it =
             std::sregex_iterator(line_copy.begin(), line_copy.end(), *p.regex);
         it != std::sregex_iterator(); ++it) {
      auto pos = static_cast<size_t>(it->position());
      auto len = static_cast<size_t>(it->length());
      if (len == 0) continue;
      MatchPiece m{pos, pos + len};
      if (!cfg.word_regexp || word_boundary_ok(line, m.begin, m.end)) {
        out.push_back(m);
      }
    }
  }

  std::sort(out.begin(), out.end(),
            [](const MatchPiece& a, const MatchPiece& b) {
              if (a.begin != b.begin) return a.begin < b.begin;
              return a.end < b.end;
            });
  out.erase(std::unique(out.begin(), out.end(),
                        [](const MatchPiece& a, const MatchPiece& b) {
                          return a.begin == b.begin && a.end == b.end;
                        }),
            out.end());
  return out;
}

auto append_prefix(std::string& out, const Config& cfg, bool show_filename,
                   std::string_view display_name, size_t line_no, size_t offset)
    -> void {
  if (show_filename) {
    if (cfg.color) {
      out.append("\033[1;35m");
      out.append(display_name);
      out.append("\033[0m");
    } else {
      out.append(display_name);
    }
    out.push_back(':');
  }
  if (cfg.line_number) {
    if (cfg.color) {
      out.append("\033[1;32m");
      auto s = std::to_string(line_no);
      out.append(s);
      out.append("\033[0m");
    } else {
      auto s = std::to_string(line_no);
      out.append(s);
    }
    out.push_back(':');
  }
  if (cfg.byte_offset) {
    auto s = std::to_string(offset);
    out.append(s);
    out.push_back(':');
  }
}

auto append_line_with_color(std::string& out, std::string_view line,
                            const std::vector<MatchPiece>& matches, bool color)
    -> void {
  if (!color || matches.empty()) {
    out.append(line);
    return;
  }
  size_t pos = 0;
  for (const auto& m : matches) {
    if (m.begin > pos) {
      out.append(line.substr(pos, m.begin - pos));
    }
    out.append("\033[1;31m");
    out.append(line.substr(m.begin, m.end - m.begin));
    out.append("\033[0m");
    pos = m.end;
  }
  if (pos < line.size()) {
    out.append(line.substr(pos));
  }
}

auto process_selected_record(std::string_view line, bool had_delim,
                             std::string_view display_name, bool show_filename,
                             size_t line_no, size_t offset, Config& cfg,
                             size_t& selected_count) -> bool {
  auto matches = collect_matches_in_line(line, cfg);
  bool is_match = !matches.empty();
  bool selected = cfg.invert_match ? !is_match : is_match;
  if (!selected) return false;

  ++selected_count;
  if (cfg.quiet) return true;

  if (cfg.files_with_matches || cfg.files_without_match || cfg.count_only)
    return true;

  const char delim = cfg.null_data ? '\0' : '\n';
  std::string output_buf;
  output_buf.reserve(line.size() + 128);

  if (cfg.only_matching && !cfg.invert_match) {
    for (const auto& m : matches) {
      if (m.end <= m.begin) continue;
      output_buf.clear();
      append_prefix(output_buf, cfg, show_filename, display_name, line_no,
                    offset + m.begin);
      if (cfg.initial_tab) output_buf.push_back('\t');
      if (cfg.color) {
        output_buf.append("\033[1;31m");
        output_buf.append(line.substr(m.begin, m.end - m.begin));
        output_buf.append("\033[0m");
      } else {
        output_buf.append(line.substr(m.begin, m.end - m.begin));
      }
      output_buf.append(1, delim);
      safePrint(output_buf);
    }
  } else {
    append_prefix(output_buf, cfg, show_filename, display_name, line_no,
                  offset);
    if (cfg.initial_tab) output_buf.push_back('\t');
    append_line_with_color(output_buf, line, matches, cfg.color);
    if (had_delim) {
      output_buf.append(1, delim);
    } else {
      output_buf.append(cfg.null_data ? "\0" : "\n");
    }
    safePrint(output_buf);
  }
  return true;
}

auto scan_text(const std::string& text, std::string_view display_name,
               bool show_filename, Config& cfg) -> std::pair<bool, size_t> {
  const char delim = cfg.null_data ? '\0' : '\n';
  auto records = split_records(text, delim);

  bool any_selected = false;
  size_t selected_count = 0;
  bool use_context = (cfg.before_context > 0 || cfg.after_context > 0) &&
                     !cfg.count_only && !cfg.files_with_matches &&
                     !cfg.files_without_match;

  if (!use_context) {
    for (size_t i = 0; i < records.size(); ++i) {
      const auto [b, e] = records[i];
      std::string_view whole(text.data() + b, e - b);
      bool had_delim = !whole.empty() && whole.back() == delim;
      std::string_view line =
          had_delim ? whole.substr(0, whole.size() - 1) : whole;
      if (!process_selected_record(line, had_delim, display_name, show_filename,
                                   i + 1, b, cfg, selected_count))
        continue;
      any_selected = true;

      if (cfg.quiet) return {true, selected_count};

      if (cfg.max_count >= 0 &&
          static_cast<int>(selected_count) >= cfg.max_count)
        break;
    }
    return {any_selected, selected_count};
  }

  std::vector<std::pair<size_t, size_t>> match_indices;
  for (size_t i = 0; i < records.size(); ++i) {
    const auto [b, e] = records[i];
    std::string_view whole(text.data() + b, e - b);
    bool had_delim = !whole.empty() && whole.back() == delim;
    std::string_view line =
        had_delim ? whole.substr(0, whole.size() - 1) : whole;
    auto matches = collect_matches_in_line(line, cfg);
    bool is_match = !matches.empty();
    bool selected = cfg.invert_match ? !is_match : is_match;
    if (selected) {
      match_indices.push_back({i, i});
      ++selected_count;
      if (cfg.quiet) return {true, selected_count};
      if (cfg.max_count >= 0 &&
          static_cast<int>(selected_count) >= cfg.max_count)
        break;
    }
  }

  if (match_indices.empty()) {
    return {false, 0};
  }

  std::vector<std::pair<size_t, size_t>> groups;
  size_t group_start = match_indices[0].first;
  size_t group_end = match_indices[0].first;
  for (size_t i = 1; i < match_indices.size(); ++i) {
    size_t idx = match_indices[i].first;
    if (idx <= group_end + 1) {
      group_end = idx;
    } else {
      groups.push_back({group_start, group_end});
      group_start = idx;
      group_end = idx;
    }
  }
  groups.push_back({group_start, group_end});

  bool first_group = true;
  for (const auto& [gs, ge] : groups) {
    size_t start = (gs >= static_cast<size_t>(cfg.before_context))
                       ? gs - cfg.before_context
                       : 0;
    size_t end = ge + cfg.after_context;
    if (end >= records.size()) end = records.size() - 1;

    if (!first_group) {
      if (!cfg.no_group_separator) {
        safePrint(cfg.group_separator);
        safePrint("\n");
      }
    }
    first_group = false;

    for (size_t i = start; i <= end; ++i) {
      const auto [b, e] = records[i];
      std::string_view whole(text.data() + b, e - b);
      bool had_delim = !whole.empty() && whole.back() == delim;
      std::string_view line =
          had_delim ? whole.substr(0, whole.size() - 1) : whole;

      bool is_in_match_group = (i >= gs && i <= ge);
      if (is_in_match_group) {
        auto matches = collect_matches_in_line(line, cfg);
        bool is_match = !matches.empty();
        bool selected = cfg.invert_match ? !is_match : is_match;
        if (selected) {
          std::string line_buf;
          line_buf.reserve(line.size() + 128);
          append_prefix(line_buf, cfg, show_filename, display_name, i + 1, b);
          if (cfg.initial_tab) line_buf.push_back('\t');
          append_line_with_color(line_buf, line, matches, cfg.color);
          if (had_delim) {
            line_buf.append(1, delim);
          } else {
            line_buf.append(cfg.null_data ? "\0" : "\n");
          }
          safePrint(line_buf);
        }
      } else {
        std::string line_buf;
        line_buf.reserve(line.size() + 128);
        append_prefix(line_buf, cfg, show_filename, display_name, i + 1, b);
        if (cfg.initial_tab) line_buf.push_back('\t');
        line_buf.append(line);
        if (had_delim) {
          line_buf.append(1, delim);
        } else {
          line_buf.append(cfg.null_data ? "\0" : "\n");
        }
        safePrint(line_buf);
      }
    }
  }

  any_selected = !match_indices.empty();
  return {any_selected, selected_count};
}

auto scan_stream(std::istream& in, std::string_view display_name,
                 bool show_filename, Config& cfg) -> std::pair<bool, size_t> {
  const char delim = cfg.null_data ? '\0' : '\n';
  std::array<char, 64 * 1024> chunk{};
  std::string pending;
  pending.reserve(chunk.size() * 2);

  size_t base_offset = 0;
  size_t line_no = 1;
  bool any_selected = false;
  size_t selected_count = 0;

  while (in) {
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    std::streamsize got = in.gcount();
    if (got <= 0) break;

    pending.append(chunk.data(), static_cast<size_t>(got));

    size_t start = 0;
    for (size_t i = 0; i < pending.size(); ++i) {
      if (pending[i] != delim) continue;

      std::string_view line(pending.data() + start, i - start);
      size_t offset = base_offset + start;
      if (process_selected_record(line, true, display_name, show_filename,
                                  line_no, offset, cfg, selected_count)) {
        any_selected = true;
        if (cfg.quiet) return {true, selected_count};
        if (cfg.max_count >= 0 &&
            static_cast<int>(selected_count) >= cfg.max_count) {
          return {any_selected, selected_count};
        }
      }

      ++line_no;
      start = i + 1;
    }

    if (start > 0) {
      base_offset += start;
      pending.erase(0, start);
    }
  }

  if (!pending.empty()) {
    if (process_selected_record(pending, false, display_name, show_filename,
                                line_no, base_offset, cfg, selected_count)) {
      any_selected = true;
    }
  }

  return {any_selected, selected_count};
}

auto read_file_binary(const std::string& path) -> cp::Result<std::string> {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + path + "'");
  }
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

auto gather_files_for_input(const Config& cfg, std::vector<std::string>& out)
    -> cp::Result<void> {
  for (const auto& f : cfg.files) {
    if (f == "-") {
      out.push_back(f);
      continue;
    }

    // Smart glob expansion for wildcard patterns
    if (contains_wildcard(f)) {
      auto glob_result = glob_expand(f);
      if (glob_result.expanded) {
        // Pattern was expanded, add all matched files
        for (const auto& file : glob_result.files) {
          out.push_back(wstring_to_utf8(file));
        }
        continue;
      }
      // If expansion failed, fall through to normal processing
    }

    std::error_code ec;
    bool is_dir = std::filesystem::is_directory(f, ec);
    if (ec) {
      out.push_back(f);
      continue;
    }

    if (!is_dir) {
      if (!cfg.include.empty()) {
        std::string filename = std::filesystem::path(f).filename().string();
        if (!wildcard_match(cfg.include, filename)) {
          continue;
        }
      }
      if (!cfg.exclude.empty()) {
        std::string filename = std::filesystem::path(f).filename().string();
        if (wildcard_match(cfg.exclude, filename)) {
          continue;
        }
      }
      out.push_back(f);
      continue;
    }

    if (cfg.directories == "skip") continue;

    if (cfg.directories == "recurse") {
      auto options = std::filesystem::directory_options::skip_permission_denied;
      if (cfg.dereference_recursive) {
        options |= std::filesystem::directory_options::follow_directory_symlink;
      }
      std::filesystem::recursive_directory_iterator it(f, options);
      const std::filesystem::recursive_directory_iterator end;
      for (; it != end; ++it) {
        const auto& e = *it;
        if (e.is_directory()) {
          std::string dirname = e.path().filename().string();
          if (!cfg.exclude_dir.empty() &&
              wildcard_match(cfg.exclude_dir, dirname)) {
            it.disable_recursion_pending();
          }
          continue;
        }
        if (e.is_regular_file()) {
          std::string filepath = e.path().string();
          std::string filename = e.path().filename().string();
          if (!cfg.include.empty() && !wildcard_match(cfg.include, filename)) {
            continue;
          }
          if (!cfg.exclude.empty() && wildcard_match(cfg.exclude, filename)) {
            continue;
          }
          out.push_back(filepath);
        }
      }
      continue;
    }

    return std::unexpected("'" + f + "' is a directory");
  }
  return {};
}

auto process(Config& cfg) -> int {
  std::vector<std::string> inputs;
  auto gather = gather_files_for_input(cfg, inputs);
  if (!gather) {
    if (!cfg.no_messages && !cfg.quiet) {
      safeErrorPrint("grep: ");
      safeErrorPrint(gather.error());
      safeErrorPrint("\n");
    }
    return 2;
  }

  bool show_filename = false;
  if (cfg.with_filename) {
    show_filename = true;
  } else if (!cfg.no_filename && inputs.size() > 1) {
    show_filename = true;
  }

  bool any_selected_global = false;

  for (const auto& input : inputs) {
    std::pair<bool, size_t> scan_result{false, 0};
    auto display_name = record_name_for_output(input, cfg);

    bool use_context = (cfg.before_context > 0 || cfg.after_context > 0) &&
                       !cfg.count_only && !cfg.files_with_matches &&
                       !cfg.files_without_match;

    if (input == "-") {
      scan_result = scan_stream(std::cin, display_name, show_filename, cfg);
    } else if (use_context) {
      // Read entire file for context support
      auto content = read_file_binary(input);
      if (!content) {
        cfg.has_error = true;
        if (!cfg.no_messages && !cfg.quiet) {
          safeErrorPrint("grep: ");
          safeErrorPrint(content.error());
          safeErrorPrint("\n");
        }
        continue;
      }
      scan_result = scan_text(*content, display_name, show_filename, cfg);
    } else {
      std::ifstream in(input, std::ios::binary);
      if (!in.is_open()) {
        cfg.has_error = true;
        if (!cfg.no_messages && !cfg.quiet) {
          safeErrorPrint("grep: ");
          safeErrorPrint("cannot open '" + input + "'");
          safeErrorPrint("\n");
        }
        continue;
      }
      scan_result = scan_stream(in, display_name, show_filename, cfg);
    }

    auto [any_selected, selected_count] = scan_result;
    any_selected_global = any_selected_global || any_selected;

    if (!cfg.quiet) {
      if (cfg.files_with_matches && any_selected) {
        safePrint(display_name);
        safePrint(cfg.null_after_filename ? "\0" : "\n");
      }

      if (cfg.files_without_match && !any_selected) {
        safePrint(display_name);
        safePrint(cfg.null_after_filename ? "\0" : "\n");
      }

      if (cfg.count_only) {
        if (show_filename) {
          safePrint(display_name);
          safePrint(":");
        }
        safePrint(std::to_string(selected_count));
        safePrint("\n");
      }
    }

    if (cfg.quiet && any_selected_global) break;
  }

  if (cfg.has_error && !cfg.quiet) return 2;
  return any_selected_global ? 0 : 1;
}

}  // namespace grep_pipeline

REGISTER_COMMAND(
    grep, "grep", "grep [OPTION]... PATTERNS [FILE]...",
    "Search for PATTERNS in each FILE.\n"
    "PATTERNS can contain multiple patterns separated by newlines.\n"
    "With no FILE, read '-' unless recursive mode is selected.",
    "  grep -i 'hello world' menu.h main.c\n"
    "  grep -n -E 'foo|bar' file.txt\n"
    "  grep -r pattern .\n"
    "  grep -F -x exact_line file.txt",
    "sed(1), awk(1), find(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    GREP_OPTIONS) {
  using namespace grep_pipeline;

  auto config = build_config(ctx);
  if (!config) {
    cp::report_error(config, L"grep");
    return 2;
  }

  return process(*config);
}
