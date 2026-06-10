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
 *  - File: fmt.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for fmt.
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

auto constexpr FMT_OPTIONS = std::array{
    OPTION("-c", "--crown-margin",
           "preserve indentation of the first two lines", BOOL_TYPE),
    OPTION("-m", "--preserve-headers",
           "attempt to detect and preserve mail headers", BOOL_TYPE),
    OPTION("-p", "--prefix", "reformat only lines beginning with STRING",
           STRING_TYPE),
    OPTION("-P", "--skip-prefix", "do not reformat lines beginning with STRING",
           STRING_TYPE),
    OPTION("-s", "--split-only", "split long lines, but do not refill",
           BOOL_TYPE),
    OPTION("-t", "--tagged-paragraph", "expect indentation in first 2 lines",
           BOOL_TYPE),
    OPTION("-u", "--uniform-spacing",
           "one space between words, two after sentences", BOOL_TYPE),
    OPTION("-x", "--exact-prefix",
           "do not ignore leading whitespace for -p", BOOL_TYPE),
    OPTION("-X", "--exact-skip-prefix",
           "do not ignore leading whitespace for -P", BOOL_TYPE),
    OPTION("-T", "--tab-width", "treat tabs as TABWIDTH spaces when measuring line length",
           STRING_TYPE),
    OPTION("-w", "--width", "maximum line width (default 75)", STRING_TYPE),
    OPTION("-g", "--goal", "goal width (default 93% of width)", STRING_TYPE)};

namespace fmt_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool crown_margin = false;
  bool preserve_headers = false;
  bool split_only = false;
  bool tagged_paragraph = false;
  bool uniform_spacing = false;
  int tab_width = 8;
  int width = 75;
  int goal = 0;  // Will be calculated as 93% of width
  bool width_set = false;
  std::string prefix;
  std::string skip_prefix;
  bool exact_prefix = false;
  bool exact_skip_prefix = false;
  SmallVector<std::string, 64> files;
};

auto parse_positive_int(std::string_view value, std::string_view name)
    -> cp::Result<int> {
  int parsed = 0;
  auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (ec != std::errc() || ptr != value.data() + value.size() || parsed <= 0) {
    return std::unexpected(std::string("invalid ") + std::string(name) +
                           " value");
  }
  return parsed;
}

auto build_config(const CommandContext<FMT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.crown_margin =
      ctx.get<bool>("--crown-margin", false) || ctx.get<bool>("-c", false);
  cfg.preserve_headers = ctx.get<bool>("--preserve-headers", false) ||
                         ctx.get<bool>("-m", false);
  cfg.split_only =
      ctx.get<bool>("--split-only", false) || ctx.get<bool>("-s", false);
  cfg.tagged_paragraph =
      ctx.get<bool>("--tagged-paragraph", false) || ctx.get<bool>("-t", false);
  cfg.uniform_spacing =
      ctx.get<bool>("--uniform-spacing", false) || ctx.get<bool>("-u", false);

  auto prefix_opt = ctx.get<std::string>("--prefix", "");
  if (prefix_opt.empty()) {
    prefix_opt = ctx.get<std::string>("-p", "");
  }
  cfg.prefix = prefix_opt;
  cfg.exact_prefix =
      ctx.get<bool>("--exact-prefix", false) || ctx.get<bool>("-x", false);

  auto skip_prefix_opt = ctx.get<std::string>("--skip-prefix", "");
  if (skip_prefix_opt.empty()) {
    skip_prefix_opt = ctx.get<std::string>("-P", "");
  }
  cfg.skip_prefix = skip_prefix_opt;
  cfg.exact_skip_prefix = ctx.get<bool>("--exact-skip-prefix", false) ||
                          ctx.get<bool>("-X", false);

  auto width_opt = ctx.get<std::string>("--width", "");
  if (width_opt.empty()) {
    width_opt = ctx.get<std::string>("-w", "");
  }
  if (!width_opt.empty()) {
    auto width = parse_positive_int(width_opt, "width");
    if (!width) return std::unexpected(width.error());
    cfg.width = *width;
    cfg.width_set = true;
  }

  auto tab_width_opt = ctx.get<std::string>("--tab-width", "");
  if (tab_width_opt.empty()) {
    tab_width_opt = ctx.get<std::string>("-T", "");
  }
  if (!tab_width_opt.empty()) {
    auto tab_width = parse_positive_int(tab_width_opt, "tab width");
    if (!tab_width) return std::unexpected(tab_width.error());
    cfg.tab_width = *tab_width;
  }

  auto goal_opt = ctx.get<std::string>("--goal", "");
  if (goal_opt.empty()) {
    goal_opt = ctx.get<std::string>("-g", "");
  }
  if (!goal_opt.empty()) {
    auto goal = parse_positive_int(goal_opt, "goal");
    if (!goal) return std::unexpected(goal.error());
    cfg.goal = *goal;
    if (!cfg.width_set) {
      cfg.width = cfg.goal + 10;
    }
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

auto read_input(const std::string& filename) -> cp::Result<std::string> {
  std::string content;
  auto input_open_error = [](std::string_view path) -> std::string {
    std::error_code ec;
    if (std::filesystem::is_directory(std::filesystem::u8path(path), ec) &&
        !ec) {
      return std::string("cannot open '") + std::string(path) +
             "' for reading: Is a directory";
    }

    return std::string("cannot open '") + std::string(path) +
           "' for reading: No such file or directory";
  };

  if (filename == "-") {
    content.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
  } else {
    // Read from file
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(input_open_error(filename));
    }
    // Read file content
    content.assign(std::istreambuf_iterator<char>(f),
                   std::istreambuf_iterator<char>());
    if (f.fail() && !f.eof()) {
      return std::unexpected("error reading from file");
    }
    // Skip UTF-8 BOM if present at the beginning
    if (content.size() >= 3 && static_cast<unsigned char>(content[0]) == 0xEF &&
        static_cast<unsigned char>(content[1]) == 0xBB &&
        static_cast<unsigned char>(content[2]) == 0xBF) {
      content = content.substr(3);
    }
  }

  return content;
}

struct Line {
  std::string text;
  bool has_newline = false;
};

auto split_lines(std::string_view content) -> std::vector<Line> {
  std::vector<Line> lines;
  size_t start = 0;
  while (start < content.size()) {
    size_t end = content.find('\n', start);
    if (end == std::string_view::npos) {
      std::string text(content.substr(start));
      if (!text.empty() && text.back() == '\r') text.pop_back();
      lines.push_back(Line{std::move(text), false});
      return lines;
    }

    std::string text(content.substr(start, end - start));
    if (!text.empty() && text.back() == '\r') text.pop_back();
    lines.push_back(Line{std::move(text), true});
    start = end + 1;
  }

  if (content.empty()) return lines;
  return lines;
}

auto leading_blank_count(std::string_view line) -> size_t {
  size_t count = 0;
  while (count < line.size() && (line[count] == ' ' || line[count] == '\t')) {
    ++count;
  }
  return count;
}

auto is_blank_line(std::string_view line) -> bool {
  return leading_blank_count(line) == line.size();
}

auto indentation_of(std::string_view line) -> std::string {
  return std::string(line.substr(0, leading_blank_count(line)));
}

auto prefix_match(std::string_view line, std::string_view prefix,
                  bool allow_leading_blank_match = true,
                  bool consume_attachment = true)
    -> std::optional<size_t> {
  size_t blanks = allow_leading_blank_match ? leading_blank_count(line) : 0;
  if (prefix.empty()) return 0;
  if (line.substr(blanks, prefix.size()) != prefix) return std::nullopt;
  size_t end = blanks + prefix.size();
  if (consume_attachment) {
    while (end < line.size() && (line[end] == ' ' || line[end] == '\t')) {
      ++end;
    }
  }
  return end;
}

auto collect_words(std::string_view text, std::vector<std::string>& words) {
  size_t pos = 0;
  while (pos < text.size()) {
    while (pos < text.size() &&
           (text[pos] == ' ' || text[pos] == '\t' || text[pos] == '\r')) {
      ++pos;
    }
    size_t start = pos;
    while (pos < text.size() && text[pos] != ' ' && text[pos] != '\t' &&
           text[pos] != '\r') {
      ++pos;
    }
    if (start < pos) words.emplace_back(text.substr(start, pos - start));
  }
}

auto ends_sentence(std::string_view word) -> bool {
  while (!word.empty()) {
    char c = word.back();
    if (c == ')' || c == ']' || c == '}' || c == '"' || c == '\'') {
      word.remove_suffix(1);
      continue;
    }
    return c == '.' || c == '?' || c == '!';
  }
  return false;
}

auto is_header_name_char(unsigned char ch) -> bool {
  return std::isalnum(ch) || ch == '-';
}

auto parse_mail_header(std::string_view line)
    -> std::optional<std::pair<std::string, std::string>> {
  if (line.empty() || line.front() == ' ' || line.front() == '\t') {
    return std::nullopt;
  }

  auto colon = line.find(':');
  if (colon == std::string_view::npos || colon == 0) {
    return std::nullopt;
  }

  for (size_t i = 0; i < colon; ++i) {
    if (!is_header_name_char(static_cast<unsigned char>(line[i]))) {
      return std::nullopt;
    }
  }

  std::string header_name(line.substr(0, colon + 1));
  std::string header_value(line.substr(colon + 1));
  return std::pair{std::move(header_name), std::move(header_value)};
}

auto is_header_continuation(std::string_view line) -> bool {
  return !line.empty() && (line.front() == ' ' || line.front() == '\t');
}

auto append_wrapped_words(std::string& out,
                          const std::vector<std::string>& words,
                          std::string_view first_indent,
                          std::string_view rest_indent, const Config& cfg,
                          bool use_full_width = false,
                          bool allow_overfull_tail = false,
                          bool prefer_optimal_wrapping = true) {
  if (words.empty()) {
    out.append(first_indent);
    out.push_back('\n');
    return;
  }

  int preferred_width =
      use_full_width ? cfg.width : (cfg.goal > 0 ? cfg.goal : (cfg.width * 93 / 100));
  preferred_width = std::max(1, std::min(preferred_width, cfg.width));

  std::string line;
  std::string_view indent = first_indent;
  bool previous_sentence = false;
  bool emitted_wrapped_line = false;

  auto display_width = [&](std::string_view text) -> size_t {
    size_t width = 0;
    for (char ch : text) {
      if (ch == '\t') {
        size_t tab_stop = static_cast<size_t>(std::max(cfg.tab_width, 1));
        width += tab_stop - (width % tab_stop);
      } else {
        ++width;
      }
    }
    return width;
  };

  auto available = [&](std::string_view active_indent, int width) -> size_t {
    size_t indent_width = display_width(active_indent);
    return indent_width >= static_cast<size_t>(width)
               ? 1
               : static_cast<size_t>(width) - indent_width;
  };

  auto squared_distance = [](size_t length, size_t target) -> size_t {
    auto diff = static_cast<long long>(length) - static_cast<long long>(target);
    return static_cast<size_t>(diff * diff);
  };

  if (prefer_optimal_wrapping && !allow_overfull_tail) {
    auto line_length = [&](size_t start, size_t end) -> size_t {
      size_t length = words[start].size();
      for (size_t i = start + 1; i <= end; ++i) {
        size_t separator =
            cfg.uniform_spacing && ends_sentence(words[i - 1]) ? 2u : 1u;
        length += separator + words[i].size();
      }
      return length;
    };

    const size_t word_count = words.size();
    std::vector<size_t> best_cost(word_count + 1, 0);
    std::vector<size_t> next_break(word_count, word_count);

    for (size_t start = word_count; start-- > 0;) {
      std::string_view active_indent = start == 0 ? first_indent : rest_indent;
      size_t preferred = available(active_indent, preferred_width);
      size_t maximum = available(active_indent, cfg.width);

      size_t chosen_end = start;
      size_t chosen_cost = std::numeric_limits<size_t>::max();
      for (size_t end = start; end < word_count; ++end) {
        size_t length = line_length(start, end);
        if (end > start && length > maximum) {
          break;
        }

        size_t continuation_cost = end + 1 < word_count ? best_cost[end + 1] : 0;
        size_t current_cost =
            end + 1 == word_count ? 0
                                  : squared_distance(length, preferred) +
                                        continuation_cost;
        if (current_cost < chosen_cost ||
            (current_cost == chosen_cost && end > chosen_end)) {
          chosen_cost = current_cost;
          chosen_end = end;
        }

        if (length > maximum) {
          break;
        }
      }

      best_cost[start] = chosen_cost;
      next_break[start] = chosen_end + 1;
    }

    size_t start = 0;
    std::string_view active_indent = first_indent;
    while (start < word_count) {
      size_t end = next_break[start];
      out.append(active_indent);
      out.append(words[start]);
      for (size_t i = start + 1; i < end; ++i) {
        out.append(cfg.uniform_spacing && ends_sentence(words[i - 1]) ? "  "
                                                                      : " ");
        out.append(words[i]);
      }
      out.push_back('\n');
      start = end;
      active_indent = rest_indent;
    }
    return;
  }

  for (size_t word_index = 0; word_index < words.size(); ++word_index) {
    const auto& word = words[word_index];
    std::string separator;
    if (!line.empty()) {
      separator.assign(cfg.uniform_spacing && previous_sentence ? 2 : 1, ' ');
    }

    size_t preferred = available(indent, preferred_width);
    size_t maximum = available(indent, cfg.width);
    size_t candidate = line.size() + separator.size() + word.size();
    bool should_break =
        !line.empty() && candidate > preferred && candidate <= maximum;
    if (!line.empty() && candidate > maximum) {
      bool keep_overfull = false;
      if (allow_overfull_tail) {
        size_t remaining_words = words.size() - word_index - 1;
        if (remaining_words == 1) {
          keep_overfull =
              squared_distance(candidate, preferred) <=
              squared_distance(line.size(), preferred);
        } else if (remaining_words == 0 && emitted_wrapped_line) {
          keep_overfull = true;
        }
      }
      should_break = !keep_overfull;
    }

    if (should_break) {
      out.append(indent);
      out.append(line);
      out.push_back('\n');
      indent = rest_indent;
      line.clear();
      separator.clear();
      emitted_wrapped_line = true;
    }

    if (!separator.empty()) line += separator;
    line += word;
    previous_sentence = cfg.uniform_spacing && ends_sentence(word);
  }

  if (!line.empty()) {
    out.append(indent);
    out.append(line);
    out.push_back('\n');
  }
}

auto format_mail_header(std::span<const Line> group, const Config& cfg)
    -> std::string {
  std::string out;
  if (group.empty()) return out;

  auto parsed_header = parse_mail_header(group.front().text);
  if (!parsed_header) {
    out += group.front().text;
    if (group.front().has_newline) out.push_back('\n');
    return out;
  }

  auto [header_name, header_value] = *parsed_header;
  std::vector<std::string> words;
  collect_words(header_value, words);
  for (size_t i = 1; i < group.size(); ++i) {
    collect_words(group[i].text, words);
  }

  std::string first_indent = header_name;
  if (!words.empty()) first_indent.push_back(' ');
  append_wrapped_words(out, words, first_indent, "  ", cfg, true, true);
  return out;
}

auto format_group(std::span<const Line> group, const Config& cfg,
                  std::string_view prefix_attachment) -> std::string {
  std::string out;
  if (group.empty()) return out;

  std::string first_indent(prefix_attachment);
  std::string rest_indent(prefix_attachment);
  bool crown_like = cfg.crown_margin || cfg.tagged_paragraph;

  if (prefix_attachment.empty()) {
    first_indent = indentation_of(group.front().text);
    rest_indent = first_indent;
    if (crown_like && group.size() > 1) {
      rest_indent = indentation_of(group[1].text);
    }
  }

  std::vector<std::string> words;
  for (size_t i = 0; i < group.size(); ++i) {
    std::string_view text = group[i].text;
    if (!prefix_attachment.empty()) {
      auto stripped = prefix_match(text, cfg.prefix);
      text.remove_prefix(stripped.value_or(0));
    } else if (crown_like) {
      text.remove_prefix(std::min(leading_blank_count(text), text.size()));
    } else {
      auto indent_size = first_indent.size();
      if (text.substr(0, indent_size) == first_indent) {
        text.remove_prefix(indent_size);
      } else {
        text.remove_prefix(std::min(leading_blank_count(text), text.size()));
      }
    }
    collect_words(text, words);
  }

  append_wrapped_words(out, words, first_indent, rest_indent, cfg,
                       cfg.preserve_headers || !prefix_attachment.empty(),
                       !cfg.skip_prefix.empty(), !crown_like);
  return out;
}

auto split_only_line(std::string_view line, const Config& cfg,
                     std::string_view prefix_attachment,
                     std::optional<size_t> prefix_strip) -> std::string {
  std::string indent(prefix_attachment);
  std::string text(line);
  if (prefix_strip) {
    text.erase(0, *prefix_strip);
  } else {
    indent = indentation_of(text);
    text.erase(0, indent.size());
  }

  std::vector<std::string> words;
  collect_words(text, words);
  std::string out;
  append_wrapped_words(out, words, indent, indent, cfg,
                       cfg.preserve_headers || !prefix_attachment.empty(),
                       !cfg.skip_prefix.empty());
  return out;
}

auto format_content(const std::string& content, const Config& cfg)
    -> std::string {
  auto lines = split_lines(content);
  std::string out;
  std::vector<Line> paragraph;
  std::string paragraph_indent;
  std::string paragraph_prefix;

  auto flush = [&](bool final_flush = false) {
    if (paragraph.empty()) return;

    if (!cfg.prefix.empty() && paragraph.size() == 1 && final_flush) {
      out += paragraph.front().text;
      if (paragraph.front().has_newline) out.push_back('\n');
    } else if (cfg.tagged_paragraph && paragraph.size() > 1 &&
               cfg.prefix.empty() &&
        indentation_of(paragraph[0].text) ==
            indentation_of(paragraph[1].text)) {
      out += format_group(std::span<const Line>(paragraph.data(), 1), cfg, "");
      out += format_group(
          std::span<const Line>(paragraph.data() + 1, paragraph.size() - 1),
          cfg, "");
    } else {
      out += format_group(paragraph, cfg, paragraph_prefix);
    }

    paragraph.clear();
    paragraph_indent.clear();
    paragraph_prefix.clear();
  };

  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];
    if (cfg.preserve_headers) {
      if (parse_mail_header(line.text)) {
        flush();
        std::vector<Line> header_group;
        header_group.push_back(line);

        size_t continuation_index = i + 1;
        while (continuation_index < lines.size() &&
               is_header_continuation(lines[continuation_index].text)) {
          header_group.push_back(lines[continuation_index]);
          ++continuation_index;
        }

        out += format_mail_header(header_group, cfg);
        i = continuation_index - 1;
        continue;
      }
    }

    auto skip_prefix_match =
        prefix_match(line.text, cfg.skip_prefix,
                     false /* observed Microsoft behavior is exact here */,
                     false);
    if (!cfg.skip_prefix.empty() && skip_prefix_match) {
      flush();
      out += line.text;
      if (line.has_newline) out.push_back('\n');
      continue;
    }

    auto strip_prefix =
        prefix_match(line.text, cfg.prefix, !cfg.exact_prefix, true);
    if (!cfg.prefix.empty() && !strip_prefix) {
      flush();
      out += line.text;
      if (line.has_newline) out.push_back('\n');
      continue;
    }

    if (is_blank_line(line.text)) {
      flush();
      out += line.text;
      if (line.has_newline) out.push_back('\n');
      continue;
    }

    std::string current_prefix;
    if (!cfg.prefix.empty()) {
      current_prefix = line.text.substr(0, *strip_prefix);
    }

    if (cfg.split_only) {
      flush();
      out += split_only_line(line.text, cfg, current_prefix, strip_prefix);
      continue;
    }

    std::string current_indent =
        cfg.prefix.empty() || cfg.crown_margin || cfg.tagged_paragraph
            ? indentation_of(line.text)
            : current_prefix;

    bool starts_new_paragraph = false;
    if (!paragraph.empty()) {
      if (!cfg.prefix.empty()) {
        starts_new_paragraph = current_prefix != paragraph_prefix;
      } else if (!cfg.crown_margin && !cfg.tagged_paragraph) {
        starts_new_paragraph = current_indent != paragraph_indent;
      }
    }
    if (starts_new_paragraph) flush();

    if (paragraph.empty()) {
      paragraph_indent = current_indent;
      paragraph_prefix = current_prefix;
    }
    paragraph.push_back(line);
  }

  flush(true);
  return out;
}

auto run(const Config& cfg) -> int {
  bool all_ok = true;

  for (const auto& file : cfg.files) {
    auto content_result = read_input(file);
    if (!content_result) {
      cp::report_error(content_result, L"fmt");
      all_ok = false;
      continue;
    }

    safePrint(format_content(*content_result, cfg));
  }

  return all_ok ? 0 : 1;
}

}  // namespace fmt_pipeline

REGISTER_COMMAND(
    fmt, "fmt", "fmt [OPTION]... [FILE]...",
    "Reformat paragraphs.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "Note: This is a simplified implementation. Advanced features\n"
    "like crown margin and tagged paragraphs are not fully supported.",
    "  fmt file.txt\n"
    "  fmt -w 60 file.txt\n"
    "  fmt -s file.txt",
    "fold(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", FMT_OPTIONS) {
  using namespace fmt_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"fmt");
    return 1;
  }

  return run(*cfg_result);
}
