// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

static auto grep_is_terminal(FILE* stream) -> bool {
  int fd = _fileno(stream);
  if (fd < 0) return false;

  DWORD mode = 0;
  auto handle = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
  if (handle == INVALID_HANDLE_VALUE) return false;
  if (GetConsoleMode(handle, &mode) != 0) return true;

  if (GetFileType(handle) != FILE_TYPE_PIPE) return false;

  constexpr DWORD kPipeNameBufferBytes =
      sizeof(FILE_NAME_INFO) + (MAX_PATH * sizeof(wchar_t));
  std::vector<char> buffer(kPipeNameBufferBytes);
  auto* info = reinterpret_cast<FILE_NAME_INFO*>(buffer.data());
  if (!GetFileInformationByHandleEx(handle, FileNameInfo, info,
                                    static_cast<DWORD>(buffer.size()))) {
    return false;
  }

  std::wstring name(info->FileName, info->FileNameLength / sizeof(wchar_t));
  std::ranges::transform(name, name.begin(), [](wchar_t ch) {
    return static_cast<wchar_t>(std::towlower(ch));
  });

  return name.find(L"pty") != std::wstring::npos &&
         (name.find(L"msys") != std::wstring::npos ||
          name.find(L"cygwin") != std::wstring::npos);
}

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
 * - @a -P, @a --perl-regexp: PATTERNS are Perl regular expressions
 * [IMPLEMENTED]
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
 * - @a --binary-files: Assume that binary files are TYPE [IMPLEMENTED]
 * - @a
 * -a, @a --text: Equivalent to --binary-files=text [IMPLEMENTED]
 * - @a -I:
 * Equivalent to --binary-files=without-match [IMPLEMENTED]
 * - @a -d, @a
 * --directories: How to handle directories: read, recurse, skip [IMPLEMENTED]
 * - @a -D, @a --devices: How to handle devices/FIFOs/sockets [IMPLEMENTED]
 * -
 * @a -r, @a --recursive: Like --directories=recurse [IMPLEMENTED]
 * - @a -R, @a --dereference-recursive: Like -r but follow symlinks
 *
 * [IMPLEMENTED]
 * - @a --include: Search only files that match GLOB
 * [IMPLEMENTED]
 * - @a --exclude: Skip files that match GLOB [IMPLEMENTED]
 * - @a --exclude-from: Skip files from patterns in FILE [IMPLEMENTED]
 * - @a
 * --exclude-dir: Skip directories that match GLOB [IMPLEMENTED]
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
 * - @a -u, @a --unix-byte-offsets: Report Unix-style byte offsets [DIFFERS:
 * accepted but no effect]
 * - @a -U, @a --binary: Do not strip CR at EOL [IMPLEMENTED]
 */
auto constexpr GREP_OPTIONS = std::array{
    OPTION("-E", "--extended-regexp",
           "PATTERNS are extended regular expressions"),
    OPTION("-F", "--fixed-strings", "PATTERNS are strings"),
    OPTION("-G", "--basic-regexp", "PATTERNS are basic regular expressions"),
    OPTION("-P", "--perl-regexp", "PATTERNS are Perl regular expressions"),
    OPTION("-e", "--regexp", "use PATTERNS for matching", STRING_TYPE),
    OPTION("-f", "--file", "take PATTERNS from FILE", STRING_TYPE),
    // [GNU] -y: obsolete synonym for -i; hidden like GNU
    OPTION("-y", "", "", BOOL_TYPE),
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
    OPTION("", "--binary-files", "assume that binary files are TYPE",
           STRING_TYPE),
    OPTION("-a", "--text", "equivalent to --binary-files=text"),
    OPTION("-I", "", "equivalent to --binary-files=without-match"),
    OPTION("-d", "--directories",
           "how to handle directories: read, recurse, skip", STRING_TYPE),
    OPTION("-D", "--devices", "how to handle devices/FIFOs/sockets: read, skip",
           STRING_TYPE),
    OPTION("-r", "--recursive", "like --directories=recurse"),
    OPTION("-R", "--dereference-recursive", "like -r but follow symlinks"),
    OPTION("", "--include", "search only files that match GLOB", STRING_TYPE),
    OPTION("", "--exclude", "skip files that match GLOB", STRING_TYPE),
    OPTION("", "--exclude-from", "skip files from patterns in FILE",
           STRING_TYPE),
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
    OPTION("-NUM", "", "same as --context=NUM", INT_TYPE),
    OPTION("", "--group-separator", "print separator between groups",
           STRING_TYPE),
    OPTION("", "--no-group-separator", "do not print group separator"),
    OPTION(
        "", "--color",
        "highlight matching strings; WHEN can be 'always', 'never', or 'auto'",
        OPTIONAL_STRING_TYPE),
    OPTION(
        "", "--colour",
        "highlight matching strings; WHEN can be 'always', 'never', or 'auto'",
        OPTIONAL_STRING_TYPE),
    OPTION("-u", "--unix-byte-offsets", "report Unix-style byte offsets"),
    // [DIFFERS] - Unix byte offsets not applicable on Windows
    OPTION("-U", "--binary", "do not strip CR at EOL")};

namespace grep_pipeline {
namespace cp = core::pipeline;

enum class PatternMode { BasicRegex, ExtendedRegex, Fixed, PerlRegex };
enum class BinaryMode { Binary, Text, WithoutMatch };

struct MatchPiece {
  size_t begin = 0;
  size_t end = 0;
};

struct FastClassRepeat {
  std::array<bool, 256> members{};
};

struct Pattern {
  std::string raw;
  std::string lowered;
  std::optional<portable_regex::Pattern> regex;
  std::vector<std::string> fast_literals;
  std::vector<std::string> lowered_fast_literals;
  std::string fast_prefix;
  std::string lowered_fast_prefix;
  std::optional<FastClassRepeat> fast_class_repeat;
};
struct FixedMatcherNode {
  std::array<int, 256> next{};
  int fail = 0;
  bool output = false;

  FixedMatcherNode() { next.fill(-1); }
};
struct FixedMatcher {
  bool ignore_case = false;
  bool empty_matches = false;
  std::vector<FixedMatcherNode> nodes;

  FixedMatcher() { nodes.emplace_back(); }
};

struct FileSelectionRule {
  bool include = false;
  std::string pattern;
};

struct ColorConfig {
  std::string matched_selected = "01;31";
  std::string matched_context = "01;31";
  std::string selected_line;
  std::string context_line;
  std::string filename = "35";
  std::string line_number = "32";
  std::string byte_offset = "32";
  std::string separator = "36";
  bool reverse_line_colors_on_invert = false;
  bool no_erase = false;
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
  bool line_buffered = false;
  bool with_filename = false;
  bool no_filename = false;
  std::string label;
  bool only_matching = false;
  bool quiet = false;
  std::string directories = "read";
  std::string devices = "read";
  bool files_without_match = false;
  bool files_with_matches = false;
  bool count_only = false;
  bool null_after_filename = false;
  bool recursive = false;
  bool recursive_directory_operand = false;
  bool dereference_recursive = false;
  SmallVector<Pattern, 32> patterns;
  std::optional<FixedMatcher> fixed_matcher;
  SmallVector<std::string, 64> files;
  bool has_error = false;
  int before_context = 0;
  int after_context = 0;
  bool context_requested = false;
  bool color = false;
  ColorConfig color_config;
  BinaryMode binary_mode = BinaryMode::Binary;
  SmallVector<FileSelectionRule, 32> file_selection_rules;
  SmallVector<std::string, 16> exclude_dir_patterns;
  std::string group_separator = "--";
  bool no_group_separator = false;
  bool initial_tab = false;
  bool preserve_cr = false;
};

auto getenv_string(const char* name) -> std::string {
  if (const char* value = std::getenv(name); value != nullptr) {
    return std::string(value);
  }
  return {};
}

auto build_color_config(std::string_view grep_color,
                        std::string_view grep_colors) -> ColorConfig {
  ColorConfig config;
  if (!grep_color.empty()) {
    config.matched_selected = std::string(grep_color);
    config.matched_context = std::string(grep_color);
  }

  size_t start = 0;
  while (start <= grep_colors.size()) {
    size_t end = grep_colors.find(':', start);
    if (end == std::string_view::npos) end = grep_colors.size();
    std::string_view item = grep_colors.substr(start, end - start);
    if (item == "ne") {
      config.no_erase = true;
    } else if (item == "rv") {
      config.reverse_line_colors_on_invert = true;
    } else if (auto pos = item.find('='); pos != std::string_view::npos) {
      std::string_view key = item.substr(0, pos);
      std::string value(item.substr(pos + 1));
      if (key == "mt") {
        config.matched_selected = value;
        config.matched_context = value;
      } else if (key == "ms") {
        config.matched_selected = value;
      } else if (key == "mc") {
        config.matched_context = value;
      } else if (key == "fn") {
        config.filename = value;
      } else if (key == "ln") {
        config.line_number = value;
      } else if (key == "bn") {
        config.byte_offset = value;
      } else if (key == "se") {
        config.separator = value;
      } else if (key == "sl") {
        config.selected_line = value;
      } else if (key == "cx") {
        config.context_line = value;
      }
    }
    if (end == grep_colors.size()) break;
    start = end + 1;
  }

  return config;
}

auto append_sgr_prefix(std::string& out, const ColorConfig& cfg,
                       std::string_view sgr) -> void {
  if (sgr.empty()) return;
  out.append("\033[");
  out.append(sgr);
  out.append("m");
  if (!cfg.no_erase) out.append("\033[K");
}

auto append_sgr_suffix(std::string& out, const ColorConfig& cfg,
                       std::string_view sgr) -> void {
  if (sgr.empty()) return;
  out.append("\033[m");
  if (!cfg.no_erase) out.append("\033[K");
}

auto append_colored(std::string& out, const ColorConfig& cfg,
                    std::string_view sgr, std::string_view text) -> void {
  append_sgr_prefix(out, cfg, sgr);
  out.append(text);
  append_sgr_suffix(out, cfg, sgr);
}

auto append_colored_char(std::string& out, const ColorConfig& cfg,
                         std::string_view sgr, char ch) -> void {
  append_sgr_prefix(out, cfg, sgr);
  out.push_back(ch);
  append_sgr_suffix(out, cfg, sgr);
}

auto is_extended_plain_literal(char ch) -> bool {
  switch (ch) {
    case '.':
    case '[':
    case ']':
    case '\\':
    case '(':
    case ')':
    case '*':
    case '+':
    case '?':
    case '{':
    case '}':
    case '|':
    case '^':
    case '$':
      return false;
    default:
      return true;
  }
}

auto append_escaped_extended_literal(std::string_view pattern, size_t& pos,
                                     std::string& out) -> bool {
  if (pattern[pos] != '\\' || pos + 1 >= pattern.size()) return false;
  const unsigned char escaped = static_cast<unsigned char>(pattern[pos + 1]);
  if (std::isalnum(escaped) != 0 || escaped == '\n') return false;
  out.push_back(static_cast<char>(escaped));
  pos += 2;
  return true;
}

auto parse_extended_literal_group(std::string_view pattern, size_t& pos)
    -> std::optional<std::vector<std::string>> {
  if (pattern[pos] != '(') return std::nullopt;
  ++pos;

  std::vector<std::string> alternatives;
  std::string current;
  while (pos < pattern.size()) {
    const char ch = pattern[pos];
    if (ch == ')') {
      if (current.empty()) return std::nullopt;
      alternatives.push_back(current);
      ++pos;
      return alternatives;
    }
    if (ch == '|') {
      if (current.empty()) return std::nullopt;
      alternatives.push_back(current);
      current.clear();
      ++pos;
      continue;
    }
    if (ch == '\\') {
      if (!append_escaped_extended_literal(pattern, pos, current)) {
        return std::nullopt;
      }
      continue;
    }
    if (!is_extended_plain_literal(ch)) return std::nullopt;
    current.push_back(ch);
    ++pos;
  }

  return std::nullopt;
}

auto append_literal_expansion(std::vector<std::string>& expanded,
                              const std::vector<std::string>& alternatives)
    -> bool {
  constexpr size_t kMaxFastLiteralExpansions = 128;
  if (alternatives.empty() ||
      expanded.size() > kMaxFastLiteralExpansions / alternatives.size()) {
    return false;
  }

  std::vector<std::string> next;
  next.reserve(expanded.size() * alternatives.size());
  for (const auto& prefix : expanded) {
    for (const auto& alternative : alternatives) {
      std::string value = prefix;
      value.append(alternative);
      next.push_back(std::move(value));
    }
  }
  expanded = std::move(next);
  return true;
}

auto extended_regex_literal_candidates(std::string_view pattern)
    -> std::optional<std::vector<std::string>> {
  std::vector<std::string> expanded{std::string{}};
  std::string literal_run;
  bool saw_regex_group = false;

  auto flush_literal_run = [&]() -> bool {
    if (literal_run.empty()) return true;
    std::vector<std::string> one{literal_run};
    literal_run.clear();
    return append_literal_expansion(expanded, one);
  };

  size_t pos = 0;
  while (pos < pattern.size()) {
    const char ch = pattern[pos];
    if (ch == '(') {
      if (!flush_literal_run()) return std::nullopt;
      auto alternatives = parse_extended_literal_group(pattern, pos);
      if (!alternatives) return std::nullopt;
      if (!append_literal_expansion(expanded, *alternatives)) {
        return std::nullopt;
      }
      saw_regex_group = true;
      continue;
    }
    if (ch == '\\') {
      if (!append_escaped_extended_literal(pattern, pos, literal_run)) {
        return std::nullopt;
      }
      continue;
    }
    if (!is_extended_plain_literal(ch)) return std::nullopt;
    literal_run.push_back(ch);
    ++pos;
  }

  if (!flush_literal_run()) return std::nullopt;
  if (!saw_regex_group && expanded.size() == 1 && expanded.front() == pattern) {
    return expanded;
  }
  return expanded;
}

auto add_class_member(FastClassRepeat& repeat, unsigned char ch,
                      bool ignore_case) -> void {
  repeat.members[ch] = true;
  if (!ignore_case) return;
  repeat.members[static_cast<unsigned char>(std::tolower(ch))] = true;
  repeat.members[static_cast<unsigned char>(std::toupper(ch))] = true;
}

auto add_posix_class(FastClassRepeat& repeat, std::string_view name,
                     bool ignore_case) -> bool {
  auto include_if = [&](auto pred) {
    for (int ch = 0; ch < 256; ++ch) {
      auto byte = static_cast<unsigned char>(ch);
      if (pred(byte)) add_class_member(repeat, byte, ignore_case);
    }
  };

  if (name == "digit") {
    include_if([](unsigned char ch) { return std::isdigit(ch) != 0; });
    return true;
  }
  if (name == "alnum") {
    include_if([](unsigned char ch) { return std::isalnum(ch) != 0; });
    return true;
  }
  if (name == "alpha") {
    include_if([](unsigned char ch) { return std::isalpha(ch) != 0; });
    return true;
  }
  if (name == "lower") {
    include_if([](unsigned char ch) { return std::islower(ch) != 0; });
    return true;
  }
  if (name == "upper") {
    include_if([](unsigned char ch) { return std::isupper(ch) != 0; });
    return true;
  }
  if (name == "xdigit") {
    include_if([](unsigned char ch) { return std::isxdigit(ch) != 0; });
    return true;
  }
  return false;
}

auto parse_simple_bracket_class(std::string_view pattern, size_t& pos,
                                bool ignore_case)
    -> std::optional<FastClassRepeat> {
  if (pos >= pattern.size() || pattern[pos] != '[') return std::nullopt;
  ++pos;
  if (pos < pattern.size() && pattern[pos] == '^') return std::nullopt;

  FastClassRepeat repeat;
  bool saw_member = false;
  while (pos < pattern.size()) {
    if (pattern[pos] == ']' && saw_member) {
      ++pos;
      return repeat;
    }

    if (pattern[pos] == '[' && pos + 1 < pattern.size() &&
        pattern[pos + 1] == ':') {
      size_t end = pattern.find(":]", pos + 2);
      if (end == std::string_view::npos) return std::nullopt;
      if (!add_posix_class(repeat, pattern.substr(pos + 2, end - pos - 2),
                           ignore_case)) {
        return std::nullopt;
      }
      saw_member = true;
      pos = end + 2;
      continue;
    }

    unsigned char first = static_cast<unsigned char>(pattern[pos]);
    if (pattern[pos] == '\\' && pos + 1 < pattern.size()) {
      ++pos;
      first = static_cast<unsigned char>(pattern[pos]);
    }
    ++pos;

    if (pos + 1 < pattern.size() && pattern[pos] == '-' &&
        pattern[pos + 1] != ']') {
      ++pos;
      unsigned char last = static_cast<unsigned char>(pattern[pos]);
      if (pattern[pos] == '\\' && pos + 1 < pattern.size()) {
        ++pos;
        last = static_cast<unsigned char>(pattern[pos]);
      }
      ++pos;
      if (last < first) return std::nullopt;
      for (int ch = first; ch <= last; ++ch) {
        add_class_member(repeat, static_cast<unsigned char>(ch), ignore_case);
      }
      saw_member = true;
      continue;
    }

    add_class_member(repeat, first, ignore_case);
    saw_member = true;
  }
  return std::nullopt;
}

auto extended_regex_prefixed_class_repeat(std::string_view pattern,
                                          bool ignore_case)
    -> std::optional<std::pair<std::string, FastClassRepeat>> {
  std::string prefix;
  size_t pos = 0;
  while (pos < pattern.size()) {
    const char ch = pattern[pos];
    if (ch == '\\') {
      size_t escaped_pos = pos;
      if (!append_escaped_extended_literal(pattern, escaped_pos, prefix)) {
        break;
      }
      pos = escaped_pos;
      continue;
    }
    if (!is_extended_plain_literal(ch)) break;
    prefix.push_back(ch);
    ++pos;
  }
  if (prefix.empty()) return std::nullopt;

  auto repeat = parse_simple_bracket_class(pattern, pos, ignore_case);
  if (!repeat) return std::nullopt;
  if (pos >= pattern.size() || pattern[pos] != '+') return std::nullopt;
  ++pos;
  if (pos != pattern.size()) return std::nullopt;

  return std::pair{prefix, *repeat};
}

auto extended_regex_required_prefix(std::string_view pattern)
    -> std::optional<std::string> {
  std::string prefix;
  size_t pos = 0;
  while (pos < pattern.size()) {
    const char ch = pattern[pos];
    if (ch == static_cast<char>(0x5C)) {
      size_t escaped_pos = pos;
      if (!append_escaped_extended_literal(pattern, escaped_pos, prefix)) {
        break;
      }
      pos = escaped_pos;
      continue;
    }
    if (!is_extended_plain_literal(ch)) break;
    prefix.push_back(ch);
    ++pos;
  }
  if (prefix.empty()) return std::nullopt;

  if (pos < pattern.size()) {
    const char ch = pattern[pos];
    if (ch == static_cast<char>(0x2A) || ch == static_cast<char>(0x3F) ||
        ch == static_cast<char>(0x7B)) {
      return std::nullopt;
    }
  }

  bool escaped = false;
  bool in_class = false;
  int group_depth = 0;
  for (size_t i = pos; i < pattern.size(); ++i) {
    const char ch = pattern[i];
    if (escaped) {
      escaped = false;
      continue;
    }
    if (ch == static_cast<char>(0x5C)) {
      escaped = true;
      continue;
    }
    if (in_class) {
      if (ch == static_cast<char>(0x5D)) in_class = false;
      continue;
    }
    if (ch == static_cast<char>(0x5B)) {
      in_class = true;
      continue;
    }
    if (ch == static_cast<char>(0x28)) {
      ++group_depth;
      continue;
    }
    if (ch == static_cast<char>(0x29)) {
      if (group_depth > 0) --group_depth;
      continue;
    }
    if (ch == static_cast<char>(0x7C) && group_depth == 0) {
      return std::nullopt;
    }
  }

  return prefix;
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

auto trim_text_record(std::string_view line, char delim, const Config& cfg)
    -> std::string_view {
  if (!cfg.preserve_cr && delim == '\n' && !line.empty() &&
      line.back() == '\r') {
    return line.substr(0, line.size() - 1);
  }
  return line;
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
auto fixed_match_byte(unsigned char ch, bool ignore_case) -> unsigned char {
  if (!ignore_case) return ch;
  return static_cast<unsigned char>(std::tolower(ch));
}
auto fixed_matcher_add(FixedMatcher& matcher, std::string_view pattern)
    -> void {
  if (pattern.empty()) {
    matcher.empty_matches = true;
    return;
  }
  int state = 0;
  for (unsigned char raw_ch : pattern) {
    unsigned char ch = fixed_match_byte(raw_ch, matcher.ignore_case);
    int next = matcher.nodes[state].next[ch];
    if (next < 0) {
      next = static_cast<int>(matcher.nodes.size());
      matcher.nodes[state].next[ch] = next;
      matcher.nodes.emplace_back();
    }
    state = next;
  }
  matcher.nodes[state].output = true;
}
auto fixed_matcher_prepare(FixedMatcher& matcher) -> void {
  std::vector<int> queue;
  queue.reserve(matcher.nodes.size());
  for (int ch = 0; ch < 256; ++ch) {
    int next = matcher.nodes[0].next[ch];
    if (next >= 0) {
      matcher.nodes[next].fail = 0;
      queue.push_back(next);
    } else {
      matcher.nodes[0].next[ch] = 0;
    }
  }
  for (size_t head = 0; head < queue.size(); ++head) {
    const int state = queue[head];
    const int fail = matcher.nodes[state].fail;
    matcher.nodes[state].output =
        matcher.nodes[state].output || matcher.nodes[fail].output;
    for (int ch = 0; ch < 256; ++ch) {
      int next = matcher.nodes[state].next[ch];
      if (next >= 0) {
        matcher.nodes[next].fail = matcher.nodes[fail].next[ch];
        queue.push_back(next);
      } else {
        matcher.nodes[state].next[ch] = matcher.nodes[fail].next[ch];
      }
    }
  }
}
auto build_fixed_matcher(const Config& cfg) -> FixedMatcher {
  FixedMatcher matcher;
  matcher.ignore_case = cfg.ignore_case;
  for (const auto& pattern : cfg.patterns) {
    if (cfg.mode == PatternMode::Fixed) {
      fixed_matcher_add(matcher, pattern.raw);
      continue;
    }
    for (const auto& literal : pattern.fast_literals) {
      fixed_matcher_add(matcher, literal);
    }
  }
  fixed_matcher_prepare(matcher);
  return matcher;
}
auto fixed_matcher_matches(const FixedMatcher& matcher, std::string_view line)
    -> bool {
  if (matcher.empty_matches) return true;
  int state = 0;
  for (unsigned char raw_ch : line) {
    unsigned char ch = fixed_match_byte(raw_ch, matcher.ignore_case);
    state = matcher.nodes[state].next[ch];
    if (matcher.nodes[state].output) return true;
  }
  return false;
}

auto compile_pattern(PatternMode mode, bool ignore_case, std::string_view raw)
    -> cp::Result<Pattern> {
  Pattern p;
  p.raw = std::string(raw);
  p.lowered = ascii_lower_copy(raw);

  if (mode == PatternMode::Fixed) return p;

  auto syntax = portable_regex::Syntax::Basic;
  if (mode == PatternMode::ExtendedRegex) {
    syntax = portable_regex::Syntax::Extended;
  } else if (mode == PatternMode::PerlRegex) {
    syntax = portable_regex::Syntax::Perl;
  }
  auto compiled = portable_regex::compile(syntax, p.raw, ignore_case);
  if (!compiled) {
    return std::unexpected("invalid regular expression: " + compiled.error);
  }
  p.regex.emplace(std::move(compiled.pattern));
  if (mode == PatternMode::ExtendedRegex) {
    if (auto quick = extended_regex_prefixed_class_repeat(p.raw, ignore_case)) {
      p.fast_prefix = quick->first;
      p.lowered_fast_prefix = ascii_lower_copy(quick->first);
      p.fast_class_repeat = quick->second;
    } else if (auto prefix = extended_regex_required_prefix(p.raw)) {
      p.fast_prefix = *prefix;
      p.lowered_fast_prefix = ascii_lower_copy(*prefix);
    }
    if (auto literals = extended_regex_literal_candidates(p.raw)) {
      p.fast_literals = std::move(*literals);
      p.lowered_fast_literals.reserve(p.fast_literals.size());
      for (const auto& literal : p.fast_literals) {
        p.lowered_fast_literals.push_back(ascii_lower_copy(literal));
      }
    }
  }

  return p;
}

auto load_patterns_from_file(const std::string& path)
    -> cp::Result<std::vector<std::string>> {
  std::string buf;
  if (path == "-") {
    buf = read_text_stream(std::cin);
  } else {
    auto in = file_io::open_binary_file(path);
    if (!in.is_open()) {
      return std::unexpected("cannot open pattern file '" + path + "'");
    }
    buf = read_text_stream(in);
  }
  if (buf.empty()) {
    return std::vector<std::string>{};
  }

  std::vector<std::string> lines;
  size_t start = 0;
  while (start < buf.size()) {
    size_t pos = buf.find('\n', start);
    if (pos == std::string::npos) {
      lines.emplace_back(buf.substr(start));
      break;
    }
    lines.emplace_back(buf.substr(start, pos - start));
    start = pos + 1;
  }

  for (auto& line : lines) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
  }
  return lines;
}

auto build_config(const CommandContext<GREP_OPTIONS.size()>& ctx,
                  PatternMode default_mode = PatternMode::BasicRegex)
    -> cp::Result<Config> {
  Config cfg;
  cfg.mode = default_mode;

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= GREP_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];

    if (option_matches(meta, "-F", "--fixed-strings")) {
      cfg.mode = PatternMode::Fixed;
      continue;
    }
    if (option_matches(meta, "-E", "--extended-regexp")) {
      cfg.mode = PatternMode::ExtendedRegex;
      continue;
    }
    if (option_matches(meta, "-G", "--basic-regexp")) {
      cfg.mode = PatternMode::BasicRegex;
      continue;
    }
    if (option_matches(meta, "-P", "--perl-regexp")) {
      cfg.mode = PatternMode::PerlRegex;
      continue;
    }
    if (option_matches(meta, "-i", "--ignore-case") ||
        option_matches(meta, "-y", "")) {
      cfg.ignore_case = true;
      continue;
    }
    if (option_matches(meta, "", "--no-ignore-case")) {
      cfg.ignore_case = false;
      continue;
    }
    if (option_matches(meta, "-H", "--with-filename")) {
      cfg.with_filename = true;
      cfg.no_filename = false;
      continue;
    }
    if (option_matches(meta, "-h", "--no-filename")) {
      cfg.no_filename = true;
      cfg.with_filename = false;
      continue;
    }
  }

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
  cfg.line_buffered = ctx.get<bool>("--line-buffered", false);
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

  std::string binary_files;
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= GREP_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];

    if (option_matches(meta, "", "--binary-files")) {
      if (const auto* value = std::get_if<std::string>(&occurrence.value)) {
        binary_files = *value;
      }
      continue;
    }
    if (option_matches(meta, "-a", "--text")) {
      (void)ctx.has("--unix-byte-offsets");
      binary_files = "text";
      continue;
    }
    if (option_matches(meta, "-I", "")) {
      binary_files = "without-match";
      continue;
    }
  }
  if (!binary_files.empty()) {
    if (binary_files == "binary") {
      cfg.binary_mode = BinaryMode::Binary;
    } else if (binary_files == "text") {
      cfg.binary_mode = BinaryMode::Text;
    } else if (binary_files == "without-match") {
      cfg.binary_mode = BinaryMode::WithoutMatch;
    } else {
      return std::unexpected("invalid binary-files type '" + binary_files +
                             "'");
    }
  }

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
  if (cfg.directories != "read" && cfg.directories != "recurse" &&
      cfg.directories != "skip") {
    return std::unexpected("invalid directories action '" + cfg.directories +
                           "'");
  }

  cfg.devices = ctx.get<std::string>("--devices", "");
  if (cfg.devices.empty()) cfg.devices = ctx.get<std::string>("-D", "");
  if (cfg.devices.empty()) cfg.devices = "read";
  if (cfg.devices != "read" && cfg.devices != "skip") {
    return std::unexpected("invalid devices action '" + cfg.devices + "'");
  }

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= GREP_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    const auto* value = std::get_if<int>(&occurrence.value);
    if (!value) continue;

    if (option_matches(meta, "-B", "--before-context")) {
      cfg.context_requested = true;
      cfg.before_context = *value;
      continue;
    }
    if (option_matches(meta, "-A", "--after-context")) {
      cfg.context_requested = true;
      cfg.after_context = *value;
      continue;
    }
    if (option_matches(meta, "-C", "--context")) {
      cfg.context_requested = true;
      cfg.before_context = *value;
      cfg.after_context = *value;
      continue;
    }
    if (option_matches(meta, "-NUM", "")) {
      cfg.context_requested = true;
      cfg.before_context = *value;
      cfg.after_context = *value;
      continue;
    }
  }

  std::optional<std::string> color_opt;
  for (const auto& occurrence :
       ctx.string_occurrences({"--color", "--colour"})) {
    color_opt = occurrence.value.empty() ? "auto" : occurrence.value;
  }
  std::string color_mode = color_opt.value_or("auto");
  if (color_mode == "always") {
    cfg.color = true;
  } else if (color_mode == "auto") {
    cfg.color = grep_is_terminal(stdout);
  } else if (color_mode == "never") {
    cfg.color = false;
  } else {
    return std::unexpected("invalid color mode '" + color_mode + "'");
  }

  std::string grep_color = getenv_string("GREP_COLOR");
  std::string grep_colors = getenv_string("GREP_COLORS");
  cfg.color_config = build_color_config(grep_color, grep_colors);
  if (cfg.color && !grep_color.empty()) {
    safeErrorPrint("grep: warning: GREP_COLOR='");
    safeErrorPrint(grep_color);
    safeErrorPrint("' is deprecated; use GREP_COLORS='mt=");
    safeErrorPrint(grep_color);
    safeErrorPrint("'\n");
  }

  for (const auto& occurrence : ctx.string_occurrences(
           {"--include", "--exclude", "--exclude-from", "--exclude-dir"})) {
    if (occurrence.long_name == "--include") {
      if (!occurrence.value.empty()) {
        cfg.file_selection_rules.push_back(
            FileSelectionRule{true, occurrence.value});
      }
      continue;
    }
    if (occurrence.long_name == "--exclude") {
      if (!occurrence.value.empty()) {
        cfg.file_selection_rules.push_back(
            FileSelectionRule{false, occurrence.value});
      }
      continue;
    }
    if (occurrence.long_name == "--exclude-from") {
      auto exclude_patterns = load_patterns_from_file(occurrence.value);
      if (!exclude_patterns) return std::unexpected(exclude_patterns.error());
      for (const auto& pattern : *exclude_patterns) {
        if (!pattern.empty()) {
          cfg.file_selection_rules.push_back(FileSelectionRule{false, pattern});
        }
      }
      continue;
    }
    if (occurrence.long_name == "--exclude-dir") {
      if (!occurrence.value.empty()) {
        cfg.exclude_dir_patterns.push_back(occurrence.value);
      }
    }
  }

  cfg.group_separator = ctx.get<std::string>("--group-separator", "--");
  cfg.no_group_separator = ctx.get<bool>("--no-group-separator", false);
  cfg.initial_tab =
      ctx.get<bool>("--initial-tab", false) || ctx.get<bool>("-T", false);
  cfg.preserve_cr =
      ctx.get<bool>("--binary", false) || ctx.get<bool>("-U", false);

  SmallVector<std::string, 32> raw_patterns;
  auto regexp_options = ctx.get_all<std::string>("--regexp");
  auto pattern_file_options = ctx.get_all<std::string>("--file");
  bool pattern_source_provided =
      !regexp_options.empty() || !pattern_file_options.empty();

  for (const auto& p_e : regexp_options) {
    auto from_e = split_lines(p_e);
    for (const auto& p : from_e) raw_patterns.push_back(p);
  }

  for (const auto& p_file : pattern_file_options) {
    auto file_patterns = load_patterns_from_file(p_file);
    if (!file_patterns) return std::unexpected(file_patterns.error());
    for (const auto& p : *file_patterns) raw_patterns.push_back(p);
  }

  std::vector<std::string> positionals;
  for (auto p : ctx.positionals) positionals.emplace_back(p);

  if (raw_patterns.empty() && !pattern_source_provided) {
    if (positionals.empty()) {
      return std::unexpected("missing PATTERNS");
    }
    auto split = split_lines(positionals.front());
    for (const auto& p : split) raw_patterns.push_back(p);
    positionals.erase(positionals.begin(),
                      positionals.begin() + 1);  // Remove first element
  }

  if (raw_patterns.empty() && !pattern_source_provided) {
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

auto flush_if_line_buffered(const Config& cfg) -> void {
  if (cfg.line_buffered) {
    ::fflush(stdout);
  }
}

auto collect_prefixed_regex_matches(std::string_view line, const Pattern& p,
                                    const Config& cfg,
                                    std::string& lowered_line)
    -> std::vector<MatchPiece> {
  std::vector<MatchPiece> matches;
  if (!p.regex || p.fast_prefix.empty()) return matches;

  std::string_view haystack = line;
  std::string_view prefix = p.fast_prefix;
  if (cfg.ignore_case) {
    if (lowered_line.empty() && !line.empty()) {
      lowered_line = ascii_lower_copy(line);
    }
    haystack = lowered_line;
    prefix = p.lowered_fast_prefix;
  }

  size_t cursor = 0;
  while (cursor <= haystack.size()) {
    size_t pos = haystack.find(prefix, cursor);
    if (pos == std::string_view::npos) break;

    auto match = p.regex->match_at(line, pos);
    if (match) {
      MatchPiece m{match->begin, match->end};
      if (!cfg.word_regexp || word_boundary_ok(line, m.begin, m.end)) {
        matches.push_back(m);
      }
      cursor = m.end > m.begin ? m.end : m.begin + 1;
    } else {
      cursor = pos + 1;
    }
  }
  return matches;
}

auto collect_prefixed_class_repeat_matches(std::string_view line,
                                           const Pattern& p, const Config& cfg,
                                           std::string& lowered_line)
    -> std::vector<MatchPiece> {
  std::vector<MatchPiece> matches;
  if (!p.fast_class_repeat || p.fast_prefix.empty()) return matches;

  std::string_view haystack = line;
  std::string_view prefix = p.fast_prefix;
  if (cfg.ignore_case) {
    if (lowered_line.empty() && !line.empty()) {
      lowered_line = ascii_lower_copy(line);
    }
    haystack = lowered_line;
    prefix = p.lowered_fast_prefix;
  }

  size_t cursor = 0;
  while (cursor <= haystack.size()) {
    size_t pos = haystack.find(prefix, cursor);
    if (pos == std::string_view::npos) break;

    size_t end = pos + p.fast_prefix.size();
    if (end >= line.size()) {
      cursor = pos + 1;
      continue;
    }
    size_t repeat_start = end;
    while (
        end < line.size() &&
        p.fast_class_repeat->members[static_cast<unsigned char>(line[end])]) {
      ++end;
    }
    if (end == repeat_start) {
      cursor = pos + 1;
      continue;
    }

    MatchPiece m{pos, end};
    if (!cfg.word_regexp || word_boundary_ok(line, m.begin, m.end)) {
      matches.push_back(m);
    }
    cursor = end;
  }
  return matches;
}

auto find_first_prefixed_regex_match(std::string_view line, const Pattern& p,
                                     const Config& cfg,
                                     std::string& lowered_line)
    -> std::optional<MatchPiece> {
  if (!p.regex || p.fast_prefix.empty()) return std::nullopt;

  std::string_view haystack = line;
  std::string_view prefix = p.fast_prefix;
  if (cfg.ignore_case) {
    if (lowered_line.empty() && !line.empty()) {
      lowered_line = ascii_lower_copy(line);
    }
    haystack = lowered_line;
    prefix = p.lowered_fast_prefix;
  }

  size_t cursor = 0;
  while (cursor <= haystack.size()) {
    size_t pos = haystack.find(prefix, cursor);
    if (pos == std::string_view::npos) break;

    auto match = p.regex->match_at(line, pos);
    if (match) {
      MatchPiece m{match->begin, match->end};
      if (!cfg.word_regexp || word_boundary_ok(line, m.begin, m.end)) {
        return m;
      }
      cursor = m.end > m.begin ? m.end : m.begin + 1;
    } else {
      cursor = pos + 1;
    }
  }
  return std::nullopt;
}

auto find_first_prefixed_class_repeat_match(std::string_view line,
                                            const Pattern& p, const Config& cfg,
                                            std::string& lowered_line)
    -> std::optional<MatchPiece> {
  auto matches =
      collect_prefixed_class_repeat_matches(line, p, cfg, lowered_line);
  if (matches.empty()) return std::nullopt;
  return matches.front();
}

auto collect_matches_in_line(std::string_view line, const Config& cfg)
    -> std::vector<MatchPiece> {
  std::vector<MatchPiece> out;
  std::string lowered_line;

  for (const auto& p : cfg.patterns) {
    if (p.raw.empty()) {
      out.push_back(MatchPiece{0, 0});
      continue;
    }

    if (cfg.mode == PatternMode::Fixed) {
      if (cfg.line_regexp) {
        bool eq = cfg.ignore_case ? (ascii_lower_copy(line) == p.lowered)
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
            lowered_line = ascii_lower_copy(line);
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
      if (p.regex->matches_entire(line)) {
        out.push_back(MatchPiece{0, line.size()});
      }
      continue;
    }

    if (p.fast_class_repeat) {
      auto matches =
          collect_prefixed_class_repeat_matches(line, p, cfg, lowered_line);
      out.insert(out.end(), matches.begin(), matches.end());
      continue;
    }

    if (!p.fast_prefix.empty()) {
      auto matches = collect_prefixed_regex_matches(line, p, cfg, lowered_line);
      out.insert(out.end(), matches.begin(), matches.end());
      continue;
    }

    for (const auto& match : p.regex->find_all(line)) {
      MatchPiece m{match.begin, match.end};
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

auto find_first_match_in_line(std::string_view line, const Config& cfg)
    -> std::optional<MatchPiece> {
  std::string lowered_line;

  for (const auto& p : cfg.patterns) {
    if (p.raw.empty()) {
      MatchPiece m{0, 0};
      if (!cfg.word_regexp || word_boundary_ok(line, m.begin, m.end)) return m;
      continue;
    }

    if (cfg.mode == PatternMode::Fixed) {
      if (cfg.line_regexp) {
        bool eq = cfg.ignore_case ? (ascii_lower_copy(line) == p.lowered)
                                  : (line == p.raw);
        if (eq) return MatchPiece{0, line.size()};
        continue;
      }

      size_t cursor = 0;
      while (cursor <= line.size()) {
        size_t pos = std::string_view::npos;
        size_t len = 0;
        if (cfg.ignore_case) {
          if (lowered_line.empty() && !line.empty()) {
            lowered_line = ascii_lower_copy(line);
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
          return m;
        }
        cursor = pos + 1;
      }
      continue;
    }

    if (!p.regex.has_value()) continue;

    if (cfg.line_regexp) {
      if (p.regex->matches_entire(line)) return MatchPiece{0, line.size()};
      continue;
    }

    if (p.fast_class_repeat) {
      auto match =
          find_first_prefixed_class_repeat_match(line, p, cfg, lowered_line);
      if (match) return match;
      continue;
    }

    if (!p.fast_prefix.empty()) {
      auto match = find_first_prefixed_regex_match(line, p, cfg, lowered_line);
      if (match) return match;
      continue;
    }

    size_t cursor = 0;
    while (cursor <= line.size()) {
      auto match = p.regex->find_first(line, cursor);
      if (!match) break;
      MatchPiece m{match->begin, match->end};
      if (!cfg.word_regexp || word_boundary_ok(line, m.begin, m.end)) {
        return m;
      }
      cursor = match->end > match->begin ? match->end : match->begin + 1;
    }
  }

  return std::nullopt;
}

auto line_matches(std::string_view line, const Config& cfg) -> bool {
  return find_first_match_in_line(line, cfg).has_value();
}

auto append_prefix(std::string& out, const Config& cfg, bool show_filename,
                   std::string_view display_name, size_t line_no, size_t offset,
                   char separator = ':') -> void {
  if (show_filename) {
    if (cfg.color) {
      append_colored(out, cfg.color_config, cfg.color_config.filename,
                     display_name);
    } else {
      out.append(display_name);
    }
    if (cfg.color) {
      append_colored_char(out, cfg.color_config, cfg.color_config.separator,
                          separator);
    } else {
      out.push_back(separator);
    }
  }
  if (cfg.line_number) {
    if (cfg.color) {
      auto s = std::to_string(line_no);
      append_colored(out, cfg.color_config, cfg.color_config.line_number, s);
    } else {
      auto s = std::to_string(line_no);
      out.append(s);
    }
    if (cfg.color) {
      append_colored_char(out, cfg.color_config, cfg.color_config.separator,
                          separator);
    } else {
      out.push_back(separator);
    }
  }
  if (cfg.byte_offset) {
    auto s = std::to_string(offset);
    if (cfg.color) {
      append_colored(out, cfg.color_config, cfg.color_config.byte_offset, s);
    } else {
      out.append(s);
    }
    if (cfg.color) {
      append_colored_char(out, cfg.color_config, cfg.color_config.separator,
                          separator);
    } else {
      out.push_back(separator);
    }
  }
}

auto line_color_for_output(const Config& cfg, bool selected_line)
    -> const std::string& {
  bool use_selected_line_color = selected_line;
  if (cfg.invert_match && cfg.color_config.reverse_line_colors_on_invert) {
    use_selected_line_color = !use_selected_line_color;
  }
  return use_selected_line_color ? cfg.color_config.selected_line
                                 : cfg.color_config.context_line;
}

auto match_color_for_output(const Config& cfg, bool selected_line)
    -> const std::string& {
  return selected_line ? cfg.color_config.matched_selected
                       : cfg.color_config.matched_context;
}

auto append_line_colored_span(std::string& out, const ColorConfig& color_cfg,
                              std::string_view line_color,
                              std::string_view text) -> void {
  if (text.empty()) return;
  append_colored(out, color_cfg, line_color, text);
}

auto append_line_with_color(std::string& out, std::string_view line,
                            const std::vector<MatchPiece>& matches,
                            const Config& cfg, bool selected_line) -> void {
  if (!cfg.color) {
    out.append(line);
    return;
  }

  const auto& line_color = line_color_for_output(cfg, selected_line);
  const auto& match_color = match_color_for_output(cfg, selected_line);
  if (matches.empty() || match_color.empty()) {
    append_line_colored_span(out, cfg.color_config, line_color, line);
    return;
  }

  size_t pos = 0;
  for (const auto& m : matches) {
    size_t begin = std::min(m.begin, line.size());
    size_t end = std::min(m.end, line.size());
    if (end <= begin || end <= pos) continue;
    if (begin < pos) begin = pos;

    if (!line_color.empty())
      append_sgr_prefix(out, cfg.color_config, line_color);
    if (begin > pos) {
      out.append(line.substr(pos, begin - pos));
    }

    append_sgr_prefix(out, cfg.color_config, match_color);
    out.append(line.substr(begin, end - begin));
    append_sgr_suffix(out, cfg.color_config, match_color);
    pos = end;
  }

  if (pos < line.size()) {
    append_line_colored_span(out, cfg.color_config, line_color,
                             line.substr(pos));
  }
}

auto process_selected_record(std::string_view line, bool had_delim,
                             std::string_view display_name, bool show_filename,
                             size_t line_no, size_t offset, Config& cfg,
                             size_t& selected_count) -> bool {
  bool is_match = line_matches(line, cfg);
  bool selected = cfg.invert_match ? !is_match : is_match;
  if (!selected) return false;

  ++selected_count;
  if (cfg.quiet) return true;

  if (cfg.files_with_matches || cfg.files_without_match || cfg.count_only)
    return true;

  const char delim = cfg.null_data ? '\0' : '\n';
  std::string output_buf;
  output_buf.reserve(line.size() + 128);
  std::vector<MatchPiece> matches;

  if (cfg.only_matching && !cfg.invert_match) {
    matches = collect_matches_in_line(line, cfg);
    for (const auto& m : matches) {
      if (m.end <= m.begin) continue;
      output_buf.clear();
      append_prefix(output_buf, cfg, show_filename, display_name, line_no,
                    offset + m.begin);
      if (cfg.initial_tab) output_buf.push_back('\t');
      if (cfg.color) {
        append_colored(output_buf, cfg.color_config,
                       cfg.color_config.matched_selected,
                       line.substr(m.begin, m.end - m.begin));
      } else {
        output_buf.append(line.substr(m.begin, m.end - m.begin));
      }
      output_buf.append(1, delim);
      safePrint(output_buf);
      flush_if_line_buffered(cfg);
    }
  } else {
    append_prefix(output_buf, cfg, show_filename, display_name, line_no,
                  offset);
    if (cfg.initial_tab) output_buf.push_back('\t');
    if (cfg.color && !cfg.invert_match) {
      matches = collect_matches_in_line(line, cfg);
    }
    append_line_with_color(output_buf, line, matches, cfg, true);
    if (had_delim) {
      output_buf.append(1, delim);
    } else {
      output_buf.push_back(delim);
    }
    safePrint(output_buf);
    flush_if_line_buffered(cfg);
  }
  return true;
}

auto can_use_fixed_fast_path(const Config& cfg) -> bool {
  if (cfg.word_regexp || cfg.line_regexp || cfg.only_matching || cfg.color ||
      cfg.before_context != 0 || cfg.after_context != 0) {
    return false;
  }
  if (cfg.mode == PatternMode::Fixed) return true;
  if (cfg.mode != PatternMode::ExtendedRegex) return false;
  return std::all_of(
      cfg.patterns.begin(), cfg.patterns.end(),
      [](const Pattern& pattern) { return !pattern.fast_literals.empty(); });
}

auto fixed_find_ascii_case_insensitive(std::string_view haystack,
                                       std::string_view needle) -> bool {
  if (needle.empty()) return true;
  if (needle.size() > haystack.size()) return false;

  const auto needle_first =
      static_cast<char>(std::tolower(static_cast<unsigned char>(needle[0])));
  const size_t last_start = haystack.size() - needle.size();
  for (size_t start = 0; start <= last_start; ++start) {
    if (static_cast<char>(std::tolower(
            static_cast<unsigned char>(haystack[start]))) != needle_first) {
      continue;
    }

    size_t i = 1;
    for (; i < needle.size(); ++i) {
      const auto lhs = static_cast<char>(
          std::tolower(static_cast<unsigned char>(haystack[start + i])));
      if (lhs != needle[i]) break;
    }
    if (i == needle.size()) return true;
  }
  return false;
}

auto fixed_line_matches_fast(std::string_view line, const Config& cfg) -> bool {
  if (cfg.fixed_matcher) return fixed_matcher_matches(*cfg.fixed_matcher, line);
  for (const auto& p : cfg.patterns) {
    if (cfg.mode == PatternMode::Fixed) {
      if (p.raw.empty()) return true;
      if (cfg.ignore_case) {
        if (fixed_find_ascii_case_insensitive(line, p.lowered)) return true;
      } else if (line.find(p.raw) != std::string_view::npos) {
        return true;
      }
      continue;
    }

    const auto& literals =
        cfg.ignore_case ? p.lowered_fast_literals : p.fast_literals;
    for (const auto& literal : literals) {
      if (literal.empty()) return true;
      if (cfg.ignore_case) {
        if (fixed_find_ascii_case_insensitive(line, literal)) return true;
      } else if (line.find(literal) != std::string_view::npos) {
        return true;
      }
    }
  }
  return false;
}

auto process_fixed_fast_record(std::string_view line, bool /*had_delim*/,
                               std::string_view display_name,
                               bool show_filename, size_t line_no,
                               size_t offset, Config& cfg,
                               size_t& selected_count) -> bool {
  const bool is_match = fixed_line_matches_fast(line, cfg);
  const bool selected = cfg.invert_match ? !is_match : is_match;
  if (!selected) return false;

  ++selected_count;
  if (cfg.quiet || cfg.files_with_matches || cfg.files_without_match ||
      cfg.count_only) {
    return true;
  }

  const char delim = cfg.null_data ? '\0' : '\n';
  std::string output_buf;
  output_buf.reserve(line.size() + 128);
  append_prefix(output_buf, cfg, show_filename, display_name, line_no, offset);
  if (cfg.initial_tab) output_buf.push_back('\t');
  output_buf.append(line);
  output_buf.push_back(delim);
  safePrint(output_buf);
  flush_if_line_buffered(cfg);
  return true;
}

auto scan_text(const std::string& text, std::string_view display_name,
               bool show_filename, Config& cfg) -> std::pair<bool, size_t> {
  const char delim = cfg.null_data ? '\0' : '\n';
  auto records = split_records(text, delim);

  bool any_selected = false;
  size_t selected_count = 0;
  bool use_context = (cfg.before_context > 0 || cfg.after_context > 0) &&
                     !cfg.only_matching && !cfg.count_only &&
                     !cfg.files_with_matches && !cfg.files_without_match;

  if (!use_context) {
    for (size_t i = 0; i < records.size(); ++i) {
      // Check max_count before processing to handle -m 0 correctly.
      if (cfg.max_count >= 0 &&
          selected_count >= static_cast<size_t>(cfg.max_count))
        break;
      const auto [b, e] = records[i];
      std::string_view whole(text.data() + b, e - b);
      bool had_delim = !whole.empty() && whole.back() == delim;
      std::string_view line =
          had_delim ? whole.substr(0, whole.size() - 1) : whole;
      line = trim_text_record(line, delim, cfg);
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

  std::vector<size_t> selected_indices;
  for (size_t i = 0; i < records.size(); ++i) {
    // Check max_count before processing to handle -m 0 correctly.
    if (cfg.max_count >= 0 &&
        selected_count >= static_cast<size_t>(cfg.max_count))
      break;
    const auto [b, e] = records[i];
    std::string_view whole(text.data() + b, e - b);
    bool had_delim = !whole.empty() && whole.back() == delim;
    std::string_view line =
        had_delim ? whole.substr(0, whole.size() - 1) : whole;
    line = trim_text_record(line, delim, cfg);
    bool is_match = line_matches(line, cfg);
    bool selected = cfg.invert_match ? !is_match : is_match;
    if (selected) {
      selected_indices.push_back(i);
      ++selected_count;
      if (cfg.quiet) return {true, selected_count};
      if (cfg.max_count >= 0 &&
          static_cast<int>(selected_count) >= cfg.max_count)
        break;
    }
  }

  if (selected_indices.empty()) {
    return {false, 0};
  }

  std::vector<std::pair<size_t, size_t>> groups;
  for (size_t idx : selected_indices) {
    size_t start = (idx >= static_cast<size_t>(cfg.before_context))
                       ? idx - cfg.before_context
                       : 0;
    size_t end = idx + cfg.after_context;
    if (end >= records.size()) end = records.size() - 1;

    if (!groups.empty() && start <= groups.back().second + 1) {
      groups.back().second = std::max(groups.back().second, end);
    } else {
      groups.push_back({start, end});
    }
  }

  bool first_group = true;
  for (const auto& [gs, ge] : groups) {
    if (!first_group) {
      if (!cfg.no_group_separator) {
        safePrint(cfg.group_separator);
        safePrint("\n");
        flush_if_line_buffered(cfg);
      }
    }
    first_group = false;

    for (size_t i = gs; i <= ge; ++i) {
      const auto [b, e] = records[i];
      std::string_view whole(text.data() + b, e - b);
      bool had_delim = !whole.empty() && whole.back() == delim;
      std::string_view line =
          had_delim ? whole.substr(0, whole.size() - 1) : whole;
      line = trim_text_record(line, delim, cfg);

      bool selected = std::binary_search(selected_indices.begin(),
                                         selected_indices.end(), i);
      if (selected) {
        std::string line_buf;
        std::vector<MatchPiece> matches;
        if (cfg.color && !cfg.invert_match) {
          matches = collect_matches_in_line(line, cfg);
        }
        line_buf.reserve(line.size() + 128);
        append_prefix(line_buf, cfg, show_filename, display_name, i + 1, b);
        if (cfg.initial_tab) line_buf.push_back('\t');
        append_line_with_color(line_buf, line, matches, cfg, true);
        if (had_delim) {
          line_buf.append(1, delim);
        } else {
          line_buf.push_back(delim);
        }
        safePrint(line_buf);
        flush_if_line_buffered(cfg);
      } else {
        std::string line_buf;
        line_buf.reserve(line.size() + 128);
        std::vector<MatchPiece> matches;
        if (cfg.color && cfg.invert_match) {
          matches = collect_matches_in_line(line, cfg);
        }
        append_prefix(line_buf, cfg, show_filename, display_name, i + 1, b,
                      '-');
        if (cfg.initial_tab) line_buf.push_back('\t');
        append_line_with_color(line_buf, line, matches, cfg, false);
        if (had_delim) {
          line_buf.append(1, delim);
        } else {
          line_buf.push_back(delim);
        }
        safePrint(line_buf);
        flush_if_line_buffered(cfg);
      }
    }
  }

  any_selected = !selected_indices.empty();
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
      // Check max_count before processing to handle -m 0 correctly.
      if (cfg.max_count >= 0 &&
          selected_count >= static_cast<size_t>(cfg.max_count))
        break;

      std::string_view line(pending.data() + start, i - start);
      line = trim_text_record(line, delim, cfg);
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
    // Check max_count before processing trailing record to handle -m 0.
    if (cfg.max_count >= 0 &&
        selected_count >= static_cast<size_t>(cfg.max_count)) {
      return {any_selected, selected_count};
    }
    std::string_view line = trim_text_record(pending, delim, cfg);
    if (process_selected_record(line, false, display_name, show_filename,
                                line_no, base_offset, cfg, selected_count)) {
      any_selected = true;
    }
  }

  return {any_selected, selected_count};
}

auto read_file_binary(const std::string& path) -> cp::Result<std::string> {
  std::ifstream in(std::filesystem::path(utf8_to_wstring(
                       native_path::normalize_api_operand(path))),
                   std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + path + "'");
  }
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

auto read_file_text(const std::string& path) -> cp::Result<std::string> {
  std::ifstream in(std::filesystem::path(utf8_to_wstring(
                       native_path::normalize_api_operand(path))),
                   std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + path + "'");
  }
  return read_text_stream(in);
}

auto has_text_bom(std::string_view bytes) -> bool {
  return (bytes.size() >= 3 && static_cast<std::uint8_t>(bytes[0]) == 0xEF &&
          static_cast<std::uint8_t>(bytes[1]) == 0xBB &&
          static_cast<std::uint8_t>(bytes[2]) == 0xBF) ||
         (bytes.size() >= 2 && ((static_cast<std::uint8_t>(bytes[0]) == 0xFF &&
                                 static_cast<std::uint8_t>(bytes[1]) == 0xFE) ||
                                (static_cast<std::uint8_t>(bytes[0]) == 0xFE &&
                                 static_cast<std::uint8_t>(bytes[1]) == 0xFF)));
}

auto decode_text_bytes(const std::string& bytes) -> std::string {
  std::istringstream in(bytes);
  return read_text_stream(in);
}

auto contains_binary_bytes(std::string_view text, const Config& cfg) -> bool {
  return !cfg.null_data && text.find('\0') != std::string_view::npos;
}

auto should_stream_file_fast_path(const std::string& path, const Config& cfg)
    -> bool {
  if (cfg.binary_mode == BinaryMode::WithoutMatch) return false;

  std::ifstream in(std::filesystem::path(utf8_to_wstring(
                       native_path::normalize_api_operand(path))),
                   std::ios::binary);
  if (!in.is_open()) return true;

  std::array<char, 64 * 1024> sample{};
  in.read(sample.data(), static_cast<std::streamsize>(sample.size()));
  const auto got =
      static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0));
  std::string_view view(sample.data(), got);

  if (has_text_bom(view)) return false;
  if (cfg.binary_mode == BinaryMode::Binary &&
      contains_binary_bytes(view, cfg)) {
    return false;
  }
  return true;
}

auto scan_file_streaming(const std::string& path, std::string_view display_name,
                         bool show_filename, Config& cfg)
    -> cp::Result<std::pair<bool, size_t>> {
  std::ifstream in(std::filesystem::path(utf8_to_wstring(
                       native_path::normalize_api_operand(path))),
                   std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + path + "'");
  }
  return scan_stream(in, display_name, show_filename, cfg);
}

auto scan_fixed_file_fast(const std::string& path,
                          std::string_view display_name, bool show_filename,
                          Config& cfg) -> cp::Result<std::pair<bool, size_t>> {
  using FilePtr = std::unique_ptr<FILE, decltype(&::fclose)>;
  FilePtr file(_wfopen(utf8_to_wstring(path).c_str(), L"rb"), &::fclose);
  if (!file) {
    return std::unexpected("cannot open '" + path + "'");
  }

  const char delim = cfg.null_data ? '\0' : '\n';
  std::vector<char> chunk(1024 * 1024);
  std::string pending;
  pending.reserve(chunk.size() + 4096);

  size_t base_offset = 0;
  size_t line_no = 1;
  bool any_selected = false;
  size_t selected_count = 0;

  auto finish_selected = [&]() -> bool {
    any_selected = true;
    if (cfg.quiet) return true;
    if (cfg.max_count >= 0 &&
        static_cast<int>(selected_count) >= cfg.max_count) {
      return true;
    }
    if ((cfg.files_with_matches || cfg.files_without_match) &&
        !cfg.count_only) {
      return true;
    }
    return false;
  };

  while (true) {
    const size_t got = ::fread(chunk.data(), 1, chunk.size(), file.get());
    if (got > 0) {
      pending.append(chunk.data(), got);
    }

    size_t start = 0;
    while (start < pending.size()) {
      // Check max_count before processing to handle -m 0 correctly.
      if (cfg.max_count >= 0 &&
          selected_count >= static_cast<size_t>(cfg.max_count))
        break;
      const void* found =
          ::memchr(pending.data() + start, delim, pending.size() - start);
      if (found == nullptr) break;

      const auto* delim_ptr = static_cast<const char*>(found);
      const size_t delim_pos = static_cast<size_t>(delim_ptr - pending.data());
      size_t line_end = delim_pos;
      if (!cfg.preserve_cr && delim == '\n' && line_end > start &&
          pending[line_end - 1] == '\r') {
        --line_end;
      }

      std::string_view line(pending.data() + start, line_end - start);
      const size_t offset = base_offset + start;
      if (process_fixed_fast_record(line, true, display_name, show_filename,
                                    line_no, offset, cfg, selected_count)) {
        if (finish_selected()) return {{any_selected, selected_count}};
      }

      ++line_no;
      start = delim_pos + 1;
    }

    if (start > 0) {
      base_offset += start;
      pending.erase(0, start);
    }

    if (got < chunk.size()) {
      if (::ferror(file.get()) != 0) {
        return std::unexpected("read error in '" + path + "'");
      }
      break;
    }
  }

  if (!pending.empty()) {
    // Check max_count before processing trailing record to handle -m 0.
    if (cfg.max_count >= 0 &&
        selected_count >= static_cast<size_t>(cfg.max_count)) {
      return {{any_selected, selected_count}};
    }
    std::string_view line(pending.data(), pending.size());
    line = trim_text_record(line, delim, cfg);
    if (process_fixed_fast_record(line, false, display_name, show_filename,
                                  line_no, base_offset, cfg, selected_count)) {
      any_selected = true;
    }
  }

  return {{any_selected, selected_count}};
}

auto report_input_error(const Config& cfg, std::string_view message) -> void {
  if (cfg.no_messages || cfg.quiet) return;
  safeErrorPrint("grep: ");
  safeErrorPrint(message);
  safeErrorPrint("\n");
}

auto print_filename_terminator(const Config& cfg) -> void {
  if (cfg.null_after_filename) {
    safePrint(char{'\0'});
  } else {
    safePrint("\n");
  }
}

auto scan_binary_default(const std::string& content,
                         std::string_view display_name, bool show_filename,
                         Config& cfg) -> std::pair<bool, size_t> {
  if (!contains_binary_bytes(content, cfg)) {
    return scan_text(content, display_name, show_filename, cfg);
  }

  if (cfg.quiet || cfg.files_with_matches || cfg.files_without_match ||
      cfg.count_only) {
    return scan_text(content, display_name, show_filename, cfg);
  }

  Config probe_cfg = cfg;
  probe_cfg.quiet = true;
  auto scan_result = scan_text(content, display_name, show_filename, probe_cfg);
  if (scan_result.first) {
    safeErrorPrint("grep: ");
    safeErrorPrint(std::string(display_name));
    safeErrorPrint(": binary file matches\n");
  }
  return scan_result;
}

auto glob_matches_name_suffix(std::string_view pattern, std::string_view name)
    -> bool {
  std::string normalized_pattern(pattern);
  std::string normalized_name(name);
  std::replace(normalized_pattern.begin(), normalized_pattern.end(), '\\', '/');
  std::replace(normalized_name.begin(), normalized_name.end(), '\\', '/');
  while (!normalized_pattern.empty() && normalized_pattern.back() == '/') {
    normalized_pattern.pop_back();
  }

  const std::wstring wpattern = utf8_to_wstring(normalized_pattern);
  size_t start = 0;
  while (start <= normalized_name.size()) {
    auto suffix = normalized_name.substr(start);
    if (wildcard_match(wpattern, utf8_to_wstring(suffix))) {
      return true;
    }
    size_t slash = normalized_name.find('/', start);
    if (slash == std::string::npos) break;
    start = slash + 1;
  }
  return false;
}

auto matches_any_glob(const SmallVector<std::string, 16>& patterns,
                      std::string_view filename) -> bool {
  for (const auto& pattern : patterns) {
    if (glob_matches_name_suffix(pattern, filename)) {
      return true;
    }
  }
  return false;
}

auto should_search_file(const Config& cfg, std::string_view filename) -> bool {
  if (cfg.file_selection_rules.empty()) {
    return true;
  }

  bool selected_by_rule = !cfg.file_selection_rules.front().include;
  for (const auto& rule : cfg.file_selection_rules) {
    if (glob_matches_name_suffix(rule.pattern, filename)) {
      selected_by_rule = rule.include;
    }
  }
  return selected_by_rule;
}

auto append_search_path(Config& cfg, std::string_view input,
                        std::vector<std::string>& out) -> cp::Result<void> {
  std::string f(input);
  if (f == "-") {
    out.push_back(f);
    return {};
  }

  std::error_code ec;
  bool is_dir = std::filesystem::is_directory(f, ec);
  if (ec) {
    out.push_back(f);
    return {};
  }

  if (!is_dir) {
    bool is_regular = std::filesystem::is_regular_file(f, ec);
    if (!ec && !is_regular && cfg.devices == "skip") return {};
    if (!should_search_file(cfg, f)) return {};
    out.push_back(f);
    return {};
  }

  if (cfg.directories == "skip") return {};

  if (matches_any_glob(cfg.exclude_dir_patterns, f)) {
    return {};
  }

  if (cfg.directories == "recurse") {
    cfg.recursive_directory_operand = true;
    auto options = std::filesystem::directory_options::skip_permission_denied;
    if (cfg.dereference_recursive) {
      options |= std::filesystem::directory_options::follow_directory_symlink;
    }
    std::filesystem::recursive_directory_iterator it(f, options);
    const std::filesystem::recursive_directory_iterator end;
    for (; it != end; ++it) {
      const auto& e = *it;
      if (e.is_directory()) {
        std::string dirname = wstring_to_utf8(e.path().filename().wstring());
        if (matches_any_glob(cfg.exclude_dir_patterns, dirname)) {
          it.disable_recursion_pending();
        }
        continue;
      }
      if (e.is_regular_file()) {
        // UTF-8, not ACP bytes: these strings feed both the printed file
        // header and the wide path rebuilt for opening the file (#88).
        std::string filepath = wstring_to_utf8(e.path().generic_wstring());
        std::string filename = wstring_to_utf8(e.path().filename().wstring());
        if (!should_search_file(cfg, filename)) continue;
        out.push_back(filepath);
      }
    }
    return {};
  }

  return std::unexpected("'" + f + "' is a directory");
}

auto gather_files_for_input(Config& cfg, std::vector<std::string>& out)
    -> cp::Result<void> {
  for (const auto& f : cfg.files) {
    // Smart glob expansion for wildcard patterns
    if (contains_wildcard(f)) {
      auto glob_result = glob_expand(f);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          auto result = append_search_path(cfg, wstring_to_utf8(file), out);
          if (!result) return result;
        }
        continue;
      }
      // If expansion failed, fall through to normal processing.
    }

    auto result = append_search_path(cfg, f, out);
    if (!result) return result;
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
  } else if (!cfg.no_filename &&
             (inputs.size() > 1 || cfg.recursive_directory_operand)) {
    show_filename = true;
  }

  bool any_selected_global = false;
  bool any_file_without_match = false;

  if (cfg.only_matching && cfg.context_requested) {
    if (!cfg.no_messages && !cfg.quiet) {
      safeErrorPrint("grep: warning: context options have no effect with -o\n");
    }
    cfg.before_context = 0;
    cfg.after_context = 0;
  }
  const bool use_fixed_fast_path = can_use_fixed_fast_path(cfg);
  if (use_fixed_fast_path) {
    cfg.fixed_matcher = build_fixed_matcher(cfg);
  } else {
    cfg.fixed_matcher.reset();
  }

  for (const auto& input : inputs) {
    std::pair<bool, size_t> scan_result{false, 0};
    auto display_name = record_name_for_output(input, cfg);

    bool use_context = (cfg.before_context > 0 || cfg.after_context > 0) &&
                       !cfg.only_matching && !cfg.count_only &&
                       !cfg.files_with_matches && !cfg.files_without_match;
    auto scan_streaming_file = [&]() -> bool {
      auto streamed =
          use_fixed_fast_path
              ? scan_fixed_file_fast(input, display_name, show_filename, cfg)
              : scan_file_streaming(input, display_name, show_filename, cfg);
      if (!streamed) {
        cfg.has_error = true;
        report_input_error(cfg, streamed.error());
        return false;
      } else {
        scan_result = *streamed;
      }
      return true;
    };

    if (input == "-") {
      if (cfg.binary_mode == BinaryMode::Binary) {
        std::string content((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
        if (has_text_bom(content)) {
          scan_result = scan_text(decode_text_bytes(content), display_name,
                                  show_filename, cfg);
        } else {
          scan_result =
              scan_binary_default(content, display_name, show_filename, cfg);
        }
      } else if (cfg.binary_mode == BinaryMode::WithoutMatch) {
        std::string content((std::istreambuf_iterator<char>(std::cin)),
                            std::istreambuf_iterator<char>());
        if (!contains_binary_bytes(content, cfg)) {
          std::istringstream decoded(content);
          scan_result = scan_text(read_text_stream(decoded), display_name,
                                  show_filename, cfg);
        }
      } else {
        scan_result = scan_text(read_text_stream(std::cin), display_name,
                                show_filename, cfg);
      }
    } else if (cfg.binary_mode == BinaryMode::WithoutMatch) {
      auto content = read_file_binary(input);
      if (!content) {
        cfg.has_error = true;
        report_input_error(cfg, content.error());
        continue;
      }
      if (!contains_binary_bytes(*content, cfg)) {
        std::istringstream decoded(*content);
        scan_result = scan_text(read_text_stream(decoded), display_name,
                                show_filename, cfg);
      }
    } else if (cfg.binary_mode == BinaryMode::Binary) {
      if (!use_context && should_stream_file_fast_path(input, cfg)) {
        if (!scan_streaming_file()) continue;
      } else {
        auto content = read_file_binary(input);
        if (!content) {
          cfg.has_error = true;
          report_input_error(cfg, content.error());
          continue;
        }
        if (has_text_bom(*content)) {
          scan_result = scan_text(decode_text_bytes(*content), display_name,
                                  show_filename, cfg);
        } else {
          scan_result =
              scan_binary_default(*content, display_name, show_filename, cfg);
        }
      }
    } else if (use_context) {
      // Read entire file for context support
      auto content = read_file_text(input);
      if (!content) {
        cfg.has_error = true;
        report_input_error(cfg, content.error());
        continue;
      }
      scan_result = scan_text(*content, display_name, show_filename, cfg);
    } else if (should_stream_file_fast_path(input, cfg)) {
      if (!scan_streaming_file()) continue;
    } else {
      auto content = read_file_text(input);
      if (!content) {
        cfg.has_error = true;
        report_input_error(cfg, content.error());
        continue;
      }
      scan_result = scan_text(*content, display_name, show_filename, cfg);
    }

    auto [any_selected, selected_count] = scan_result;
    any_selected_global = any_selected_global || any_selected;

    if (!cfg.quiet) {
      if (cfg.files_with_matches && any_selected) {
        safePrint(display_name);
        print_filename_terminator(cfg);
        flush_if_line_buffered(cfg);
      }

      if (cfg.files_without_match && !any_selected) {
        safePrint(display_name);
        print_filename_terminator(cfg);
        flush_if_line_buffered(cfg);
        any_file_without_match = true;
      }

      if (cfg.count_only) {
        if (show_filename) {
          safePrint(display_name);
          safePrint(":");
        }
        safePrint(std::to_string(selected_count));
        safePrint("\n");
        flush_if_line_buffered(cfg);
      }
    }

    if (cfg.quiet && any_selected_global) break;
  }

  if (cfg.has_error && !cfg.quiet) return 2;
  // -L (files-without-match): exit 0 if any file was printed (had no matches),
  // exit 1 if all files matched (none printed).
  if (cfg.files_without_match) return any_file_without_match ? 0 : 1;
  return any_selected_global ? 0 : 1;
}

auto execute_with_default_mode(CommandContext<GREP_OPTIONS.size()>& ctx,
                               PatternMode default_mode,
                               const wchar_t* command_name) -> int {
  auto config = build_config(ctx, default_mode);
  if (!config) {
    cp::report_error(config, command_name);
    return 2;
  }

  return process(*config);
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

  return execute_with_default_mode(ctx, PatternMode::BasicRegex, L"grep");
}

REGISTER_COMMAND(
    egrep, "egrep", "egrep [OPTION]... PATTERNS [FILE]...",
    "Search for PATTERNS in each FILE using extended regular expressions.\n"
    "This compatibility entry point is equivalent to grep -E.",
    "  egrep 'foo|bar' file.txt\n"
    "  egrep -n 'a+' file.txt",
    "grep(1), fgrep(1), sed(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    GREP_OPTIONS) {
  using namespace grep_pipeline;

  return execute_with_default_mode(ctx, PatternMode::ExtendedRegex, L"egrep");
}

REGISTER_COMMAND(fgrep, "fgrep", "fgrep [OPTION]... PATTERNS [FILE]...",
                 "Search for fixed PATTERNS in each FILE.\n"
                 "This compatibility entry point is equivalent to grep -F.",
                 "  fgrep 'a+b' file.txt\n"
                 "  fgrep -n 'literal text' file.txt",
                 "grep(1), egrep(1), sed(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", GREP_OPTIONS) {
  using namespace grep_pipeline;

  return execute_with_default_mode(ctx, PatternMode::Fixed, L"fgrep");
}
