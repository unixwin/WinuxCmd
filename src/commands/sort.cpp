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
 *  - File: sort.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

/// @Author: WinuxCmd
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implemention for sort.
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

auto constexpr SORT_OPTIONS = std::array{
    OPTION("-b", "--ignore-leading-blanks", "ignore leading blanks"),
    OPTION("-d", "--dictionary-order",
           "consider only blanks and alphanumeric characters"),
    OPTION("-f", "--ignore-case", "fold lower case to upper case"),
    OPTION("-g", "--general-numeric-sort",
           "compare according to string numerical value"),
    OPTION("-i", "--ignore-nonprinting", "consider only printable characters"),
    OPTION("-h", "--human-numeric-sort",
           "compare human readable numbers (e.g., 1K, 2M)"),
    OPTION("-M", "--month-sort", "compare as month names"),
    OPTION("-m", "--merge", "merge already sorted files"),
    OPTION("-n", "--numeric-sort",
           "compare according to string numerical value"),
    OPTION("-V", "--version-sort", "compare version numbers naturally"),
    OPTION("-R", "--random-sort", "sort by random hash of keys"),
    OPTION("", "--random-source", "get random bytes from FILE", STRING_TYPE),
    OPTION("-r", "--reverse", "reverse the result of comparisons"),
    OPTION("-S", "--buffer-size",
           "use SIZE for the main memory buffer; accepted as a memory hint",
           STRING_TYPE),
    OPTION("-s", "--stable",
           "stabilize sort by disabling last-resort comparison"),
    OPTION("-u", "--unique", "output only the first of equal runs"),
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline"),
    OPTION("-c", "--check", "check whether input is sorted"),
    OPTION("-C", "", "check whether input is sorted quietly"),
    OPTION("", "--debug", "print sort key diagnostics to standard error"),
    OPTION("-o", "--output", "write result to FILE instead of standard output",
           STRING_TYPE),
    OPTION(
        "", "--files0-from",
        "read input from the files specified by NUL-terminated names in FILE",
        STRING_TYPE),
    OPTION("", "--sort", "set sort order; 'version' enables version sort",
           STRING_TYPE),
    OPTION("-t", "--field-separator",
           "use SEP instead of non-blank to blank transition", STRING_TYPE),
    OPTION("-k", "--key", "sort via a key; KEYDEF has form F[.C][,F[.C]]",
           STRING_TYPE)};

namespace sort_pipeline {
namespace cp = core::pipeline;

enum class OperationMode { Sort, Check, CheckQuiet };

enum class SortMode {
  GeneralNumeric,
  HumanNumeric,
  Month,
  Numeric,
  Random,
  Version
};

struct KeySpec {
  size_t start_field = 1;
  std::optional<size_t> start_char;
  std::optional<size_t> end_field;
  std::optional<size_t> end_char;
  bool ignore_leading_blanks = false;
  bool dictionary_order = false;
  bool ignore_case = false;
  bool ignore_nonprinting = false;
  bool numeric_sort = false;
  bool version_sort = false;
  bool human_numeric = false;
  bool month_sort = false;
  bool general_numeric = false;
  bool random_sort = false;
  bool reverse = false;
};

struct Config {
  bool ignore_leading_blanks = false;
  bool dictionary_order = false;
  bool ignore_case = false;
  bool ignore_nonprinting = false;
  bool numeric_sort = false;
  bool version_sort = false;
  bool human_numeric = false;
  bool month_sort = false;
  bool general_numeric = false;
  bool random_sort = false;
  bool reverse = false;
  bool merge = false;
  bool stable = false;
  bool unique = false;
  bool debug = false;
  OperationMode mode = OperationMode::Sort;
  char delimiter = '\n';
  std::optional<char> field_separator;
  std::string output_file;
  std::string files0_from;
  std::string random_source;
  uint64_t random_seed = 0;
  std::vector<KeySpec> keys;
  SmallVector<std::string, 64>
      files{};  // SmallVector for paths, stack-allocated
};

auto read_all(std::istream& in) -> std::string { return read_text_stream(in); }

auto read_source(std::string_view path) -> cp::Result<std::string> {
  if (path == "-") return read_all(std::cin);

  std::ifstream in(std::string(path), std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + std::string(path) + "'");
  }
  return read_all(in);
}

auto read_binary_source(std::string_view path) -> cp::Result<std::string> {
  if (path == "-") {
    return std::string{std::istreambuf_iterator<char>{std::cin},
                       std::istreambuf_iterator<char>{}};
  }

  std::ifstream in(std::string(path), std::ios::binary);
  if (!in.is_open()) {
    return std::unexpected("cannot open '" + std::string(path) + "'");
  }
  return std::string{std::istreambuf_iterator<char>{in},
                     std::istreambuf_iterator<char>{}};
}

auto read_files0_from(const std::string& path)
    -> cp::Result<std::vector<std::string>> {
  std::istream* input = nullptr;
  std::ifstream file;
  if (path == "-") {
    input = &std::cin;
  } else {
    file.open(path, std::ios::binary);
    if (!file.is_open()) {
      return std::unexpected("cannot open file list '" + path + "'");
    }
    input = &file;
  }

  std::vector<std::string> paths;
  std::string name;
  while (std::getline(*input, name, '\0')) {
    if (!name.empty()) paths.push_back(name);
  }
  return paths;
}

auto split_records(std::string_view content, char delimiter)
    -> std::vector<std::string> {
  std::vector<std::string> out;
  out.reserve(content.size() / 20);  // Estimate: assume ~20 chars per record
  size_t start = 0;
  for (size_t i = 0; i < content.size(); ++i) {
    if (content[i] == delimiter) {
      out.emplace_back(content.substr(start, i - start));
      start = i + 1;
    }
  }
  if (start < content.size()) {
    out.emplace_back(content.substr(start));
  }
  return out;
}

auto to_lower_ascii(std::string_view s) -> std::string {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    out.push_back(static_cast<char>(std::tolower(c)));
  }
  return out;
}

auto option_matches(const OptionMeta& meta, std::string_view short_name,
                    std::string_view long_name) -> bool {
  return (!short_name.empty() && meta.short_name == short_name) ||
         (!long_name.empty() && meta.long_name == long_name);
}

auto normalize_text_key(std::string_view s, bool dictionary_order,
                        bool ignore_nonprinting, bool ignore_case)
    -> std::string {
  std::string out;
  out.reserve(s.size());
  for (unsigned char c : s) {
    if (dictionary_order && !(std::isalnum(c) || c == ' ' || c == '\t')) {
      continue;
    }
    if (ignore_nonprinting && std::isprint(c) == 0) {
      continue;
    }
    out.push_back(static_cast<char>(c));
  }
  if (ignore_case) {
    out = to_lower_ascii(out);
  }
  return out;
}

auto normalize_text_key(std::string_view s, const Config& cfg) -> std::string {
  return normalize_text_key(s, cfg.dictionary_order, cfg.ignore_nonprinting,
                            cfg.ignore_case);
}

auto mix64(uint64_t value) -> uint64_t {
  value ^= value >> 30;
  value *= 0xbf58476d1ce4e5b9ULL;
  value ^= value >> 27;
  value *= 0x94d049bb133111ebULL;
  value ^= value >> 31;
  return value;
}

auto hash_bytes(std::string_view bytes, uint64_t seed) -> uint64_t {
  uint64_t hash = 1469598103934665603ULL ^ seed;
  for (unsigned char c : bytes) {
    hash ^= c;
    hash *= 1099511628211ULL;
  }
  hash ^= static_cast<uint64_t>(bytes.size()) * 0x9e3779b97f4a7c15ULL;
  return mix64(hash);
}

auto make_random_seed_from_source(std::string_view bytes) -> uint64_t {
  return hash_bytes(bytes, 0x243f6a8885a308d3ULL);
}

auto make_default_random_seed() -> uint64_t {
  uint64_t seed = static_cast<uint64_t>(
      std::chrono::steady_clock::now().time_since_epoch().count());
  try {
    std::random_device device;
    seed ^= static_cast<uint64_t>(device()) << 32;
    seed ^= static_cast<uint64_t>(device());
  } catch (...) {
  }
  return mix64(seed);
}

auto ltrim_ascii(std::string_view s) -> std::string_view {
  size_t i = 0;
  while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i])) != 0) {
    ++i;
  }
  return s.substr(i);
}

auto is_key_modifier(char c) -> bool {
  switch (c) {
    case 'M':
    case 'b':
    case 'd':
    case 'f':
    case 'g':
    case 'h':
    case 'i':
    case 'n':
    case 'R':
    case 'r':
    case 'V':
      return true;
    default:
      return false;
  }
}

void apply_key_modifier(KeySpec& key, char c) {
  switch (c) {
    case 'M':
      key.month_sort = true;
      break;
    case 'b':
      key.ignore_leading_blanks = true;
      break;
    case 'd':
      key.dictionary_order = true;
      break;
    case 'f':
      key.ignore_case = true;
      break;
    case 'g':
      key.general_numeric = true;
      break;
    case 'h':
      key.human_numeric = true;
      break;
    case 'i':
      key.ignore_nonprinting = true;
      break;
    case 'n':
      key.numeric_sort = true;
      break;
    case 'R':
      key.random_sort = true;
      break;
    case 'r':
      key.reverse = true;
      break;
    case 'V':
      key.version_sort = true;
      break;
    default:
      break;
  }
}

struct ParsedKeyPosition {
  size_t field = 0;
  std::optional<size_t> character;
  std::string_view modifiers;
};

auto parse_key_position(std::string_view text)
    -> cp::Result<ParsedKeyPosition> {
  if (text.empty()) return std::unexpected("invalid key spec");

  ParsedKeyPosition pos;
  size_t i = 0;
  while (i < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
    ++i;
  }
  if (i == 0) {
    return std::unexpected("invalid key spec");
  }

  auto field_text = text.substr(0, i);
  auto [ptr, ec] = std::from_chars(
      field_text.data(), field_text.data() + field_text.size(), pos.field);
  if (ec != std::errc() || ptr != field_text.data() + field_text.size() ||
      pos.field == 0) {
    return std::unexpected("invalid key spec");
  }

  if (i < text.size() && text[i] == '.') {
    ++i;
    size_t char_start = i;
    while (i < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
      ++i;
    }
    if (i == char_start) {
      return std::unexpected("invalid key spec");
    }
    size_t value = 0;
    auto char_text = text.substr(char_start, i - char_start);
    auto [char_ptr, char_ec] = std::from_chars(
        char_text.data(), char_text.data() + char_text.size(), value);
    if (char_ec != std::errc() ||
        char_ptr != char_text.data() + char_text.size()) {
      return std::unexpected("invalid key spec");
    }
    pos.character = value;
  }

  for (size_t j = i; j < text.size(); ++j) {
    if (!is_key_modifier(text[j])) {
      return std::unexpected("invalid key spec");
    }
  }
  pos.modifiers = text.substr(i);
  return pos;
}

auto parse_key_spec(std::string_view text) -> cp::Result<KeySpec> {
  if (text.empty()) return std::unexpected("invalid key spec");

  KeySpec key;

  auto comma = text.find(',');
  auto first = parse_key_position(text.substr(0, comma));
  if (!first) return std::unexpected(first.error());
  if (first->character.has_value() && *first->character == 0) {
    return std::unexpected("invalid key spec");
  }

  key.start_field = first->field;
  key.start_char = first->character;
  for (char c : first->modifiers) {
    apply_key_modifier(key, c);
  }

  if (comma != std::string_view::npos) {
    auto second = parse_key_position(text.substr(comma + 1));
    if (!second) return std::unexpected(second.error());
    key.end_field = second->field;
    key.end_char = second->character;
    for (char c : second->modifiers) {
      apply_key_modifier(key, c);
    }
  }

  return key;
}

struct FieldSpan {
  size_t start = 0;
  size_t end = 0;
  bool found = false;
};

auto get_field_span_by_whitespace(std::string_view line, size_t index)
    -> FieldSpan {
  size_t field = 0;
  size_t i = 0;
  while (i < line.size()) {
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i])) != 0) {
      ++i;
    }
    if (i >= line.size()) break;

    size_t start = i;
    while (i < line.size() &&
           std::isspace(static_cast<unsigned char>(line[i])) == 0) {
      ++i;
    }
    ++field;
    if (field == index) return FieldSpan{start, i, true};
  }
  return FieldSpan{line.size(), line.size(), false};
}

auto get_field_span_by_separator(std::string_view line, size_t index, char sep)
    -> FieldSpan {
  size_t field = 1;
  size_t start = 0;
  while (true) {
    size_t pos = line.find(sep, start);
    if (field == index) {
      if (pos == std::string_view::npos) {
        return FieldSpan{start, line.size(), true};
      }
      return FieldSpan{start, pos, true};
    }
    if (pos == std::string_view::npos) break;
    start = pos + 1;
    ++field;
  }
  return FieldSpan{line.size(), line.size(), false};
}

auto get_field_span(std::string_view line, size_t index, const Config& cfg)
    -> FieldSpan {
  if (cfg.field_separator.has_value()) {
    return get_field_span_by_separator(line, index, *cfg.field_separator);
  }
  return get_field_span_by_whitespace(line, index);
}

auto resolve_key_start(std::string_view line, const FieldSpan& span,
                       const KeySpec& key, bool ignore_leading_blanks)
    -> size_t {
  if (!span.found) return line.size();
  if (!key.start_char.has_value()) return span.start;
  size_t base = span.start;
  if (ignore_leading_blanks) {
    while (base < span.end &&
           std::isspace(static_cast<unsigned char>(line[base])) != 0) {
      ++base;
    }
  }
  return std::min(base + *key.start_char - 1, span.end);
}

auto resolve_key_end(std::string_view line, const FieldSpan& span,
                     const KeySpec& key, bool ignore_leading_blanks) -> size_t {
  if (!span.found) return line.size();
  if (!key.end_char.has_value()) return span.end;
  if (*key.end_char == 0) return span.end;
  size_t base = span.start;
  if (ignore_leading_blanks) {
    while (base < span.end &&
           std::isspace(static_cast<unsigned char>(line[base])) != 0) {
      ++base;
    }
  }
  return std::min(base + *key.end_char, span.end);
}

auto extract_key(std::string_view line, const Config& cfg,
                 const KeySpec* key_spec) -> std::string_view {
  if (key_spec == nullptr) return line;

  const auto start_span = get_field_span(line, key_spec->start_field, cfg);
  const bool ignore_blanks =
      cfg.ignore_leading_blanks || key_spec->ignore_leading_blanks;
  size_t start = resolve_key_start(line, start_span, *key_spec, ignore_blanks);
  size_t end = line.size();
  if (key_spec->end_field.has_value()) {
    const auto end_span = get_field_span(line, *key_spec->end_field, cfg);
    end = resolve_key_end(line, end_span, *key_spec, ignore_blanks);
  }

  if (end < start) end = start;
  std::string_view key = line.substr(start, end - start);

  if (cfg.ignore_leading_blanks || key_spec->ignore_leading_blanks) {
    key = ltrim_ascii(key);
  }
  return key;
}

auto effective_dictionary_order(const Config& cfg, const KeySpec* key_spec)
    -> bool {
  return cfg.dictionary_order ||
         (key_spec != nullptr && key_spec->dictionary_order);
}

auto effective_ignore_case(const Config& cfg, const KeySpec* key_spec) -> bool {
  return cfg.ignore_case || (key_spec != nullptr && key_spec->ignore_case);
}

auto effective_ignore_nonprinting(const Config& cfg, const KeySpec* key_spec)
    -> bool {
  return cfg.ignore_nonprinting ||
         (key_spec != nullptr && key_spec->ignore_nonprinting);
}

auto make_random_sort_key(std::string_view line, const Config& cfg,
                          const KeySpec* key_spec) -> std::string {
  return normalize_text_key(extract_key(line, cfg, key_spec),
                            effective_dictionary_order(cfg, key_spec),
                            effective_ignore_nonprinting(cfg, key_spec),
                            effective_ignore_case(cfg, key_spec));
}

struct NumericPrefix {
  long double value = 0.0L;
  size_t end = 0;
};

auto parse_numeric_prefix(std::string_view s) -> NumericPrefix {
  auto trimmed = ltrim_ascii(s);
  if (trimmed.empty()) return {};

  size_t i = 0;
  if (trimmed[i] == '-') {
    ++i;
  }

  bool saw_digit = false;
  while (i < trimmed.size() &&
         std::isdigit(static_cast<unsigned char>(trimmed[i])) != 0) {
    saw_digit = true;
    ++i;
  }

  if (i < trimmed.size() && trimmed[i] == '.') {
    ++i;
    while (i < trimmed.size() &&
           std::isdigit(static_cast<unsigned char>(trimmed[i])) != 0) {
      saw_digit = true;
      ++i;
    }
  }

  if (!saw_digit) return {};

  std::string local(trimmed.substr(0, i));
  char* end = nullptr;
  errno = 0;
  const long double value = std::strtold(local.c_str(), &end);
  if (end == local.c_str() || errno == ERANGE) {
    return {};
  }
  return NumericPrefix{value, i};
}

enum class GeneralNumberClass {
  NonNumber,
  Nan,
  NegativeInfinity,
  Finite,
  PositiveInfinity
};

struct GeneralNumber {
  GeneralNumberClass cls = GeneralNumberClass::NonNumber;
  long double value = 0.0L;
};

auto parse_general_number(std::string_view s) -> GeneralNumber {
  auto trimmed = ltrim_ascii(s);
  if (trimmed.empty()) return {};

  std::string local(trimmed);
  char* end = nullptr;
  errno = 0;
  const long double value = std::strtold(local.c_str(), &end);
  if (end == local.c_str()) {
    return {};
  }
  if (std::isnan(value)) {
    return GeneralNumber{GeneralNumberClass::Nan, value};
  }
  if (std::isinf(value)) {
    return GeneralNumber{value < 0 ? GeneralNumberClass::NegativeInfinity
                                   : GeneralNumberClass::PositiveInfinity,
                         value};
  }
  return GeneralNumber{GeneralNumberClass::Finite, value};
}

auto compare_general_numbers(const GeneralNumber& a, const GeneralNumber& b)
    -> int {
  if (a.cls != b.cls) {
    return static_cast<int>(a.cls) < static_cast<int>(b.cls) ? -1 : 1;
  }
  if (a.cls != GeneralNumberClass::Finite) return 0;
  if (a.value < b.value) return -1;
  if (a.value > b.value) return 1;
  return 0;
}

struct HumanNumber {
  int sign = 0;
  int suffix_rank = 0;
  long double value = 0.0L;
};

auto human_suffix_rank(char c) -> int {
  if (c == 'k' || c == 'K') return 1;
  constexpr std::string_view suffixes = "MGTPEZYRQ";
  auto pos = suffixes.find(c);
  if (pos == std::string_view::npos) return 0;
  return static_cast<int>(pos) + 2;
}

auto parse_human_number(std::string_view s) -> HumanNumber {
  auto trimmed = ltrim_ascii(s);
  auto numeric = parse_numeric_prefix(trimmed);

  int sign = 0;
  if (numeric.value < 0.0L) {
    sign = -1;
  } else if (numeric.value > 0.0L) {
    sign = 1;
  }

  int suffix_rank = 0;
  if (numeric.end < trimmed.size()) {
    suffix_rank = human_suffix_rank(trimmed[numeric.end]);
  }

  return HumanNumber{sign, suffix_rank, numeric.value};
}

auto compare_human_numbers(const HumanNumber& a, const HumanNumber& b) -> int {
  if (a.sign != b.sign) return a.sign < b.sign ? -1 : 1;
  if (a.suffix_rank != b.suffix_rank) {
    return a.suffix_rank < b.suffix_rank ? -1 : 1;
  }
  if (a.value < b.value) return -1;
  if (a.value > b.value) return 1;
  return 0;
}

auto parse_month_rank(std::string_view s) -> std::optional<int> {
  static constexpr std::array<std::string_view, 12> months{
      "jan", "feb", "mar", "apr", "may", "jun",
      "jul", "aug", "sep", "oct", "nov", "dec"};

  auto trimmed = ltrim_ascii(s);
  if (trimmed.size() < 3) return std::nullopt;

  std::array<char, 3> prefix{};
  for (size_t i = 0; i < prefix.size(); ++i) {
    prefix[i] =
        static_cast<char>(std::tolower(static_cast<unsigned char>(trimmed[i])));
  }

  for (size_t i = 0; i < months.size(); ++i) {
    if (std::string_view(prefix.data(), prefix.size()) == months[i]) {
      return static_cast<int>(i);
    }
  }
  return std::nullopt;
}

auto compare_version_strings(std::string_view a, std::string_view b) -> int {
  size_t i = 0;
  size_t j = 0;

  auto is_digit = [](unsigned char c) { return std::isdigit(c) != 0; };

  while (i < a.size() || j < b.size()) {
    bool a_digit = i < a.size() && is_digit(static_cast<unsigned char>(a[i]));
    bool b_digit = j < b.size() && is_digit(static_cast<unsigned char>(b[j]));

    if (a_digit && b_digit) {
      size_t a_start = i;
      size_t b_start = j;
      while (i < a.size() && is_digit(static_cast<unsigned char>(a[i]))) ++i;
      while (j < b.size() && is_digit(static_cast<unsigned char>(b[j]))) ++j;

      while (a_start < i && a[a_start] == '0') ++a_start;
      while (b_start < j && b[b_start] == '0') ++b_start;

      size_t a_len = i - a_start;
      size_t b_len = j - b_start;
      if (a_len < b_len) return -1;
      if (a_len > b_len) return 1;

      for (size_t k = 0; k < a_len; ++k) {
        unsigned char ac = static_cast<unsigned char>(a[a_start + k]);
        unsigned char bc = static_cast<unsigned char>(b[b_start + k]);
        if (ac < bc) return -1;
        if (ac > bc) return 1;
      }
      continue;
    }

    if (a_digit != b_digit) {
      return a_digit ? -1 : 1;
    }

    while (i < a.size() && !is_digit(static_cast<unsigned char>(a[i])) &&
           j < b.size() && !is_digit(static_cast<unsigned char>(b[j]))) {
      unsigned char ac = static_cast<unsigned char>(a[i]);
      unsigned char bc = static_cast<unsigned char>(b[j]);
      if (ac == bc) {
        ++i;
        ++j;
        continue;
      }

      if (std::tolower(ac) < std::tolower(bc)) return -1;
      if (std::tolower(ac) > std::tolower(bc)) return 1;
      if (ac < bc) return -1;
      if (ac > bc) return 1;
    }

    if (i < a.size() && j < b.size()) {
      continue;
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
  }

  return 0;
}

auto compare_single_key(std::string_view a, std::string_view b,
                        const Config& cfg, const KeySpec* key_spec) -> int {
  auto key_a = extract_key(a, cfg, key_spec);
  auto key_b = extract_key(b, cfg, key_spec);

  const bool random_sort =
      cfg.random_sort || (key_spec != nullptr && key_spec->random_sort);
  const bool version_sort =
      cfg.version_sort || (key_spec != nullptr && key_spec->version_sort);
  const bool numeric_sort =
      cfg.numeric_sort || (key_spec != nullptr && key_spec->numeric_sort);
  const bool human_numeric =
      cfg.human_numeric || (key_spec != nullptr && key_spec->human_numeric);
  const bool month_sort =
      cfg.month_sort || (key_spec != nullptr && key_spec->month_sort);
  const bool general_numeric =
      cfg.general_numeric || (key_spec != nullptr && key_spec->general_numeric);

  if (random_sort) {
    std::string random_key_a = make_random_sort_key(a, cfg, key_spec);
    std::string random_key_b = make_random_sort_key(b, cfg, key_spec);
    if (random_key_a == random_key_b) return 0;

    const uint64_t hash_a = hash_bytes(random_key_a, cfg.random_seed);
    const uint64_t hash_b = hash_bytes(random_key_b, cfg.random_seed);
    if (hash_a < hash_b) return -1;
    if (hash_a > hash_b) return 1;
    if (random_key_a < random_key_b) return -1;
    if (random_key_a > random_key_b) return 1;
    return 0;
  }

  if (version_sort) {
    int cmp = compare_version_strings(key_a, key_b);
    if (cmp != 0) return cmp;
    return 0;
  } else if (numeric_sort) {
    auto n_a = parse_numeric_prefix(key_a);
    auto n_b = parse_numeric_prefix(key_b);
    if (n_a.value < n_b.value) return -1;
    if (n_a.value > n_b.value) return 1;
    return 0;
  } else if (human_numeric) {
    return compare_human_numbers(parse_human_number(key_a),
                                 parse_human_number(key_b));
  } else if (month_sort) {
    auto a_month = parse_month_rank(key_a);
    auto b_month = parse_month_rank(key_b);
    if (a_month.has_value() && b_month.has_value()) {
      if (*a_month < *b_month) return -1;
      if (*a_month > *b_month) return 1;
      return 0;
    } else if (a_month.has_value() != b_month.has_value()) {
      return a_month.has_value() ? 1 : -1;
    }
  } else if (general_numeric) {
    return compare_general_numbers(parse_general_number(key_a),
                                   parse_general_number(key_b));
  }

  std::string left =
      normalize_text_key(key_a, effective_dictionary_order(cfg, key_spec),
                         effective_ignore_nonprinting(cfg, key_spec),
                         effective_ignore_case(cfg, key_spec));
  std::string right =
      normalize_text_key(key_b, effective_dictionary_order(cfg, key_spec),
                         effective_ignore_nonprinting(cfg, key_spec),
                         effective_ignore_case(cfg, key_spec));

  if (left < right) return -1;
  if (left > right) return 1;

  return 0;
}

auto compare_records_by_sort_key(std::string_view a, std::string_view b,
                                 const Config& cfg) -> int {
  if (cfg.keys.empty()) {
    return compare_single_key(a, b, cfg, nullptr);
  }

  for (const auto& key : cfg.keys) {
    int cmp = compare_single_key(a, b, cfg, &key);
    if (key.reverse) cmp = -cmp;
    if (cmp != 0) return cmp;
  }
  return 0;
}

auto compare_records(std::string_view a, std::string_view b, const Config& cfg)
    -> int {
  int cmp = compare_records_by_sort_key(a, b, cfg);
  if (cmp != 0) return cmp;

  if (cfg.stable || cfg.unique) return 0;

  if (a < b) return -1;
  if (a > b) return 1;
  return 0;
}

auto is_unsupported_used(const CommandContext<SORT_OPTIONS.size()>& ctx)
    -> std::optional<std::string_view> {
  (void)ctx;
  return std::nullopt;
}

void clear_global_sort_modes(Config& cfg) {
  cfg.general_numeric = false;
  cfg.human_numeric = false;
  cfg.month_sort = false;
  cfg.numeric_sort = false;
  cfg.random_sort = false;
  cfg.version_sort = false;
}

void apply_global_sort_mode(Config& cfg, SortMode mode) {
  clear_global_sort_modes(cfg);
  switch (mode) {
    case SortMode::GeneralNumeric:
      cfg.general_numeric = true;
      break;
    case SortMode::HumanNumeric:
      cfg.human_numeric = true;
      break;
    case SortMode::Month:
      cfg.month_sort = true;
      break;
    case SortMode::Numeric:
      cfg.numeric_sort = true;
      break;
    case SortMode::Random:
      cfg.random_sort = true;
      break;
    case SortMode::Version:
      cfg.version_sort = true;
      break;
  }
}

auto parse_sort_mode_word(std::string_view text) -> std::optional<SortMode> {
  auto lowered = to_lower_ascii(text);
  if (lowered == "general-numeric" || lowered == "g") {
    return SortMode::GeneralNumeric;
  }
  if (lowered == "human-numeric" || lowered == "h") {
    return SortMode::HumanNumeric;
  }
  if (lowered == "month" || lowered == "m") {
    return SortMode::Month;
  }
  if (lowered == "numeric" || lowered == "n") {
    return SortMode::Numeric;
  }
  if (lowered == "random" || lowered == "r") {
    return SortMode::Random;
  }
  if (lowered == "version" || lowered == "v") {
    return SortMode::Version;
  }
  return std::nullopt;
}

auto effective_sort_mode_name(const Config& cfg, const KeySpec* key_spec)
    -> std::string_view {
  if (cfg.random_sort || (key_spec != nullptr && key_spec->random_sort)) {
    return "random";
  }
  if (cfg.version_sort || (key_spec != nullptr && key_spec->version_sort)) {
    return "version";
  }
  if (cfg.numeric_sort || (key_spec != nullptr && key_spec->numeric_sort)) {
    return "numeric";
  }
  if (cfg.human_numeric || (key_spec != nullptr && key_spec->human_numeric)) {
    return "human-numeric";
  }
  if (cfg.month_sort || (key_spec != nullptr && key_spec->month_sort)) {
    return "month";
  }
  if (cfg.general_numeric ||
      (key_spec != nullptr && key_spec->general_numeric)) {
    return "general-numeric";
  }
  return "lexicographic";
}

auto describe_key_range(const KeySpec& key) -> std::string {
  std::ostringstream out;
  out << "field " << key.start_field;
  if (key.start_char.has_value()) out << "." << *key.start_char;

  if (key.end_field.has_value()) {
    out << " to field " << *key.end_field;
    if (key.end_char.has_value()) out << "." << *key.end_char;
  } else {
    out << " to end of line";
  }

  return out.str();
}

void append_active_key_flags(std::ostream& out, const Config& cfg,
                             const KeySpec* key_spec) {
  if (cfg.ignore_leading_blanks ||
      (key_spec != nullptr && key_spec->ignore_leading_blanks)) {
    out << ", ignore-leading-blanks";
  }
  if (effective_dictionary_order(cfg, key_spec)) out << ", dictionary-order";
  if (effective_ignore_case(cfg, key_spec)) out << ", ignore-case";
  if (effective_ignore_nonprinting(cfg, key_spec)) {
    out << ", ignore-nonprinting";
  }
  if (key_spec != nullptr && key_spec->reverse) out << ", reverse-key";
}

void emit_debug_diagnostics(const Config& cfg) {
  if (!cfg.debug) return;

  std::cerr << "sort: debug: output ordering is unchanged; diagnostics only\n";
  if (cfg.keys.empty()) {
    std::cerr << "sort: debug: key 1: whole line, mode "
              << effective_sort_mode_name(cfg, nullptr);
    append_active_key_flags(std::cerr, cfg, nullptr);
    std::cerr << "\n";
  } else {
    for (size_t i = 0; i < cfg.keys.size(); ++i) {
      const auto& key = cfg.keys[i];
      std::cerr << "sort: debug: key " << (i + 1) << ": "
                << describe_key_range(key) << ", mode "
                << effective_sort_mode_name(cfg, &key);
      append_active_key_flags(std::cerr, cfg, &key);
      std::cerr << "\n";
    }
  }

  if (cfg.field_separator.has_value()) {
    std::cerr << "sort: debug: field separator is ";
    if (*cfg.field_separator == '\0') {
      std::cerr << "NUL";
    } else {
      std::cerr << "'" << *cfg.field_separator << "'";
    }
    std::cerr << "\n";
  }
  if (cfg.stable || cfg.unique) {
    std::cerr << "sort: debug: last-resort whole-line comparison disabled";
    if (cfg.stable) std::cerr << " by --stable";
    if (cfg.unique) std::cerr << " by --unique";
    std::cerr << "\n";
  } else {
    std::cerr << "sort: debug: last-resort whole-line comparison enabled\n";
  }
  if (cfg.reverse) std::cerr << "sort: debug: final result is reversed\n";
  if (cfg.merge) {
    std::cerr << "sort: debug: merge mode assumes input files are already "
                 "sorted\n";
  }
}

auto apply_sort_ordering_options(Config& cfg,
                                 const CommandContext<SORT_OPTIONS.size()>& ctx)
    -> cp::Result<void> {
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= SORT_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];

    if (option_matches(meta, "-g", "--general-numeric-sort")) {
      apply_global_sort_mode(cfg, SortMode::GeneralNumeric);
    } else if (option_matches(meta, "-h", "--human-numeric-sort")) {
      apply_global_sort_mode(cfg, SortMode::HumanNumeric);
    } else if (option_matches(meta, "-M", "--month-sort")) {
      apply_global_sort_mode(cfg, SortMode::Month);
    } else if (option_matches(meta, "-n", "--numeric-sort")) {
      apply_global_sort_mode(cfg, SortMode::Numeric);
    } else if (option_matches(meta, "-R", "--random-sort")) {
      apply_global_sort_mode(cfg, SortMode::Random);
    } else if (option_matches(meta, "-V", "--version-sort")) {
      apply_global_sort_mode(cfg, SortMode::Version);
    } else if (option_matches(meta, "", "--sort")) {
      const auto* value = std::get_if<std::string>(&occurrence.value);
      if (value == nullptr) return std::unexpected("invalid sort mode");
      auto mode = parse_sort_mode_word(*value);
      if (!mode) return std::unexpected("invalid sort mode");
      apply_global_sort_mode(cfg, *mode);
    }
  }
  return {};
}

struct SizeSuffix {
  std::string_view suffix;
  std::uintmax_t multiplier;
};

auto parse_buffer_size_hint(std::string_view text) -> bool {
  if (text.empty()) return false;

  size_t number_end = 0;
  while (number_end < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[number_end])) != 0) {
    ++number_end;
  }
  if (number_end == 0) return false;

  std::uintmax_t value = 0;
  auto number = text.substr(0, number_end);
  auto [ptr, ec] =
      std::from_chars(number.data(), number.data() + number.size(), value);
  if (ec != std::errc() || ptr != number.data() + number.size()) {
    return false;
  }

  auto suffix = text.substr(number_end);
  if (suffix == "%") {
    return value <= 100;
  }

  static constexpr std::array suffixes{
      SizeSuffix{"", 1},
      SizeSuffix{"b", 512},
      SizeSuffix{"K", 1024},
      SizeSuffix{"KB", 1000},
      SizeSuffix{"kB", 1000},
      SizeSuffix{"KiB", 1024},
      SizeSuffix{"M", 1024ULL * 1024ULL},
      SizeSuffix{"MB", 1000ULL * 1000ULL},
      SizeSuffix{"MiB", 1024ULL * 1024ULL},
      SizeSuffix{"G", 1024ULL * 1024ULL * 1024ULL},
      SizeSuffix{"GB", 1000ULL * 1000ULL * 1000ULL},
      SizeSuffix{"GiB", 1024ULL * 1024ULL * 1024ULL},
      SizeSuffix{"T", 1024ULL * 1024ULL * 1024ULL * 1024ULL},
      SizeSuffix{"TB", 1000ULL * 1000ULL * 1000ULL * 1000ULL},
      SizeSuffix{"TiB", 1024ULL * 1024ULL * 1024ULL * 1024ULL},
      SizeSuffix{"P", 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
      SizeSuffix{"PB", 1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL},
      SizeSuffix{"PiB", 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
      SizeSuffix{"E",
                 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL},
      SizeSuffix{"EB",
                 1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL * 1000ULL},
      SizeSuffix{"EiB",
                 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL * 1024ULL}};

  for (const auto& entry : suffixes) {
    if (entry.suffix != suffix) continue;
    return value <=
           std::numeric_limits<std::uintmax_t>::max() / entry.multiplier;
  }

  return false;
}

auto build_config(const CommandContext<SORT_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.ignore_leading_blanks = ctx.get<bool>("--ignore-leading-blanks", false) ||
                              ctx.get<bool>("-b", false);
  cfg.dictionary_order =
      ctx.get<bool>("--dictionary-order", false) || ctx.get<bool>("-d", false);
  cfg.ignore_case =
      ctx.get<bool>("--ignore-case", false) || ctx.get<bool>("-f", false);
  cfg.ignore_nonprinting = ctx.get<bool>("--ignore-nonprinting", false) ||
                           ctx.get<bool>("-i", false);
  auto sort_options = apply_sort_ordering_options(cfg, ctx);
  if (!sort_options) return std::unexpected(sort_options.error());
  cfg.reverse = ctx.get<bool>("--reverse", false) || ctx.get<bool>("-r", false);
  cfg.merge = ctx.get<bool>("--merge", false) || ctx.get<bool>("-m", false);
  cfg.stable = ctx.get<bool>("--stable", false) || ctx.get<bool>("-s", false);
  cfg.unique = ctx.get<bool>("--unique", false) || ctx.get<bool>("-u", false);
  cfg.debug = ctx.get<bool>("--debug", false);
  if (ctx.get<bool>("--check", false) || ctx.get<bool>("-c", false)) {
    cfg.mode = OperationMode::Check;
  }
  if (ctx.get<bool>("-C", false)) {
    cfg.mode = OperationMode::CheckQuiet;
  }
  cfg.delimiter =
      (ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false))
          ? '\0'
          : '\n';

  cfg.output_file = ctx.get<std::string>("--output", "");
  if (cfg.output_file.empty()) cfg.output_file = ctx.get<std::string>("-o", "");
  cfg.files0_from = ctx.get<std::string>("--files0-from", "");
  cfg.random_source = ctx.get<std::string>("--random-source", "");

  std::string buffer_size = ctx.get<std::string>("--buffer-size", "");
  if (buffer_size.empty()) buffer_size = ctx.get<std::string>("-S", "");
  if ((ctx.has("--buffer-size") || ctx.has("-S")) &&
      !parse_buffer_size_hint(buffer_size)) {
    return std::unexpected("invalid buffer size");
  }

  std::string sep = ctx.get<std::string>("--field-separator", "");
  if (sep.empty()) sep = ctx.get<std::string>("-t", "");
  if (!sep.empty()) {
    if (sep == "\\0") {
      cfg.field_separator = '\0';
    } else if (sep.size() == 1) {
      cfg.field_separator = sep[0];
    } else {
      return std::unexpected("field separator must be a single character");
    }
  }

  for (const auto& key_text : ctx.get_all<std::string>("--key")) {
    auto key = parse_key_spec(key_text);
    if (!key) return std::unexpected(key.error());
    cfg.keys.push_back(*key);
  }

  const bool any_random_key =
      std::any_of(cfg.keys.begin(), cfg.keys.end(),
                  [](const KeySpec& key) { return key.random_sort; });
  if (cfg.random_sort || any_random_key) {
    if (!cfg.random_source.empty()) {
      auto source = read_binary_source(cfg.random_source);
      if (!source) return std::unexpected(source.error());
      cfg.random_seed = make_random_seed_from_source(*source);
    } else {
      cfg.random_seed = make_default_random_seed();
    }
  }

  if (!cfg.files0_from.empty()) {
    if (!ctx.positionals.empty()) {
      return std::unexpected(
          "--files0-from cannot be combined with file operands");
    }
    auto listed = read_files0_from(cfg.files0_from);
    if (!listed) return std::unexpected(listed.error());
    for (const auto& file : *listed) {
      cfg.files.push_back(file);
    }
  } else {
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
    if (cfg.files.empty()) cfg.files.push_back("-");
  }

  return cfg;
}

auto load_records(const Config& cfg) -> cp::Result<std::vector<std::string>> {
  std::vector<std::string> records;
  for (size_t i = 0; i < cfg.files.size(); ++i) {
    auto content = read_source(cfg.files[i]);
    if (!content) {
      return std::unexpected(content.error());
    }
    auto chunk = split_records(*content, cfg.delimiter);
    for (auto& r : chunk) {
      records.push_back(std::move(r));
    }
  }

  return records;
}

auto is_before(const std::string& a, const std::string& b, const Config& cfg)
    -> bool {
  int cmp = compare_records(a, b, cfg);
  if (cfg.reverse) return cmp > 0;
  return cmp < 0;
}

auto check_sorted(const std::vector<std::string>& records, const Config& cfg)
    -> std::optional<size_t> {
  if (records.size() < 2) return std::nullopt;
  for (size_t i = 1; i < records.size(); ++i) {
    if (cfg.unique &&
        compare_records_by_sort_key(records[i - 1], records[i], cfg) == 0) {
      return i;
    }
    if (!is_before(records[i - 1], records[i], cfg) &&
        compare_records(records[i - 1], records[i], cfg) != 0) {
      return i;
    }
  }
  return std::nullopt;
}

auto run(const Config& cfg) -> int {
  emit_debug_diagnostics(cfg);

  auto loaded = load_records(cfg);
  if (!loaded) {
    cp::report_custom_error(L"sort", utf8_to_wstring(loaded.error()));
    return 2;
  }

  std::vector<std::string> records = std::move(*loaded);

  if (cfg.mode != OperationMode::Sort) {
    auto disorder = check_sorted(records, cfg);
    if (disorder) {
      if (cfg.mode == OperationMode::Check) {
        cp::report_custom_error(L"sort", L"input is not sorted");
      }
      return 1;
    }
    return 0;
  }

  std::stable_sort(
      records.begin(), records.end(),
      [&](const auto& a, const auto& b) { return is_before(a, b, cfg); });

  if (cfg.unique) {
    std::vector<std::string> unique_records;
    unique_records.reserve(records.size());
    for (const auto& rec : records) {
      if (unique_records.empty()) {
        unique_records.push_back(rec);
        continue;
      }
      if (compare_records_by_sort_key(unique_records.back(), rec, cfg) != 0) {
        unique_records.push_back(rec);
      }
    }
    records = std::move(unique_records);
  }

  std::ostream* out = &std::cout;
  std::ofstream file_out;
  if (!cfg.output_file.empty()) {
    file_out.open(cfg.output_file, std::ios::binary | std::ios::trunc);
    if (!file_out.is_open()) {
      cp::report_custom_error(L"sort", L"cannot open output file");
      return 2;
    }
    out = &file_out;
  }

  for (const auto& rec : records) {
    (*out) << rec;
    (*out) << cfg.delimiter;
  }
  out->flush();
  return 0;
}

}  // namespace sort_pipeline

REGISTER_COMMAND(sort, "sort", "sort [OPTION]... [FILE]...",
                 "Sort lines of text files.\n"
                 "With no FILE, or when FILE is -, read standard input.",
                 "  sort a.txt\n"
                 "  sort -n -r data.txt\n"
                 "  sort -u -k 1 names.txt",
                 "uniq(1), grep(1), head(1)", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", SORT_OPTIONS) {
  using namespace sort_pipeline;

  if (auto unsupported = is_unsupported_used(ctx); unsupported.has_value()) {
    cp::report_custom_error(L"sort", utf8_to_wstring(*unsupported));
    return 2;
  }

  auto cfg = build_config(ctx);
  if (!cfg) {
    cp::report_error(cfg, L"sort");
    return 2;
  }

  return run(*cfg);
}
