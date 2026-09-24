/*
 * Copyright 2026 WinuxCmd
 * ptx compatible subset based on GNU coreutils ptx data flow.
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PTX_OPTIONS = std::array{
    // [GNU]
    OPTION("-A", "--auto-reference",
           "output automatically generated references", BOOL_TYPE),
    // [GNU]
    OPTION("-C", "--copyright", "display copyright and version", BOOL_TYPE),
    // [GNU]
    OPTION("-G", "--traditional", "use traditional non-GNU word splitting",
           BOOL_TYPE),
    // [GNU]
    OPTION("-F", "--flag-truncation", "string used for line truncation",
           STRING_TYPE),
    // [GNU]
    OPTION("-M", "--macro-name", "macro name for formatted output",
           STRING_TYPE),
    // [GNU]
    OPTION("-O", "",
           "generate output as roff directives [DIFFERS: --format=roff not "
           "supported]",
           BOOL_TYPE),
    // [GNU]
    OPTION("-R", "--right-side-refs", "put references in right margin",
           BOOL_TYPE),
    // [GNU]
    OPTION("-S", "--sentence-regexp", "regexp for sentence ends", STRING_TYPE),
    // [GNU]
    OPTION("-T", "",
           "generate output as TeX directives [DIFFERS: --format=tex not "
           "supported]",
           BOOL_TYPE),
    // [GNU]
    OPTION("-W", "--word-regexp", "regexp for words", STRING_TYPE),
    // [GNU]
    OPTION("-b", "--break-file", "file containing word break characters",
           STRING_TYPE),
    // [GNU]
    OPTION("-f", "--ignore-case", "fold lower case to upper case for sorting",
           BOOL_TYPE),
    // [GNU]
    OPTION("-g", "--gap-size", "gap size in columns between output fields",
           INT_TYPE),
    // [GNU]
    OPTION("-i", "--ignore-file", "ignore words from file", STRING_TYPE),
    // [GNU]
    OPTION("-o", "--only-file", "only output words from file", STRING_TYPE),
    // [GNU]
    OPTION("-r", "--references", "first input field is reference", BOOL_TYPE),
    // [GNU]
    OPTION("-t", "--typeset-mode", "output for troff or nroff", BOOL_TYPE),
    // [GNU]
    OPTION("-w", "--width", "output width in columns, reference excluded",
           INT_TYPE)};

namespace ptx_pipeline {
namespace cp = core::pipeline;

enum class OutputFormat { Dumb, Roff, Tex };

auto make_error(std::string message) -> cp::Error {
  static thread_local std::string storage;
  storage = std::move(message);
  return storage;
}

struct Config {
  bool auto_reference = false;
  bool copyright = false;
  bool traditional = false;
  std::string truncation = "/";
  std::string macro_name = "xx";
  OutputFormat output_format = OutputFormat::Dumb;
  bool right_side_refs = false;
  std::string sentence_regexp;
  std::string word_regexp;
  std::string break_file;
  bool ignore_case = false;
  int gap_size = 3;
  std::string ignore_file;
  std::string only_file;
  bool input_references = false;
  int width = 78;
  SmallVector<std::string, 64> files;
};

struct SourceText {
  std::string name;
  std::string text;
  std::vector<size_t> line_starts{0};
};

struct Occurrence {
  size_t source_index = 0;
  size_t start = 0;
  size_t end = 0;
  size_t context_start = 0;
  size_t context_end = 0;
  size_t line_number = 1;
  std::string key;
};

auto option_value(const CommandContext<PTX_OPTIONS.size()>& ctx,
                  std::string_view long_name, std::string_view short_name)
    -> std::string {
  auto value = ctx.get<std::string>(std::string(long_name), "");
  if (value.empty()) value = ctx.get<std::string>(std::string(short_name), "");
  return value;
}

auto parse_int_option(const std::string& value, int fallback,
                      std::string_view label) -> cp::Result<int> {
  if (value.empty()) return fallback;
  int parsed = 0;
  auto [ptr, ec] =
      std::from_chars(value.data(), value.data() + value.size(), parsed);
  if (ec != std::errc() || ptr != value.data() + value.size()) {
    return std::unexpected(
        make_error("invalid " + std::string(label) + " value"));
  }
  return parsed;
}

auto build_config(const CommandContext<PTX_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.auto_reference =
      ctx.get<bool>("--auto-reference", false) || ctx.get<bool>("-A", false);
  cfg.copyright =
      ctx.get<bool>("--copyright", false) || ctx.get<bool>("-C", false);
  cfg.traditional =
      ctx.get<bool>("--traditional", false) || ctx.get<bool>("-G", false);
  cfg.right_side_refs =
      ctx.get<bool>("--right-side-refs", false) || ctx.get<bool>("-R", false);
  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-f", false);
  cfg.input_references =
      ctx.get<bool>("--references", false) || ctx.get<bool>("-r", false);

  if (ctx.get<bool>("-O", false) || ctx.get<bool>("--typeset-mode", false) ||
      ctx.get<bool>("-t", false)) {
    cfg.output_format = OutputFormat::Roff;
  }
  if (ctx.get<bool>("-T", false)) {
    cfg.output_format = OutputFormat::Tex;
  }

  cfg.truncation = option_value(ctx, "--flag-truncation", "-F");
  if (cfg.truncation.empty()) cfg.truncation = "/";
  cfg.macro_name = option_value(ctx, "--macro-name", "-M");
  if (cfg.macro_name.empty()) cfg.macro_name = "xx";
  cfg.sentence_regexp = option_value(ctx, "--sentence-regexp", "-S");
  cfg.word_regexp = option_value(ctx, "--word-regexp", "-W");
  cfg.break_file = option_value(ctx, "--break-file", "-b");

  // [GNU] the -S/-W operands are compiled as regular expressions; an invalid
  // one is a fatal "Invalid regular expression (for regexp 'X')" error before
  // any file is read (ptx.c compile_regex, uutils #12790).
  {
    auto flags = std::regex_constants::basic;
    if (cfg.ignore_case) flags |= std::regex_constants::icase;
    for (const auto* pattern : {&cfg.sentence_regexp, &cfg.word_regexp}) {
      if (pattern->empty()) continue;
      try {
        std::regex compiled(*pattern, flags);
        (void)compiled;
      } catch (const std::regex_error&) {
        return std::unexpected(make_error(
            "Invalid regular expression (for regexp '" + *pattern + "')"));
      }
    }
  }
  cfg.ignore_file = option_value(ctx, "--ignore-file", "-i");
  cfg.only_file = option_value(ctx, "--only-file", "-o");

  cfg.gap_size = std::max(0, ctx.get<int>("--gap-size", ctx.get<int>("-g", 3)));
  if (cfg.gap_size < 1) {
    // [GNU] ptx rejects a gap size below 1 (uutils #13794)
    std::string raw = option_value(ctx, "--gap-size", "-g");
    if (raw.empty()) raw = std::to_string(cfg.gap_size);
    return std::unexpected(make_error("invalid gap width: '" + raw + "'"));
  }

  cfg.width = std::max(1, ctx.get<int>("--width", ctx.get<int>("-w", 78)));

  for (auto arg : ctx.positionals) cfg.files.push_back(std::string(arg));
  if (cfg.files.empty()) cfg.files.push_back("-");
  return cfg;
}

auto read_all(const std::string& filename) -> std::optional<std::string> {
  if (filename == "-") {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
  }
  auto input = file_io::open_binary_file(filename);
  if (!input) return std::nullopt;
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return buffer.str();
}

auto lower_copy(std::string value) -> std::string {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
  return value;
}

auto key_for(const std::string& value, bool ignore_case) -> std::string {
  return ignore_case ? lower_copy(value) : value;
}

auto is_word_char(unsigned char ch, const std::set<unsigned char>& breaks,
                  bool traditional) -> bool {
  if (!breaks.empty()) return breaks.find(ch) == breaks.end();
  if (traditional) return !std::isspace(ch);
  return std::isalpha(ch) != 0;
}

auto read_break_chars(const std::string& file) -> std::set<unsigned char> {
  std::set<unsigned char> breaks;
  if (file.empty()) return breaks;
  if (auto data = read_all(file)) {
    for (unsigned char ch : *data) breaks.insert(ch);
  }
  return breaks;
}

auto word_set_from_file(const std::string& file, bool ignore_case)
    -> std::set<std::string> {
  std::set<std::string> words;
  if (file.empty()) return words;
  auto data = read_all(file);
  if (!data) return words;
  std::string current;
  for (unsigned char ch : *data) {
    if (std::isalpha(ch)) {
      current.push_back(static_cast<char>(ch));
    } else if (!current.empty()) {
      words.insert(key_for(current, ignore_case));
      current.clear();
    }
  }
  if (!current.empty()) words.insert(key_for(current, ignore_case));
  return words;
}

auto trim_left(std::string value) -> std::string {
  size_t pos = 0;
  while (pos < value.size() &&
         std::isspace(static_cast<unsigned char>(value[pos]))) {
    ++pos;
  }
  value.erase(0, pos);
  return value;
}

auto trim_right(std::string value) -> std::string {
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

auto printable(std::string value) -> std::string {
  for (char& ch : value) {
    if (std::isspace(static_cast<unsigned char>(ch)))
      ch = static_cast<char>(32);
  }
  return value;
}

auto rightmost_field(std::string value, size_t limit)
    -> std::pair<std::string, bool> {
  value = trim_right(printable(value));
  if (value.size() <= limit) return {trim_left(value), false};
  size_t start = value.size() - limit;
  while (start < value.size() &&
         !std::isspace(static_cast<unsigned char>(value[start]))) {
    ++start;
  }
  if (start >= value.size()) start = value.size() - limit;
  return {trim_left(value.substr(start)), true};
}

auto leftmost_field(std::string value, size_t limit)
    -> std::pair<std::string, bool> {
  value = trim_left(printable(value));
  if (value.size() <= limit) return {trim_right(value), false};
  size_t end = limit;
  while (end > 0 && !std::isspace(static_cast<unsigned char>(value[end - 1]))) {
    --end;
  }
  if (end == 0) end = limit;
  return {trim_right(value.substr(0, end)), true};
}

auto line_number_for(const SourceText& source, size_t pos) -> size_t {
  auto it = std::upper_bound(source.line_starts.begin(),
                             source.line_starts.end(), pos);
  return static_cast<size_t>(std::distance(source.line_starts.begin(), it));
}

auto load_sources(const Config& cfg) -> cp::Result<std::vector<SourceText>> {
  std::vector<SourceText> sources;
  for (const auto& file : cfg.files) {
    auto data = read_all(file);
    if (!data) {
      std::string q(1, static_cast<char>(39));
      // [GNU] cannot-open diagnostics carry the errno text; directories
      // are reported GNU-style ("<path>: Is a directory").
      auto operand = native_path::make_api_path_operand(file);
      const DWORD attrs = native_path::operand_target_attributes_w(operand);
      if (native_path::attributes_are_directory(attrs)) {
        return std::unexpected(make_error(file + ": Is a directory"));
      }
      return std::unexpected(make_error("cannot open " + q + file + q +
                                        " for reading: No such file or "
                                        "directory"));
    }
    SourceText source;
    source.name = file == "-" ? "" : file;
    source.text = std::move(*data);
    for (size_t i = 0; i < source.text.size(); ++i) {
      if (static_cast<unsigned char>(source.text[i]) == 10 &&
          i + 1 < source.text.size()) {
        source.line_starts.push_back(i + 1);
      }
    }
    sources.push_back(std::move(source));
  }
  return sources;
}

void collect_occurrences(const SourceText& source, size_t source_index,
                         const Config& cfg,
                         const std::set<unsigned char>& breaks,
                         const std::set<std::string>& ignore_words,
                         const std::set<std::string>& only_words,
                         std::vector<Occurrence>& out) {
  const auto& text = source.text;
  size_t pos = 0;
  const size_t text_end = trim_right(text).size();
  while (pos < text_end) {
    while (pos < text_end &&
           !is_word_char(static_cast<unsigned char>(text[pos]), breaks,
                         cfg.traditional)) {
      ++pos;
    }
    if (pos >= text_end) break;
    size_t start = pos;
    while (pos < text_end && is_word_char(static_cast<unsigned char>(text[pos]),
                                          breaks, cfg.traditional)) {
      ++pos;
    }
    size_t end = pos;
    std::string word = text.substr(start, end - start);
    std::string lookup = key_for(word, cfg.ignore_case);
    if (!ignore_words.empty() && ignore_words.contains(lookup)) continue;
    if (!only_words.empty() && !only_words.contains(lookup)) continue;
    const size_t line_number = line_number_for(source, start);
    const size_t context_start = source.line_starts[line_number - 1];
    const size_t next_line = line_number < source.line_starts.size()
                                 ? source.line_starts[line_number]
                                 : text.size();
    const size_t context_end = std::min(next_line, text_end);
    out.push_back(Occurrence{source_index, start, end, context_start,
                             context_end, line_number, lookup});
  }
}

auto reference_text(const SourceText& source, const Occurrence& occ)
    -> std::string {
  if (source.name.empty()) return std::to_string(occ.line_number);
  return source.name + ":" + std::to_string(occ.line_number);
}

void append_spaces(std::string& line, int count) {
  if (count > 0) line.append(static_cast<size_t>(count), static_cast<char>(32));
}

void emit_dumb_line(const SourceText& source, const Occurrence& occ,
                    const Config& cfg) {
  int width = std::max(1, cfg.width);
  int gap = cfg.gap_size;
  std::string ref;
  int ref_width = 0;
  if (cfg.auto_reference) {
    ref = reference_text(source, occ);
    ref_width = static_cast<int>(ref.size()) + 1;
    width = std::max(0, width - (ref_width + gap));
  }

  const int half = std::max(1, width / 2);
  const int trunc = static_cast<int>(cfg.truncation.size());
  const int before_limit = std::max(0, half - gap - 2 * trunc);
  const int key_limit = std::max(1, half - 2 * trunc);

  auto before_pair = rightmost_field(
      source.text.substr(occ.context_start, occ.start - occ.context_start),
      static_cast<size_t>(before_limit));
  auto key_pair =
      leftmost_field(source.text.substr(occ.start, occ.context_end - occ.start),
                     static_cast<size_t>(key_limit));

  std::string before = before_pair.first;
  std::string keyafter = key_pair.first;
  bool before_truncated = before_pair.second;
  bool key_truncated = key_pair.second;

  std::string line;
  if (cfg.auto_reference && !cfg.right_side_refs) {
    line += ref + ":";
    append_spaces(line, ref_width + gap - static_cast<int>(ref.size()) - 1);
  }

  append_spaces(line, half - gap - static_cast<int>(before.size()) -
                          (before_truncated ? trunc : 0));
  if (before_truncated) line += cfg.truncation;
  line += before;
  append_spaces(line, gap);
  line += keyafter;
  if (key_truncated) line += cfg.truncation;

  if (cfg.auto_reference && cfg.right_side_refs) {
    append_spaces(line, half - static_cast<int>(keyafter.size()) -
                            (key_truncated ? trunc : 0));
    append_spaces(line, gap);
    line += ref;
  }
  safePrintLn(line);
}

void emit_roff_line(const SourceText& source, const Occurrence& occ,
                    const Config& cfg) {
  std::string before = printable(trim_left(trim_right(
      source.text.substr(occ.context_start, occ.start - occ.context_start))));
  std::string keyafter = printable(trim_left(
      trim_right(source.text.substr(occ.start, occ.context_end - occ.start))));
  safePrintLn("." + cfg.macro_name + " \"\" \"" + before + "\" \"" + keyafter +
              "\" \"\"");
}

void emit_tex_line(const SourceText& source, const Occurrence& occ,
                   const Config& cfg) {
  std::string before = printable(trim_left(trim_right(
      source.text.substr(occ.context_start, occ.start - occ.context_start))));
  std::string keyafter = printable(trim_left(
      trim_right(source.text.substr(occ.start, occ.context_end - occ.start))));
  safePrintLn("\\" + cfg.macro_name + " {}{" + before + "}{" + keyafter +
              "}{}");
}

auto run(const Config& cfg) -> int {
  if (cfg.copyright) {
    safePrintLn("ptx (GNU coreutils compatible subset) 0.1.0");
    safePrintLn("Copyright 2026 WinuxCmd");
    return 0;
  }

  auto sources_result = load_sources(cfg);
  if (!sources_result) {
    safeErrorPrintLn(std::string("ptx: ") +
                     std::string(sources_result.error()));
    return 1;
  }
  auto sources = std::move(*sources_result);
  auto breaks = read_break_chars(cfg.break_file);
  auto ignore_words = word_set_from_file(cfg.ignore_file, cfg.ignore_case);
  auto only_words = word_set_from_file(cfg.only_file, cfg.ignore_case);

  std::vector<Occurrence> occurrences;
  for (size_t i = 0; i < sources.size(); ++i) {
    collect_occurrences(sources[i], i, cfg, breaks, ignore_words, only_words,
                        occurrences);
  }

  std::sort(occurrences.begin(), occurrences.end(),
            [&](const auto& a, const auto& b) {
              if (a.key != b.key) return a.key < b.key;
              if (a.source_index != b.source_index)
                return a.source_index < b.source_index;
              return a.start < b.start;
            });

  for (const auto& occ : occurrences) {
    const auto& source = sources[occ.source_index];
    switch (cfg.output_format) {
      case OutputFormat::Dumb:
        emit_dumb_line(source, occ, cfg);
        break;
      case OutputFormat::Roff:
        emit_roff_line(source, occ, cfg);
        break;
      case OutputFormat::Tex:
        emit_tex_line(source, occ, cfg);
        break;
    }
  }
  return 0;
}

}  // namespace ptx_pipeline

REGISTER_COMMAND(ptx, "ptx", "ptx [OPTION]... [FILE]...",
                 "Produce a permuted index of file contents.",
                 "  ptx file.txt\n  ptx -w 40 file.txt", "grep(1), sort(1)",
                 "WinuxCmd", "Copyright 2026 WinuxCmd", PTX_OPTIONS) {
  using namespace ptx_pipeline;
  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"ptx");
    return 1;
  }
  return run(*cfg_result);
}
