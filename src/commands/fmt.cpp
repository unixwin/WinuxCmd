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
    OPTION("-p", "--prefix", "reformat only lines beginning with STRING",
           STRING_TYPE),
    OPTION("-s", "--split-only", "split long lines, but do not refill",
           BOOL_TYPE),
    OPTION("-t", "--tagged-paragraph", "expect indentation in first 2 lines",
           BOOL_TYPE),
    OPTION("-u", "--uniform-spacing",
           "one space between words, two after sentences", BOOL_TYPE),
    OPTION("-w", "--width", "maximum line width (default 75)", STRING_TYPE),
    OPTION("-g", "--goal", "goal width (default 93% of width)", STRING_TYPE)};

namespace fmt_pipeline {
namespace cp = core::pipeline;

struct Config {
  bool crown_margin = false;
  bool split_only = false;
  bool tagged_paragraph = false;
  bool uniform_spacing = false;
  int width = 75;
  int goal = 0;  // Will be calculated as 93% of width
  bool width_set = false;
  std::string prefix;
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

  if (filename == "-") {
    content.assign(std::istreambuf_iterator<char>(std::cin),
                   std::istreambuf_iterator<char>());
  } else {
    // Read from file
    std::ifstream f(filename, std::ios::binary);
    if (!f) {
      return std::unexpected(std::string("cannot open '") + filename +
                             "' for reading");
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

auto prefix_match(std::string_view line, std::string_view prefix)
    -> std::optional<size_t> {
  size_t blanks = leading_blank_count(line);
  if (prefix.empty()) return 0;
  if (line.substr(blanks, prefix.size()) != prefix) return std::nullopt;
  return blanks + prefix.size();
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

auto append_wrapped_words(std::string& out,
                          const std::vector<std::string>& words,
                          std::string_view first_indent,
                          std::string_view rest_indent, const Config& cfg) {
  if (words.empty()) {
    out.append(first_indent);
    out.push_back('\n');
    return;
  }

  int preferred_width = cfg.goal > 0 ? cfg.goal : (cfg.width * 93 / 100);
  preferred_width = std::max(1, std::min(preferred_width, cfg.width));

  std::string line;
  std::string_view indent = first_indent;
  bool previous_sentence = false;

  auto available = [&](std::string_view active_indent, int width) -> size_t {
    size_t indent_width = active_indent.size();
    return indent_width >= static_cast<size_t>(width)
               ? 1
               : static_cast<size_t>(width) - indent_width;
  };

  for (const auto& word : words) {
    std::string separator;
    if (!line.empty()) {
      separator.assign(cfg.uniform_spacing && previous_sentence ? 2 : 1, ' ');
    }

    size_t preferred = available(indent, preferred_width);
    size_t maximum = available(indent, cfg.width);
    size_t candidate = line.size() + separator.size() + word.size();
    bool should_break =
        !line.empty() && candidate > preferred && candidate <= maximum;
    if (!line.empty() && candidate > maximum) should_break = true;

    if (should_break) {
      out.append(indent);
      out.append(line);
      out.push_back('\n');
      indent = rest_indent;
      line.clear();
      separator.clear();
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

  append_wrapped_words(out, words, first_indent, rest_indent, cfg);
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
  append_wrapped_words(out, words, indent, indent, cfg);
  return out;
}

auto format_content(const std::string& content, const Config& cfg)
    -> std::string {
  auto lines = split_lines(content);
  std::string out;
  std::vector<Line> paragraph;
  std::string paragraph_indent;
  std::string paragraph_prefix;

  auto flush = [&]() {
    if (paragraph.empty()) return;

    if (cfg.tagged_paragraph && paragraph.size() > 1 && cfg.prefix.empty() &&
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

  for (const auto& line : lines) {
    auto strip_prefix = prefix_match(line.text, cfg.prefix);
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

    bool starts_new_paragraph = !paragraph.empty() && cfg.prefix.empty() &&
                                !cfg.crown_margin && !cfg.tagged_paragraph &&
                                current_indent != paragraph_indent;
    if (starts_new_paragraph) flush();

    if (paragraph.empty()) {
      paragraph_indent = current_indent;
      paragraph_prefix = current_prefix;
    }
    paragraph.push_back(line);
  }

  flush();
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
