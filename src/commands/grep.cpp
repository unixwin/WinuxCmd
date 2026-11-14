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
import rust2_bridge;

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
 * - @a -U, @a --binary: Treat the file(s) as binary [DIFFERS: accepted but
 * no effect; a trailing CR is never stripped]
 */
auto constexpr GREP_OPTIONS = std::array{
    OPTION("-E", "--extended-regexp",
           "PATTERNS are extended regular expressions"),
    OPTION("-F", "--fixed-strings", "PATTERNS are strings"),
    // [GNU] grep.c registers --fixed-regexp as a second long alias of -F.
    OPTION("", "--fixed-regexp", "PATTERNS are strings"),
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
    // [GNU] #1521: -m is xstrtoimax with LONGINT_OVERFLOW accepted
    // (grep.c:2681-2692), so it clamps to INTMAX_MAX via the clamp
    // declaration — past UINT64_MAX included (captain adjudication
    // closing #1521's residual: GNU's acceptance is unbounded);
    // negatives still store signed.
    OPTION("-m", "--max-count", "stop after NUM selected lines",
           UINT_CLAMP_TYPE),
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
    // [GNU] #1521: context lengths go through context_length_arg
    // (grep.c:826-841, xstrtoimax, LONGINT_OVERFLOW accepted), the
    // same clamp family as -m.
    OPTION("-B", "--before-context", "print NUM lines of leading context",
           UINT_CLAMP_TYPE),
    OPTION("-A", "--after-context", "print NUM lines of trailing context",
           UINT_CLAMP_TYPE),
    OPTION("-C", "--context", "print NUM lines of output context",
           UINT_CLAMP_TYPE),
    OPTION("-NUM", "", "same as --context=NUM", UINT_CLAMP_TYPE),
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

// Same rationale as sed's open_operand_path, in wide form for the
// filesystem::path/_wfopen open sites (#1332/#1333).
auto open_operand_path_w(const std::string& path) -> std::wstring {
  auto operand = native_path::make_api_path_operand(path);
  if (operand.had_trailing_separator) {
    return operand.normalized;
  }
  return operand.extended;
}

enum class PatternMode { BasicRegex, ExtendedRegex, Fixed, PerlRegex };
enum class BinaryMode { Binary, Text, WithoutMatch };

struct MatchPiece {
  size_t begin = 0;
  size_t end = 0;
};

// #1488 split-engine wire: the Pattern/Config types below carry only what
// the retained C++ PCRE2 face (-P/--perl-regexp) still needs. The BRE/ERE
// matcher selection, the -F fixed-string machinery (FixedMatcher Aho-
// Corasick, the ERE literal/class fast paths) and the fixed scan fast
// path were the non-Perl surface; that surface is the rust2 grep library
// now (see the REGISTER_COMMAND split at the bottom), so the helpers that
// only served it are deleted in the same change. Everything reachable
// from a -P invocation stays.

struct Pattern {
  std::string raw;
  std::optional<portable_regex::Pattern> regex;
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
  // [GNU] grep.c stores max_count as intmax_t (xstrtoimax, #1509).
  std::int64_t max_count = -1;
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
  // [GNU] an empty pattern source (keycc == 0) is modelled by grep.c as
  // an empty key with out_invert flipped (grep.c:2843-2850): without -v
  // nothing is selected and grep exits 1 before opening operands; with
  // -v every line is selected. The scan below keeps an empty pattern
  // list (matches nothing) and process() applies the flip's exit
  // semantics; the forced "return 1" an earlier build had here broke
  // the -v case.
  bool empty_pattern_from_file = false;
  bool recursive = false;
  bool recursive_directory_operand = false;
  bool dereference_recursive = false;
  SmallVector<Pattern, 32> patterns;
  SmallVector<std::string, 64> files;
  bool has_error = false;
  // [GNU] context lengths are intmax_t in grep.c (context_length_arg).
  std::int64_t before_context = 0;
  std::int64_t after_context = 0;
  bool context_requested = false;
  bool color = false;
  ColorConfig color_config;
  BinaryMode binary_mode = BinaryMode::Binary;
  SmallVector<FileSelectionRule, 32> file_selection_rules;
  SmallVector<std::string, 16> exclude_dir_patterns;
  std::string group_separator = "--";
  bool no_group_separator = false;
  bool initial_tab = false;
  // [GNU] grep.c's offset_width: the field width print_offset applies to
  // BOTH the line number and the byte offset ("%*"PRIdMAX). It stays 0
  // unless align_tabs (-T) is in effect; process() recomputes it per
  // input from that input's size (grep.c:1541-1550).
  int offset_width = 0;
  // [GNU] --label= (even empty) is a real label; presence, not emptiness,
  // decides (grep.c LABEL_OPTION / input_filename).
  bool label_given = false;
  bool unix_byte_offsets = false;
  // Set when the operand list defaulted to "." for a bare recursive run;
  // GNU strips the resulting "./" prefix (omit_dot_slash).
  bool implicit_cwd_operand = false;
  // [GNU] prtext's separator state is process-wide (a static in grep.c),
  // not per file: a later file's first context group still gets "--".
  bool group_separator_printed = false;
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

// Word-character test for -w. Mirrors portable_regex: ASCII alnum/_ plus
// multi-byte UTF-8 codepoints, so CJK is not split into word-breaking bytes.
auto word_cp_is_word(std::string_view line, size_t pos) -> bool {
  if (pos >= line.size()) return false;
  size_t start = pos;
  auto at = [&](size_t i) {
    return static_cast<unsigned char>(line[i]);
  };
  auto continuation = [](unsigned char c) { return (c & 0xC0) == 0x80; };
  if (continuation(at(start))) {
    size_t i = start - 1;
    size_t scanned = 0;
    while (i > 0 && scanned < 3 && continuation(at(i))) {
      --i;
      ++scanned;
    }
    start = i;
  }
  const auto lead = at(start);
  if (lead < 0x80) return is_word_char(lead);
  // Multi-byte lead with continuation bytes: treat as a word character.
  size_t need = 0;
  if ((lead & 0xE0) == 0xC0) {
    need = 2;
  } else if ((lead & 0xF0) == 0xE0) {
    need = 3;
  } else if ((lead & 0xF8) == 0xF0) {
    need = 4;
  } else {
    return false;
  }
  if (start + need > line.size()) return false;
  for (size_t k = 1; k < need; ++k) {
    if (!continuation(at(start + k))) return false;
  }
  return true;
}

auto word_cp_is_word_before(std::string_view line, size_t pos) -> bool {
  if (pos == 0) return false;
  return word_cp_is_word(line, pos - 1);
}

auto word_boundary_ok(std::string_view line, size_t begin, size_t end) -> bool {
  bool left_ok = !word_cp_is_word_before(line, begin);
  bool right_ok = !word_cp_is_word(line, end);
  return left_ok && right_ok;
}
// #1488 split-engine wire: this compiles ONLY the retained C++ face's
// patterns. Every invocation that reaches it selected -P/--perl-regexp
// (the routing split lives in the REGISTER_COMMAND handlers; a -F/-E/-G
// co-occurrence is a matcher conflict that build_config rejects before
// compiling), so the BRE/ERE/-F compilation — now the rust2 grep
// library's surface — is gone with the fast paths that served it.
auto compile_pattern(bool ignore_case, std::string_view raw)
    -> cp::Result<Pattern> {
  Pattern p;
  p.raw = std::string(raw);

  auto compiled =
      portable_regex::compile(portable_regex::Syntax::Perl, p.raw, ignore_case);
  if (!compiled) {
    return std::unexpected("invalid regular expression: " + compiled.error);
  }
  p.regex.emplace(std::move(compiled.pattern));

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
      // [GNU] grep.c 'f' / --exclude-from failures are reported as
      // die(EXIT_TROUBLE, errno, "%s", optarg): "grep: FILE: <reason>".
      std::error_code fec;
      std::string reason = "Permission denied";
      if (!std::filesystem::exists(path, fec)) {
        reason = "No such file or directory";
      } else if (std::filesystem::is_directory(path, fec)) {
        reason = "Is a directory";
      }
      return std::unexpected(path + ": " + reason);
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

// [GNU] grep.c:2869 calls usage (EXIT_TROUBLE) when no pattern is
// given: exactly the two usage lines on stderr (grep.c:1969-1974), not
// a prefixed "grep: ..." diagnostic. build_config reports the case
// with this sentinel; execute_with_default_mode renders the GNU shape
// (#1510).
inline constexpr std::string_view kMissingPatternsError = "missing PATTERNS";

// [GNU] grep.c COLOR_OPTION: an unrecognized --color/--colour value
// calls usage (EXIT_SUCCESS) — the full help on stdout with status 0,
// no diagnostic (grep 3.11 probe 2026-11-07 covers both a bogus value
// and the attached empty value, undoing the #1513 rejection). Report
// the case with this sentinel; execute_with_default_mode renders the
// GNU shape (#1516).
inline constexpr std::string_view kColorUsageSuccessError =
    "invalid color mode";

auto build_config(const CommandContext<GREP_OPTIONS.size()>& ctx,
                  PatternMode default_mode = PatternMode::BasicRegex)
    -> cp::Result<Config> {
  Config cfg;
  cfg.mode = default_mode;
  std::optional<PatternMode> explicit_matcher;

  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= GREP_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];

    // [GNU] a second, different matcher is a hard error (grep.c:2108-2110);
    // repeating the same matcher stays legal.  Only EXPLICIT matchers count:
    // the command's default mode (egrep -> -E) must not conflict with -P.
    if (option_matches(meta, "-F", "--fixed-strings") ||
        option_matches(meta, "", "--fixed-regexp")) {
      if (explicit_matcher && *explicit_matcher != PatternMode::Fixed) {
        return std::unexpected("conflicting matchers specified");
      }
      explicit_matcher = PatternMode::Fixed;
      cfg.mode = PatternMode::Fixed;
      continue;
    }
    if (option_matches(meta, "-E", "--extended-regexp")) {
      if (explicit_matcher && *explicit_matcher != PatternMode::ExtendedRegex) {
        return std::unexpected("conflicting matchers specified");
      }
      explicit_matcher = PatternMode::ExtendedRegex;
      cfg.mode = PatternMode::ExtendedRegex;
      continue;
    }
    if (option_matches(meta, "-G", "--basic-regexp")) {
      if (explicit_matcher && *explicit_matcher != PatternMode::BasicRegex) {
        return std::unexpected("conflicting matchers specified");
      }
      explicit_matcher = PatternMode::BasicRegex;
      cfg.mode = PatternMode::BasicRegex;
      continue;
    }
    if (option_matches(meta, "-P", "--perl-regexp")) {
      if (explicit_matcher && *explicit_matcher != PatternMode::PerlRegex) {
        return std::unexpected("conflicting matchers specified");
      }
      explicit_matcher = PatternMode::PerlRegex;
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
  cfg.max_count = ctx.get<std::int64_t>("--max-count", -1);
  if (cfg.max_count < 0) cfg.max_count = ctx.get<std::int64_t>("-m", -1);
  cfg.byte_offset =
      ctx.get<bool>("--byte-offset", false) || ctx.get<bool>("-b", false);
  cfg.line_number =
      ctx.get<bool>("--line-number", false) || ctx.get<bool>("-n", false);
  cfg.line_buffered = ctx.get<bool>("--line-buffered", false);
  cfg.label = ctx.get<std::string>("--label", "");
  cfg.label_given = ctx.has("--label");
  cfg.unix_byte_offsets = ctx.get<bool>("--unix-byte-offsets", false) ||
                          ctx.get<bool>("-u", false);
  cfg.only_matching =
      ctx.get<bool>("--only-matching", false) || ctx.get<bool>("-o", false);
  cfg.quiet = ctx.get<bool>("--quiet", false) ||
              ctx.get<bool>("--silent", false) || ctx.get<bool>("-q", false);
  // [GNU] -l and -L set ONE list_files variable, so the last one given
  // wins (grep.c case 'l'/'L'); they are not independent booleans.
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= GREP_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (option_matches(meta, "-L", "--files-without-match")) {
      cfg.files_without_match = true;
      cfg.files_with_matches = false;
    } else if (option_matches(meta, "-l", "--files-with-matches")) {
      cfg.files_with_matches = true;
      cfg.files_without_match = false;
    }
  }
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
      // [GNU] grep.c: die (EXIT_TROUBLE, 0, "unknown binary-files type").
      return std::unexpected("unknown binary-files type");
    }
  }

  // [GNU] -d, -r and -R are processed sequentially in grep.c's option
  // switch, so the LAST of them wins: -r/-R assign directories=recurse
  // (-r also resets the fts options to physical, -R selects logical),
  // while -d assigns its argument and leaves the deref choice alone.
  {
    std::string dir_action;
    bool have_dir_action = false;
    bool deref = false;
    for (const auto& occurrence : ctx.options.occurrences()) {
      if (!ctx.metas || occurrence.index >= GREP_OPTIONS.size()) continue;
      const auto& meta = (*ctx.metas)[occurrence.index];
      if (option_matches(meta, "-r", "--recursive")) {
        dir_action = "recurse";
        have_dir_action = true;
        deref = false;
      } else if (option_matches(meta, "-R", "--dereference-recursive")) {
        dir_action = "recurse";
        have_dir_action = true;
        deref = true;
      } else if (option_matches(meta, "-d", "--directories")) {
        if (const auto* value = std::get_if<std::string>(&occurrence.value)) {
          dir_action = *value;
          have_dir_action = true;
        }
      }
    }
    cfg.dereference_recursive = deref;
    cfg.directories = have_dir_action ? dir_action : "read";
    cfg.recursive = cfg.directories == "recurse";
  }
  if (cfg.directories != "read" && cfg.directories != "recurse" &&
      cfg.directories != "skip") {
    // [GNU] -d goes through XARGMATCH: "invalid argument 'X' for
    // '--directories'".
    return std::unexpected("invalid argument '" + cfg.directories +
                           "' for '--directories'");
  }

  cfg.devices = ctx.get<std::string>("--devices", "");
  if (cfg.devices.empty()) cfg.devices = ctx.get<std::string>("-D", "");
  if (cfg.devices.empty()) cfg.devices = "read";
  if (cfg.devices != "read" && cfg.devices != "skip") {
    // [GNU] grep.c: die (EXIT_TROUBLE, 0, "unknown devices method").
    return std::unexpected("unknown devices method");
  }

  // [GNU] grep.c keeps TWO context levels: explicit -A/-B values
  // (out_after/out_before) and a default_context set by -C/-NUM; after
  // parsing, the default fills only the sides no explicit option set
  // (grep.c:2514-2527, 2904-2907) — regardless of option order.
  std::optional<std::int64_t> default_context;
  bool explicit_before = false;
  bool explicit_after = false;
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= GREP_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    // [GNU] #1521: a band context value arrives in the unsigned store;
    // read it through the saturating bridge (a bare get_if<int64_t>
    // would silently drop the occurrence, the #1519 trap).
    std::optional<std::int64_t> value;
    if (auto p = std::get_if<std::int64_t>(&occurrence.value)) {
      value = *p;
    } else if (auto p = std::get_if<std::uint64_t>(&occurrence.value)) {
      value = static_cast<std::int64_t>(std::min(
          *p, static_cast<std::uint64_t>(
                  std::numeric_limits<std::int64_t>::max())));
    }
    if (!value) continue;

    if (option_matches(meta, "-B", "--before-context")) {
      if (*value < 0) {
        return std::unexpected(std::to_string(*value) +
                               ": invalid context length argument");
      }
      cfg.context_requested = true;
      cfg.before_context = *value;
      explicit_before = true;
      continue;
    }
    if (option_matches(meta, "-A", "--after-context")) {
      if (*value < 0) {
        return std::unexpected(std::to_string(*value) +
                               ": invalid context length argument");
      }
      cfg.context_requested = true;
      cfg.after_context = *value;
      explicit_after = true;
      continue;
    }
    if (option_matches(meta, "-C", "--context")) {
      if (*value < 0) {
        return std::unexpected(std::to_string(*value) +
                               ": invalid context length argument");
      }
      cfg.context_requested = true;
      default_context = *value;
      continue;
    }
    if (option_matches(meta, "-NUM", "")) {
      if (*value < 0) {
        return std::unexpected(std::to_string(*value) +
                               ": invalid context length argument");
      }
      cfg.context_requested = true;
      default_context = *value;
      continue;
    }
  }
  if (default_context) {
    if (!explicit_before) cfg.before_context = *default_context;
    if (!explicit_after) cfg.after_context = *default_context;
  }

  std::optional<std::string> color_opt;
  for (const auto& occurrence :
       ctx.string_occurrences({"--color", "--colour"})) {
    // [GNU] grep.c:2749-2768: a bare --color (optarg == nullptr) selects
    // color_option = 2, i.e. auto (grep.c:2916-2917 resolves 2 through
    // the tty check) -- unlike ls, whose bare --color means always.
    // An attached value is judged as written, so "--color=" reaches the
    // invalid-mode branch below instead of collapsing into auto; GNU
    // answers it with usage (EXIT_SUCCESS), not an auto collapse
    // (#1513/#1516).
    color_opt = occurrence.has_value ? occurrence.value : "auto";
  }
  std::string color_mode = color_opt.value_or("auto");
  // [GNU] grep.c COLOR_OPTION accepts, case-insensitively
  // (c_strcasecmp): always|yes|force, never|no|none, auto|tty|if-tty.
  {
    std::string lower = color_mode;
    for (auto& ch : lower) {
      ch = static_cast<char>(
          std::tolower(static_cast<unsigned char>(ch)));
    }
    if (lower == "always" || lower == "yes" || lower == "force") {
      cfg.color = true;
    } else if (lower == "auto" || lower == "tty" || lower == "if-tty") {
      cfg.color = grep_is_terminal(stdout);
    } else if (lower == "never" || lower == "no" || lower == "none") {
      cfg.color = false;
    } else {
      // [GNU] grep.c: usage (EXIT_SUCCESS) — help on stdout, status 0.
      return std::unexpected(std::string(kColorUsageSuccessError));
    }
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
      return std::unexpected(std::string(kMissingPatternsError));
    }
    auto split = split_lines(positionals.front());
    for (const auto& p : split) raw_patterns.push_back(p);
    positionals.erase(positionals.begin(),
                      positionals.begin() + 1);  // Remove first element
  }

  if (raw_patterns.empty() && !pattern_source_provided) {
    return std::unexpected(std::string(kMissingPatternsError));
  }
  if (raw_patterns.empty() && pattern_source_provided) {
    // [GNU] an empty pattern file selects NOTHING: no output, exit 1
    // (verified against GNU 9.4: `grep -f /dev/null` prints nothing).
    cfg.empty_pattern_from_file = true;
  }

  for (const auto& rp : raw_patterns) {
    auto c = compile_pattern(cfg.ignore_case, rp);
    if (!c) return std::unexpected(c.error());
    cfg.patterns.push_back(*c);
  }

  for (const auto& p : positionals) cfg.files.push_back(std::string(p));
  if (cfg.files.empty()) {
    if (cfg.directories == "recurse") {
      cfg.files.push_back(".");
      cfg.implicit_cwd_operand = true;
    } else {
      cfg.files.push_back("-");
    }
  }

  return cfg;
}

auto record_name_for_output(std::string_view input_name, const Config& cfg)
    -> std::string {
  if (input_name == "-") {
    if (cfg.label_given) return cfg.label;
    return "(standard input)";
  }
  return std::string(input_name);
}

auto flush_if_line_buffered(const Config& cfg) -> void {
  if (cfg.line_buffered) {
    ::fflush(stdout);
  }
}

// First accepted match of the single pattern P at or after START, or
// nullopt. The -P face compiles every pattern through portable_regex's
// Perl syntax (the matcher options -F/-E/-G are matcher conflicts under
// -P and die in build_config before this runs), so only the plain regex
// search remains; the word-boundary filter skips rejected candidates the
// same way the old per-pattern collectors did.
auto find_first_match_at_or_after(std::string_view line, const Pattern& p,
                                  const Config& cfg, size_t start)
    -> std::optional<MatchPiece> {
  if (!p.regex.has_value()) return std::nullopt;

  size_t cursor = start;
  while (cursor <= line.size()) {
    auto match = p.regex->find_first(line, cursor);
    if (!match) return std::nullopt;
    MatchPiece m{match->begin, match->end};
    if (!cfg.word_regexp || word_boundary_ok(line, m.begin, m.end)) {
      return m;
    }
    cursor = m.end > m.begin ? m.end : m.begin + 1;
  }
  return std::nullopt;
}

auto collect_matches_in_line(std::string_view line, const Config& cfg)
    -> std::vector<MatchPiece> {
  std::vector<MatchPiece> out;

  if (cfg.line_regexp) {
    for (const auto& p : cfg.patterns) {
      if (p.raw.empty()) {
        out.push_back(MatchPiece{0, 0});
        continue;
      }

      if (!p.regex.has_value()) continue;

      if (p.regex->matches_entire(line)) {
        out.push_back(MatchPiece{0, line.size()});
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

  bool has_nonempty_pattern = false;
  for (const auto& p : cfg.patterns) {
    if (p.raw.empty()) {
      // An empty pattern contributes its single empty match, as before;
      // the printers skip empty pieces.
      if (out.empty()) out.push_back(MatchPiece{0, 0});
    } else {
      has_nonempty_pattern = true;
    }
  }
  if (!has_nonempty_pattern) return out;

  // [GNU] -o and --color report the leftmost-longest match over the
  // WHOLE pattern set at each scan position (grep's combined matcher),
  // not every pattern's matches independently: with -e foo -e foobar on
  // "foobar" the only piece is "foobar" (#1508).
  size_t cursor = 0;
  while (cursor <= line.size()) {
    std::optional<MatchPiece> best;
    for (const auto& p : cfg.patterns) {
      if (p.raw.empty()) continue;
      auto m = find_first_match_at_or_after(line, p, cfg, cursor);
      if (!m) continue;
      if (!best || m->begin < best->begin ||
          (m->begin == best->begin && m->end > best->end)) {
        best = m;
      }
    }
    if (!best) break;
    out.push_back(*best);
    cursor = best->end > best->begin ? best->end : best->begin + 1;
  }
  return out;
}

auto find_first_match_in_line(std::string_view line, const Config& cfg)
    -> std::optional<MatchPiece> {
  for (const auto& p : cfg.patterns) {
    if (p.raw.empty()) {
      MatchPiece m{0, 0};
      if (!cfg.word_regexp || word_boundary_ok(line, m.begin, m.end)) return m;
      continue;
    }

    if (!p.regex.has_value()) continue;

    if (cfg.line_regexp) {
      if (p.regex->matches_entire(line)) return MatchPiece{0, line.size()};
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

// [GNU] grep.c print_offset: a line number or byte offset is printed
// right-aligned in a field of cfg.offset_width ("%*"PRIdMAX); the
// padding spaces sit inside the field's color span, exactly like
// pr_sgr_start_if/printf/pr_sgr_end_if. Width 0 pads nothing.
auto append_offset_field(std::string& out, const Config& cfg,
                         std::string_view color, size_t value) -> void {
  auto s = std::to_string(value);
  if (cfg.offset_width > 0 &&
      s.size() < static_cast<size_t>(cfg.offset_width)) {
    s.insert(0, static_cast<size_t>(cfg.offset_width) - s.size(), ' ');
  }
  if (cfg.color) {
    append_colored(out, cfg.color_config, color, s);
  } else {
    out.append(s);
  }
}

auto append_prefix(std::string& out, const Config& cfg, bool show_filename,
                   std::string_view display_name, size_t line_no, size_t offset,
                   char separator = ':') -> void {
  if (show_filename) {
    // [GNU] -Z/--null replaces ONLY the ':' after the filename with NUL;
    // the line-number/offset separators stay ':' (grep.c filename_mask,
    // print_line_head 1179-1187).
    if (cfg.color) {
      append_colored(out, cfg.color_config, cfg.color_config.filename,
                     display_name);
    } else {
      out.append(display_name);
    }
    if (cfg.color) {
      append_colored_char(out, cfg.color_config, cfg.color_config.separator,
                          cfg.null_after_filename ? '\0' : separator);
    } else {
      out.push_back(cfg.null_after_filename ? '\0' : separator);
    }
  }
  if (cfg.line_number) {
    append_offset_field(out, cfg, cfg.color_config.line_number, line_no);
    if (cfg.color) {
      append_colored_char(out, cfg.color_config, cfg.color_config.separator,
                          separator);
    } else {
      out.push_back(separator);
    }
  }
  if (cfg.byte_offset) {
    append_offset_field(out, cfg, cfg.color_config.byte_offset, offset);
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

  // [GNU] -o prints the parts of a line that MATCH the pattern.  With -v
  // the selected lines are exactly the non-matching ones, so they have no
  // matching parts and -o prints nothing for them (the line still counts
  // as selected above).  GNU computes match offsets before inversion.
  if (cfg.only_matching && cfg.invert_match) return true;

  if (cfg.only_matching && !cfg.invert_match) {
    matches = collect_matches_in_line(line, cfg);
    for (const auto& m : matches) {
      if (m.end <= m.begin) continue;
      output_buf.clear();
      append_prefix(output_buf, cfg, show_filename, display_name, line_no,
                    offset + m.begin);
      if (cfg.initial_tab && (show_filename || cfg.line_number ||
          cfg.byte_offset) &&
          !line.empty())
        output_buf.push_back('\t');
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
    if (cfg.initial_tab && (show_filename || cfg.line_number ||
          cfg.byte_offset) &&
          !line.empty())
        output_buf.push_back('\t');
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

  for (const auto& [gs, ge] : groups) {
    if (cfg.group_separator_printed) {
      if (!cfg.no_group_separator) {
        safePrint(cfg.group_separator);
        safePrint("\n");
        flush_if_line_buffered(cfg);
      }
    }
    cfg.group_separator_printed = true;

    for (size_t i = gs; i <= ge; ++i) {
      const auto [b, e] = records[i];
      std::string_view whole(text.data() + b, e - b);
      bool had_delim = !whole.empty() && whole.back() == delim;
      std::string_view line =
          had_delim ? whole.substr(0, whole.size() - 1) : whole;

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
        if (cfg.initial_tab && (show_filename || cfg.line_number ||
            cfg.byte_offset) &&
            !line.empty())
          line_buf.push_back('\t');
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
        if (cfg.initial_tab && (show_filename || cfg.line_number ||
            cfg.byte_offset) &&
            !line.empty())
          line_buf.push_back('\t');
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
    std::string_view line(pending);
    if (process_selected_record(line, false, display_name, show_filename,
                                line_no, base_offset, cfg, selected_count)) {
      any_selected = true;
    }
  }

  return {any_selected, selected_count};
}

auto read_file_binary(const std::string& path) -> cp::Result<std::string> {
  std::ifstream in(std::filesystem::path(open_operand_path_w(path)),
                   std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + path + "'");
  }
  return std::string((std::istreambuf_iterator<char>(in)),
                     std::istreambuf_iterator<char>());
}

auto read_file_text(const std::string& path) -> cp::Result<std::string> {
  std::ifstream in(std::filesystem::path(open_operand_path_w(path)),
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

  std::ifstream in(std::filesystem::path(open_operand_path_w(path)),
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

  // [GNU] grep.c:1554-1565 judges binary-ness over the whole buffer it
  // reads, and for a regular file that buffer is effectively the whole
  // file -- so a first NUL past the 64 KiB sample still makes the file
  // binary. The sample alone used to send such files down the text
  // streaming path (#1507). When per-line output will actually be
  // printed (the only case where the verdict is observable; the modes
  // scan_binary_default delegates to text scanning are exempt), extend
  // the probe over the rest of a regular file before trusting the
  // streaming path. Non-regular inputs keep the sample behavior.
  if (cfg.binary_mode == BinaryMode::Binary && !cfg.null_data &&
      !cfg.quiet && !cfg.files_with_matches && !cfg.files_without_match &&
      !cfg.count_only && got == sample.size()) {
    std::error_code ec;
    if (std::filesystem::is_regular_file(
            std::filesystem::path(open_operand_path_w(path)), ec) &&
        !ec) {
      while (in) {
        in.read(sample.data(), static_cast<std::streamsize>(sample.size()));
        const auto n =
            static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0));
        if (n == 0) break;
        if (contains_binary_bytes(std::string_view(sample.data(), n), cfg)) {
          return false;
        }
      }
    }
  }
  return true;
}

auto scan_file_streaming(const std::string& path, std::string_view display_name,
                         bool show_filename, Config& cfg)
    -> cp::Result<std::pair<bool, size_t>> {
  std::ifstream in(std::filesystem::path(open_operand_path_w(path)),
                   std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + path + "'");
  }
  return scan_stream(in, display_name, show_filename, cfg);
}

auto report_input_error(const Config& cfg, const std::string& path,
                        std::string_view message) -> void {
  // [GNU] -q suppresses normal output, not error diagnostics; only -s
  // silences diagnostics (suppressible_error, grep.c:667).  Input opens
  // are reported as "grep: FILE: <strerror>".
  if (cfg.no_messages) return;
  safeErrorPrint("grep: ");
  safeErrorPrint(path);
  if (message.starts_with("cannot open '")) {
    auto reason = portable_digest::open_error_reason(path);
    if (!reason.empty()) {
      safeErrorPrint(": ");
      safeErrorPrint(reason);
    }
  } else {
    safeErrorPrint(": ");
    safeErrorPrint(message);
  }
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

  // Probes and the recursive walk run on the resolved spelling so the MSYS
  // drive dialect ("/e") names the same target a native spelling would;
  // `f` keeps the caller's text for exclude matching and diagnostics
  // (#1230). The resolved spelling stays normalized (not the extended
  // form the scan_* sites use): the walk builds child paths with
  // generic_wstring (slash form), which does not round-trip a \\?\\ root
  // through the operand boundary — a trailing-dot/device-stem DIRECTORY
  // operand therefore still classifies like baseline, and file operands are
  // unaffected because the scan_* open sites use the extended form.
  const std::string resolved = native_path::normalize_api_operand(f);

  std::error_code ec;
  bool is_dir = std::filesystem::is_directory(resolved, ec);
  if (ec) {
    out.push_back(f);
    return {};
  }

  if (!is_dir) {
    bool is_regular = std::filesystem::is_regular_file(resolved, ec);
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
    // Hand-rolled walk, not recursive_directory_iterator: pnpm lays out
    // node_modules with NTFS junctions, which std::filesystem does not
    // classify as symlinks (mount-point reparse tag), so
    // follow_directory_symlink=off still descends into them and crossed
    // junction trees recurse until the stack dies with 0xC0000409 and no
    // diagnostics (#1135). Explicit stack + visited-real-path dedup keeps
    // -r GNU-shaped (never follow reparse dirs) and -R loop-safe.
    struct Pending {
      std::wstring dir;      // directory whose files we read next
      std::wstring dir_arg;  // as spelled for exclude matching
    };
    std::vector<Pending> pending{
        {utf8_to_wstring(resolved), utf8_to_wstring(f)}};
    std::set<std::wstring> visited_dirs;  // canonical real paths
    auto is_reparse_dir = [](const std::filesystem::path& p) {
      // #1288: probe the self-extending spelling (native_path), not the
      // raw walked path.
      DWORD attrs = native_path::attributes_w(p.wstring());
      return attrs != INVALID_FILE_ATTRIBUTES &&
             (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    };
    auto canonical_key = [](const std::filesystem::path& p) -> std::wstring {
      std::error_code canon_ec;
      // #1288: canonicalize the extended spelling; on the raw one,
      // canonical() fails past MAX_PATH and the dedup key degrades to
      // the literal spelling, so junction cycles escape detection.
      const std::filesystem::path io(
          native_path::to_extended_path(p.wstring()));
      auto real = std::filesystem::canonical(io, canon_ec);
      if (canon_ec) real = std::filesystem::weakly_canonical(io, canon_ec);
      std::wstring key = real.wstring();
      for (auto& ch : key) {
        ch = static_cast<wchar_t>(
            std::towlower(static_cast<wint_t>(ch)));
      }
      return key;
    };
    while (!pending.empty()) {
      Pending cur = pending.back();
      pending.pop_back();

      std::error_code iter_ec;
      std::filesystem::directory_iterator it(std::filesystem::path(cur.dir),
                                             std::filesystem::directory_options
                                                 ::skip_permission_denied,
                                             iter_ec);
      if (iter_ec) {
        // [GNU] an unreadable directory is FTS_DNR: diagnosed (unless
        // -s) and counted as an error (exit 2), not silently skipped.
        if (!cfg.no_messages) {
          safeErrorPrint("grep: ");
          safeErrorPrint(wstring_to_utf8(cur.dir));
          safeErrorPrint(": ");
          safeErrorPrintLn(iter_ec.message());
        }
        cfg.has_error = true;
        continue;
      }
      std::filesystem::directory_iterator end;
      for (; it != end; it.increment(iter_ec)) {
        if (iter_ec) {
          if (!cfg.no_messages) {
            safeErrorPrint("grep: ");
            safeErrorPrint(wstring_to_utf8(cur.dir));
            safeErrorPrint(": ");
            safeErrorPrintLn(iter_ec.message());
          }
          cfg.has_error = true;
          break;
        }
        const auto& e = *it;
        std::error_code entry_ec;
        if (e.is_directory(entry_ec) && !entry_ec) {
          std::string dirname =
              wstring_to_utf8(e.path().filename().wstring());
          if (matches_any_glob(cfg.exclude_dir_patterns, dirname)) continue;
          bool reparse = is_reparse_dir(e.path());
          if (reparse && !cfg.dereference_recursive) continue;
          if (reparse) {
            auto key = canonical_key(e.path());
            if (!visited_dirs.insert(key).second) {
              // [GNU] the FTS_DC loop warning is a suppressible_error:
              // -s silences it (grep.c:1687-1690).
              if (!cfg.no_messages) {
                safeErrorPrint("grep: warning: ");
                safeErrorPrintLn(
                    wstring_to_utf8(e.path().wstring()) +
                    ": cyclic directory junction; skipped");
              }
              continue;
            }
          }
          pending.push_back({e.path().wstring(), e.path().wstring()});
          continue;
        }
        std::error_code file_ec;
        if (e.is_regular_file(file_ec) && !file_ec) {
          // [GNU] under plain -r the walk is FTS_PHYSICAL: a symlink or
          // junction FILE is an FTS_SL entry and is skipped, not
          // searched; only -R (FTS_LOGICAL) follows it (grep.c:606-607,
          // 1726-1727). is_regular_file follows links, so gate on the
          // reparse attribute explicitly.
          if (!cfg.dereference_recursive && is_reparse_dir(e.path())) {
            continue;
          }
          std::string filepath = wstring_to_utf8(e.path().generic_wstring());
          std::string filename = wstring_to_utf8(e.path().filename().wstring());
          if (!should_search_file(cfg, filename)) continue;
          // [GNU] omit_dot_slash: names from the implicit "." operand
          // lose their "./" prefix (grep.c:1674-1676).
          if (cfg.implicit_cwd_operand && filepath.starts_with("./")) {
            filepath = filepath.substr(2);
          }
          out.push_back(filepath);
        }
      }
    }
    return {};
  }

  // [GNU] a directory operand under -d read is a PER-OPERAND error
  // ("grep: NAME: Is a directory", errseen), not a fatal gather failure:
  // the remaining operands are still searched and the exit status is 2.
  // Keep the operand in the input list; process() reports it in order.
  out.push_back(f);
  return {};
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

// [GNU] grep.c:1541-1550: with align_tabs, offset_width is the decimal
// digit count of the largest value a line number or byte offset can
// take for this input, bounded by the input size (line numbers are
// origin-1, so the bound grows by one when line numbers are printed);
// an input with no usable size (pipe, console, device) falls back to
// INTMAX_MAX, i.e. a width of 19.
auto grep_offset_width(bool size_usable, std::uint64_t size, bool out_line)
    -> int {
  auto num = size_usable
                 ? size
                 : static_cast<std::uint64_t>(
                       std::numeric_limits<std::int64_t>::max());
  if (out_line &&
      num < static_cast<std::uint64_t>(
                std::numeric_limits<std::int64_t>::max())) {
    ++num;
  }
  int width = 0;
  do {
    ++width;
  } while ((num /= 10) != 0);
  return width;
}

// [GNU] usable_st_size for a named operand: a regular file's size is
// the bound; anything else (device, fifo bridge, vanished file) has no
// usable size.
auto operand_usable_size(const std::string& input, std::uint64_t& size)
    -> bool {
  const std::filesystem::path path(open_operand_path_w(input));
  std::error_code ec;
  if (!std::filesystem::is_regular_file(path, ec) || ec) return false;
  const auto file_size = std::filesystem::file_size(path, ec);
  if (ec) return false;
  size = static_cast<std::uint64_t>(file_size);
  return true;
}

// [GNU] the same bound for standard input comes from fstat(0): a disk
// file redirected into stdin reports its size, a pipe or console does
// not (GetFileType != FILE_TYPE_DISK).
auto stdin_usable_size(std::uint64_t& size) -> bool {
  const auto handle =
      reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(stdin)));
  if (handle == INVALID_HANDLE_VALUE) return false;
  if (GetFileType(handle) != FILE_TYPE_DISK) return false;
  LARGE_INTEGER file_size{};
  if (!GetFileSizeEx(handle, &file_size) || file_size.QuadPart < 0) {
    return false;
  }
  size = static_cast<std::uint64_t>(file_size.QuadPart);
  return true;
}

auto process(Config& cfg) -> int {
  // [GNU] grep.c prints the -u obsolescence warning while parsing,
  // before any input is touched (grep.c:2574-2577).
  if (cfg.unix_byte_offsets) {
    safeErrorPrint(
        "grep: warning: --unix-byte-offsets (-u) is obsolete\n");
  }
  // [GNU] grep.c:2911-2914: with -m 0 (or an empty pattern set whose
  // post-flip invert makes selection impossible) and list_files !=
  // LISTFILES_NONMATCHING, grep exits EXIT_FAILURE before opening any
  // operand — a missing operand is NOT diagnosed.
  if (cfg.max_count == 0 && !cfg.files_without_match) return 1;
  if (cfg.empty_pattern_from_file && !cfg.invert_match &&
      !cfg.files_without_match) {
    return 1;
  }
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
    // [GNU] -o with context options is accepted silently: context
    // lines simply produce no output through the prline path. (An
    // earlier build printed an invented warning here.)
    cfg.before_context = 0;
    cfg.after_context = 0;
  }

  for (const auto& input : inputs) {
    // #1368: once stdout can no longer take writes, scanning further files
    // only feeds a dead sink; GNU grep stops at the first failed write.
    if (is_stdout_pipe_closed() || is_stdout_write_failed()) break;
    // [GNU] directory operand under -d read: report in operand order,
    // mark the run as errored (final status 2), keep searching the rest.
    if (input != "-" && cfg.directories == "read") {
      std::error_code dir_ec;
      if (std::filesystem::is_directory(
              native_path::normalize_api_operand(input), dir_ec) &&
          !dir_ec) {
        cfg.has_error = true;
        report_input_error(cfg, input, "Is a directory");
        continue;
      }
    }
    std::pair<bool, size_t> scan_result{false, 0};
    auto display_name = record_name_for_output(input, cfg);

    // [GNU] grep.c computes offset_width once per input at the top of
    // grep() (1541-1550); without -T it is 0 and print_offset pads
    // nothing (#1515).
    if (!cfg.initial_tab) {
      cfg.offset_width = 0;
    } else {
      std::uint64_t input_size = 0;
      const bool size_usable =
          (input == "-") ? stdin_usable_size(input_size)
                         : operand_usable_size(input, input_size);
      cfg.offset_width =
          grep_offset_width(size_usable, input_size, cfg.line_number);
    }

    bool use_context = (cfg.before_context > 0 || cfg.after_context > 0) &&
                       !cfg.only_matching && !cfg.count_only &&
                       !cfg.files_with_matches && !cfg.files_without_match;
    auto scan_streaming_file = [&]() -> bool {
      auto streamed = scan_file_streaming(input, display_name, show_filename, cfg);
      if (!streamed) {
        cfg.has_error = true;
        report_input_error(cfg, input, streamed.error());
        return false;
      } else {
        scan_result = *streamed;
      }
      return true;
    };

    if (input == "-") {
      file_io::set_stdin_binary();
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
        report_input_error(cfg, input, content.error());
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
          report_input_error(cfg, input, content.error());
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
        report_input_error(cfg, input, content.error());
        continue;
      }
      scan_result = scan_text(*content, display_name, show_filename, cfg);
    } else if (should_stream_file_fast_path(input, cfg)) {
      if (!scan_streaming_file()) continue;
    } else {
      auto content = read_file_text(input);
      if (!content) {
        cfg.has_error = true;
        report_input_error(cfg, input, content.error());
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

      // [GNU] -l/-L override -c: with a file listing in effect the counts
      // are never printed (grep.c:2892-2900).
      if (cfg.count_only && !cfg.files_with_matches &&
          !cfg.files_without_match) {
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

  // [GNU] a failed stdout write is an EXIT_TROUBLE (2) outcome, reported
  // once as "grep: write error: <strerror>" (#1368). A closed downstream
  // pipe keeps the quiet SIGPIPE-style status.
  if (int write_error_rc = report_stdout_write_error("grep", 2)) {
    return write_error_rc;
  }
  // [GNU] under -q, grep exits ON the first match with exit_failure,
  // which -q itself reset to 0 (grep.c:2700-2702, 1490): an error seen
  // before the match does not turn the status into 2. Without a match,
  // errors exit 2 (errseen, grep.c:3032).
  if (cfg.quiet && any_selected_global) return 0;
  if (cfg.has_error) return 2;
  // -L (files-without-match): exit 0 if any file was printed (had no matches),
  // exit 1 if all files matched (none printed).
  // [GNU] -L follows the same rule as normal mode: exit 0 iff at least one
  // line was selected anywhere (doc/grep.texi; all files listed -> 1).
  if (cfg.files_without_match) return any_selected_global ? 0 : 1;
  return any_selected_global ? 0 : 1;
}

auto execute_with_default_mode(CommandContext<GREP_OPTIONS.size()>& ctx,
                               PatternMode default_mode,
                               const wchar_t* command_name) -> int {
  auto config = build_config(ctx, default_mode);
  if (!config) {
    if (config.error() == kMissingPatternsError) {
      const std::string name = wstring_to_utf8(command_name);
      safeErrorPrintLn("Usage: " + name + " [OPTION]... PATTERNS [FILE]...");
      safeErrorPrintLn(winux::i18n::format(
          "common.try_help", "Try '{} --help' for more information.", name));
      return 2;
    }
    // [GNU] #1516: an unrecognized --color/--colour value is answered
    // with usage (EXIT_SUCCESS) in grep.c — the full help on stdout,
    // status 0, no diagnostic.
    if (config.error() == kColorUsageSuccessError) {
      std::ignore = cmd::meta::Registry::print_help(
          wstring_to_utf8(command_name));
      return 0;
    }
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

  // #1488 mechanical replacement, split-engine wire: the rust2 library
  // (rust2/crates/winux-cmds/src/grep/, GNU grep 3.11 port on
  // winux-regex) owns the -F/-E/-G and every non-Perl face, byte-verified
  // against the WSL GNU grep 3.11 oracle (rust2/gate 168 rows). Perl
  // matching stays on the C++ PCRE2 path (DOCS/2.0-regex-engine-decision.md
  // §6 owner point 2): winux::rust2::grep_uses_perl applies grep's own
  // getopt_long grammar to ctx.raw_args, and the split point between the
  // two engines of the SAME command is exactly here, at dispatch — before
  // either body runs. The C++ body below is kept only for that Perl face
  // (everything it needs to serve -P); the replaced non-Perl-only
  // machinery is deleted in the same change (REPLACEMENT-RECIPE.md).
  //
  // The non-Perl shape streams fd 0 through the ABI v2 pull callback:
  // grep is a genuinely-streaming stdin reader (REPLACEMENT-RECIPE.md §5),
  // so `grep -q pattern` on a pipe reads only what it needs instead of
  // slurping the producer dry.
  if (winux::rust2::grep_uses_perl(ctx.raw_args) != 0) {
    return execute_with_default_mode(ctx, PatternMode::BasicRegex, L"grep");
  }
  return winux::rust2::run_command_with_stdin_streaming("grep", ctx.raw_args);
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

  // #1488 mechanical replacement, no split here: upstream grep 3.11 ships
  // `egrep` as a script that warns on stderr and runs `exec grep -E "$@"`,
  // so EVERY egrep shape — including `egrep -P`, which is `grep -E -P` and
  // therefore a fatal "conflicting matchers specified" (the gate's
  // eg02-P-conflict row, probed against WSL grep 3.11) — belongs to the
  // rust2 library. Routing `egrep -P` to the C++ PCRE2 face would run
  // Perl where GNU never does (the 1.x body did exactly that, diverging
  // from the oracle; that divergence retires with the body).
  return winux::rust2::run_command_with_stdin_streaming("egrep", ctx.raw_args);
}

REGISTER_COMMAND(fgrep, "fgrep", "fgrep [OPTION]... PATTERNS [FILE]...",
                 "Search for fixed PATTERNS in each FILE.\n"
                 "This compatibility entry point is equivalent to grep -F.",
                 "  fgrep 'a+b' file.txt\n"
                 "  fgrep -n 'literal text' file.txt",
                 "grep(1), egrep(1), sed(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", GREP_OPTIONS) {
  using namespace grep_pipeline;

  // Same no-split wire as egrep: the `fgrep` script is `exec grep -F
  // "$@"`, so `fgrep -P` is `grep -F -P` — a matcher conflict, not Perl.
  return winux::rust2::run_command_with_stdin_streaming("fgrep", ctx.raw_args);
}
