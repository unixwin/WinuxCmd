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
 *  - File: ptx.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for ptx.
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

auto constexpr PTX_OPTIONS = std::array{
    OPTION("-A", "--auto-reference",
           "output automatically generated references", BOOL_TYPE),
    OPTION("-C", "--copyright", "display copyright and version", BOOL_TYPE),
    OPTION("-G", "--traditional", "behave more like System V 'ptx'", BOOL_TYPE),
    OPTION("-F", "--flag-truncation", "truncate words flagged by 'g'",
           STRING_TYPE),
    OPTION("-M", "--macro-name", "macro name for missing file", STRING_TYPE),
    OPTION("-O", "--format", "roff format for output", BOOL_TYPE),
    OPTION("-R", "--right-side-refs", "put references in right margin",
           BOOL_TYPE),
    OPTION("-S", "--sentence-regexp", "regexp for sentence ends", STRING_TYPE),
    OPTION("-T", "--tabs", "tabs in output", STRING_TYPE),
    OPTION("-W", "--word-regexp", "regexp for words", STRING_TYPE),
    OPTION("-b", "--break", "file break character", STRING_TYPE),
    OPTION("-f", "--ignore-case", "fold lower case to upper case for sorting",
           BOOL_TYPE),
    OPTION("-g", "--gap-size", "gap size for output", STRING_TYPE),
    OPTION("-i", "--ignore-file", "ignore file", STRING_TYPE),
    OPTION("-o", "--only-file", "output only file", BOOL_TYPE),
    OPTION("-r", "--references", "first field is reference", BOOL_TYPE),
    OPTION("-t", "--typeset-mode", "output for troff/nroff", BOOL_TYPE),
    OPTION("-w", "--width", "output width", STRING_TYPE)};

namespace ptx_pipeline {
namespace cp = core::pipeline;

struct WordEntry {
  std::string word;
  int line_number;
  std::string line_context;
  int position;
};

struct Config {
  bool auto_reference = false;
  bool copyright = false;
  bool traditional = false;
  std::string flag_truncation;
  std::string macro_name;
  bool roff_format = false;
  bool right_side_refs = false;
  std::string sentence_regexp;
  std::string tabs;
  std::string word_regexp;
  std::string break_char;
  bool ignore_case = false;
  std::string gap_size;
  std::string ignore_file;
  bool only_file = false;
  bool references = false;
  bool typeset_mode = false;
  int width = 72;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<PTX_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.auto_reference =
      ctx.get<bool>("--auto-reference", false) || ctx.get<bool>("-A", false);
  cfg.copyright =
      ctx.get<bool>("--copyright", false) || ctx.get<bool>("-C", false);
  cfg.traditional =
      ctx.get<bool>("--traditional", false) || ctx.get<bool>("-G", false);
  cfg.roff_format =
      ctx.get<bool>("--format", false) || ctx.get<bool>("-O", false);
  cfg.right_side_refs =
      ctx.get<bool>("--right-side-refs", false) || ctx.get<bool>("-R", false);
  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-f", false);
  cfg.only_file =
      ctx.get<bool>("--only-file", false) || ctx.get<bool>("-o", false);
  cfg.references =
      ctx.get<bool>("--references", false) || ctx.get<bool>("-r", false);
  cfg.typeset_mode =
      ctx.get<bool>("--typeset-mode", false) || ctx.get<bool>("-t", false);

  auto trunc_opt = ctx.get<std::string>("--flag-truncation", "");
  if (trunc_opt.empty()) {
    trunc_opt = ctx.get<std::string>("-F", "");
  }
  cfg.flag_truncation = trunc_opt;

  auto macro_opt = ctx.get<std::string>("--macro-name", "");
  if (macro_opt.empty()) {
    macro_opt = ctx.get<std::string>("-M", "");
  }
  cfg.macro_name = macro_opt;

  auto sent_opt = ctx.get<std::string>("--sentence-regexp", "");
  if (sent_opt.empty()) {
    sent_opt = ctx.get<std::string>("-S", "");
  }
  cfg.sentence_regexp = sent_opt;

  auto tabs_opt = ctx.get<std::string>("--tabs", "");
  if (tabs_opt.empty()) {
    tabs_opt = ctx.get<std::string>("-T", "");
  }
  cfg.tabs = tabs_opt;

  auto word_opt = ctx.get<std::string>("--word-regexp", "");
  if (word_opt.empty()) {
    word_opt = ctx.get<std::string>("-W", "");
  }
  cfg.word_regexp = word_opt;

  auto break_opt = ctx.get<std::string>("--break", "");
  if (break_opt.empty()) {
    break_opt = ctx.get<std::string>("-b", "");
  }
  cfg.break_char = break_opt;

  auto gap_opt = ctx.get<std::string>("--gap-size", "");
  if (gap_opt.empty()) {
    gap_opt = ctx.get<std::string>("-g", "");
  }
  cfg.gap_size = gap_opt;

  auto ignore_opt = ctx.get<std::string>("--ignore-file", "");
  if (ignore_opt.empty()) {
    ignore_opt = ctx.get<std::string>("-i", "");
  }
  cfg.ignore_file = ignore_opt;

  auto width_opt = ctx.get<std::string>("--width", "");
  if (width_opt.empty()) {
    width_opt = ctx.get<std::string>("-w", "");
  }
  if (!width_opt.empty()) {
    try {
      cfg.width = std::stoi(width_opt);
    } catch (...) {
      return std::unexpected("invalid width value");
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

auto read_file_content(const std::string& filename)
    -> std::vector<std::string> {
  std::vector<std::string> lines;

  if (filename == "-") {
    // Read from stdin
    std::string line;
    while (std::getline(std::cin, line)) {
      if (!line.empty() && line.back() == '\r') {
        line.pop_back();
      }
      lines.push_back(line);
    }
  } else {
    // Read from file
    lines = read_file_lines(filename);
  }

  return lines;
}

auto extract_words(const std::vector<std::string>& lines, bool ignore_case)
    -> std::vector<WordEntry> {
  std::vector<WordEntry> entries;

  for (size_t line_num = 0; line_num < lines.size(); ++line_num) {
    const std::string& line = lines[line_num];
    std::string word;

    for (size_t i = 0; i <= line.size(); ++i) {
      if (i < line.size() &&
          std::isalpha(static_cast<unsigned char>(line[i]))) {
        word += line[i];
      } else {
        if (!word.empty()) {
          std::string word_key = word;
          if (ignore_case) {
            std::transform(word_key.begin(), word_key.end(), word_key.begin(),
                           ::tolower);
          }

          WordEntry entry;
          entry.word = word_key;
          entry.line_number = static_cast<int>(line_num + 1);
          entry.line_context = line;
          entry.position = static_cast<int>(i) - static_cast<int>(word.size());

          entries.push_back(entry);
          word.clear();
        }
      }
    }
  }

  return entries;
}

auto compare_entries(const WordEntry& a, const WordEntry& b, bool ignore_case)
    -> bool {
  std::string a_word = a.word;
  std::string b_word = b.word;

  if (ignore_case) {
    std::transform(a_word.begin(), a_word.end(), a_word.begin(), ::tolower);
    std::transform(b_word.begin(), b_word.end(), b_word.begin(), ::tolower);
  }

  if (a_word != b_word) {
    return a_word < b_word;
  }

  // If words are equal, sort by line number
  return a.line_number < b.line_number;
}

auto format_output(const std::vector<WordEntry>& entries, const Config& cfg)
    -> void {
  for (const auto& entry : entries) {
    std::string output;

    if (cfg.right_side_refs) {
      // Word context, then reference on right
      output = entry.line_context;
      output += "\t(" + std::to_string(entry.line_number) + ")";
    } else {
      // Reference, then word context
      output = "(" + std::to_string(entry.line_number) + ")\t";
      output += entry.line_context;
    }

    safePrintLn(output);
  }
}

auto run(const Config& cfg) -> int {
  std::vector<WordEntry> all_entries;

  for (const auto& file : cfg.files) {
    auto lines = read_file_content(file);
    if (lines.empty()) {
      continue;
    }

    auto entries = extract_words(lines, cfg.ignore_case);
    all_entries.insert(all_entries.end(), entries.begin(), entries.end());
  }

  // Sort entries
  std::sort(all_entries.begin(), all_entries.end(),
            [&cfg](const WordEntry& a, const WordEntry& b) {
              return compare_entries(a, b, cfg.ignore_case);
            });

  // Output
  format_output(all_entries, cfg);

  return 0;
}

}  // namespace ptx_pipeline

REGISTER_COMMAND(
    ptx, "ptx", "ptx [OPTION]... [FILE]...",
    "Produce a permuted index of file contents.\n"
    "\n"
    "Output a permuted index, including context, of the words in the given "
    "files.\n"
    "\n"
    "Note: This is an advanced command. Full implementation is not provided\n"
    "due to its complexity. This is mainly used for building book indexes.",
    "  ptx file.txt\n"
    "  ptx -w file.txt",
    "grep(1), sort(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", PTX_OPTIONS) {
  using namespace ptx_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"ptx");
    return 1;
  }

  return run(*cfg_result);
}
