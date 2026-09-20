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
/// @Description: Implementation for sort.
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

using cmd::meta::option_matches;
using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr SORT_OPTIONS = std::array{
    // [GNU] -b, --ignore-leading-blanks
    OPTION("-b", "--ignore-leading-blanks", "ignore leading blanks"),
    // [GNU] -d, --dictionary-order
    OPTION("-d", "--dictionary-order",
           "consider only blanks and alphanumeric characters"),
    // [GNU] -f, --ignore-case
    OPTION("-f", "--ignore-case", "fold lower case to upper case"),
    // [GNU] -g, --general-numeric-sort
    OPTION("-g", "--general-numeric-sort",
           "compare according to string numerical value"),
    // [GNU] -i, --ignore-nonprinting
    OPTION("-i", "--ignore-nonprinting", "consider only printable characters"),
    // [GNU] -h, --human-numeric-sort
    OPTION("-h", "--human-numeric-sort",
           "compare human readable numbers (e.g., 1K, 2M)"),
    // [GNU] -M, --month-sort
    OPTION("-M", "--month-sort", "compare as month names"),
    // [GNU] -m, --merge
    OPTION("-m", "--merge", "merge already sorted files"),
    // [GNU] -n, --numeric-sort
    OPTION("-n", "--numeric-sort",
           "compare according to string numerical value"),
    // [GNU] -V, --version-sort
    OPTION("-V", "--version-sort", "compare version numbers naturally"),
    // [GNU] -R, --random-sort
    OPTION("-R", "--random-sort", "sort by random hash of keys"),
    // [GNU] --random-source
    OPTION("", "--random-source", "get random bytes from FILE", STRING_TYPE),
    // [GNU] -r, --reverse
    OPTION("-r", "--reverse", "reverse the result of comparisons"),
    // [GNU] -S, --buffer-size
    OPTION("-S", "--buffer-size",
           "use SIZE for the main memory buffer; accepted as a memory hint",
           STRING_TYPE),
    // [GNU] --parallel
    OPTION("", "--parallel",
           "change the number of sorts run concurrently to N; accepted as a "
           "concurrency hint",
           STRING_TYPE),
    // [GNU] -s, --stable
    OPTION("-s", "--stable",
           "stabilize sort by disabling last-resort comparison"),
    // [GNU] -u, --unique
    OPTION("-u", "--unique", "output only the first of equal runs"),
    // [GNU] -z, --zero-terminated
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline"),
    // [GNU] --files0-from: read input from the file specified
    OPTION("", "--files0-from", "read input from the file specified",
           STRING_TYPE),
    // [GNU] -c takes no argument (a glued "-cquiet" is an invalid option);
    // --check accepts the optional =diagnose-first|quiet|silent argument.
    OPTION("-c", "", "check whether input is sorted; diagnose-first"),
    OPTION("", "--check",
           "check whether input is sorted; accepts =diagnose-first|quiet|"
           "silent",
           OPTIONAL_STRING_TYPE),
    // [GNU] -C, --check-silent
    OPTION("-C", "--check-silent", "check whether input is sorted quietly"),
    // [GNU] --debug
    OPTION("", "--debug", "print sort key diagnostics to standard error"),
    // [GNU] -o, --output
    OPTION("-o", "--output", "write result to FILE instead of standard output",
           STRING_TYPE),
    // [GNU] option
    OPTION(
        "", "--files0-from",
        "read input from the files specified by NUL-terminated names in FILE",
        STRING_TYPE),
    // [GNU] --batch-size
    OPTION("", "--batch-size",
           "merge at most NMERGE inputs at once; accepted as a merge hint",
           STRING_TYPE),
    // [GNU] --compress-program
    OPTION("", "--compress-program",
           "compress temporaries with PROG; accepted as a compression hint",
           STRING_TYPE),
    // [GNU] -T, --temporary-directory
    OPTION("-T", "--temporary-directory",
           "use DIR for temporaries; accepted as a temporary-directory hint",
           STRING_TYPE),
    // [GNU] --sort
    OPTION("", "--sort", "set sort order; 'version' enables version sort",
           STRING_TYPE),
    // [GNU] -t, --field-separator
    OPTION("-t", "--field-separator",
           "use SEP instead of non-blank to blank transition", STRING_TYPE),
    // [GNU] -k, --key
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
  std::string batch_size_hint;
  std::string compress_program_hint;
  std::string temporary_directory_hint;
  std::string random_source;
  std::string parallel_hint;
  uint64_t random_seed = 0;
  std::vector<KeySpec> keys;
  SmallVector<std::string, 64>
      files{};  // SmallVector for paths, stack-allocated
};

auto read_all(std::istream& in) -> std::string { return read_text_stream(in); }

auto describe_input_open_failure(std::string_view path) -> std::string {
  // Probe through the extended API path so >MAX_PATH operands and DOS
  // device names resolve like the open itself (#1061).
  DWORD attrs = native_path::attributes_w(utf8_to_wstring(std::string(path)));
  if (attrs == INVALID_FILE_ATTRIBUTES) {
    return "No such file or directory";
  }
  if ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) {
    return "Is a directory";
  }
  return "Permission denied";
}

auto read_source(std::string_view path) -> cp::Result<std::string> {
  // [GNU] A closed standard input (<&-) is a stat/read error, not EOF
  // (#973). GNU reports "sort: stat failed: -: Bad file descriptor".
  if (path == "-") {
    if (file_io::stdin_is_bad()) {
      return std::unexpected("stat failed: -: Bad file descriptor");
    }
    return read_all(std::cin);
  }

  auto in = file_io::open_binary_file(path);
  if (!in.is_open()) {
    return std::unexpected("cannot read: " + std::string(path) + ": " +
                           describe_input_open_failure(path));
  }
  return read_all(in);
}

auto read_simple_lexical_source(std::string_view path)
    -> cp::Result<std::string> {
  if (path == "-") return read_source(path);

  auto in = file_io::open_binary_file(path);
  in.seekg(0, std::ios::end);
  if (!in.is_open()) {
    return std::unexpected("cannot read: " + std::string(path) + ": " +
                           describe_input_open_failure(path));
  }

  const auto end = in.tellg();
  if (end < 0) return read_source(path);

  std::string bytes(static_cast<size_t>(end), '\0');
  in.seekg(0, std::ios::beg);
  if (!bytes.empty()) {
    in.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!in && !in.eof()) return read_source(path);
    bytes.resize(static_cast<size_t>(in.gcount()));
  }

  if (bytes.size() >= 3 && static_cast<std::uint8_t>(bytes[0]) == 0xEF &&
      static_cast<std::uint8_t>(bytes[1]) == 0xBB &&
      static_cast<std::uint8_t>(bytes[2]) == 0xBF) {
    bytes.erase(0, 3);
  }
  return bytes;
}

auto read_binary_source(std::string_view path) -> cp::Result<std::string> {
  if (path == "-") {
    if (file_io::stdin_is_bad()) {
      return std::unexpected("stat failed: -: Bad file descriptor");
    }
    return std::string{std::istreambuf_iterator<char>{std::cin},
                       std::istreambuf_iterator<char>{}};
  }

  auto in = file_io::open_binary_file(path);
  if (!in.is_open()) {
    return std::unexpected("cannot read: " + std::string(path) + ": " +
                           describe_input_open_failure(path));
  }
  return std::string{std::istreambuf_iterator<char>{in},
                     std::istreambuf_iterator<char>{}};
}

auto read_files0_from(const std::string& path)
    -> cp::Result<std::vector<std::string>> {
  std::istream* input = nullptr;
  std::ifstream file;
  if (path == "-") {
    if (file_io::stdin_is_bad()) {
      return std::unexpected("stat failed: -: Bad file descriptor");
    }
    input = &std::cin;
  } else {
    file = file_io::open_binary_file(path);
    if (!file.is_open()) {
      return std::unexpected("cannot open file list '" + path + "'");
    }
    input = &file;
  }

  std::vector<std::string> paths;
  std::string name;
  size_t file_number = 1;
  while (std::getline(*input, name, '\0')) {
    if (name.empty()) {
      return std::unexpected(path + ":" + std::to_string(file_number) +
                             ": invalid zero-length file name");
    }
    if (name == "-") {
      return std::unexpected(
          "when reading file names from standard input, "
          "no file name of '-' allowed");
    }
    paths.push_back(name);
    ++file_number;
  }
  if (paths.empty()) return std::unexpected("no input from " + path);
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
    out = ascii_lower_copy(out);
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
  if (text.empty())
    return std::unexpected("invalid key spec '" + std::string(text) + "'");

  ParsedKeyPosition pos;
  size_t i = 0;
  while (i < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
    ++i;
  }
  if (i == 0) {
    return std::unexpected("invalid key spec '" + std::string(text) + "'");
  }

  auto field_text = text.substr(0, i);
  // [GNU] overflowing field/char numbers are accepted and clamp (the field is
  // beyond every line, so the key is empty; uutils #7185).
  unsigned long long field_big = 0;
  auto [ptr, ec] = std::from_chars(
      field_text.data(), field_text.data() + field_text.size(), field_big);
  if ((ec != std::errc() && ec != std::errc::result_out_of_range) ||
      ptr != field_text.data() + field_text.size()) {
    return std::unexpected("invalid key spec '" + std::string(text) + "'");
  }
  pos.field = ec == std::errc::result_out_of_range
                  ? std::numeric_limits<size_t>::max()
                  : static_cast<size_t>(field_big);
  if (pos.field == 0) {
    return std::unexpected("invalid key spec '" + std::string(text) + "'");
  }

  if (i < text.size() && text[i] == '.') {
    ++i;
    size_t char_start = i;
    while (i < text.size() &&
           std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
      ++i;
    }
    if (i == char_start) {
      return std::unexpected("invalid key spec '" + std::string(text) + "'");
    }
    size_t value = 0;
    auto char_text = text.substr(char_start, i - char_start);
    auto [char_ptr, char_ec] = std::from_chars(
        char_text.data(), char_text.data() + char_text.size(), value);
    if (char_ec == std::errc::result_out_of_range) {
      // [GNU] clamp overflowing character offsets like field numbers.
      value = std::numeric_limits<size_t>::max();
    } else if (char_ec != std::errc() ||
               char_ptr != char_text.data() + char_text.size()) {
      return std::unexpected("invalid key spec '" + std::string(text) + "'");
    }
    pos.character = value;
  }

  for (size_t j = i; j < text.size(); ++j) {
    if (!is_key_modifier(text[j])) {
      return std::unexpected("invalid key spec '" + std::string(text) + "'");
    }
  }
  pos.modifiers = text.substr(i);
  return pos;
}

auto parse_key_spec(std::string_view text) -> cp::Result<KeySpec> {
  if (text.empty())
    return std::unexpected("invalid key spec '" + std::string(text) + "'");

  KeySpec key;

  auto comma = text.find(',');
  auto first = parse_key_position(text.substr(0, comma));
  if (!first) return std::unexpected(first.error());
  if (first->character.has_value() && *first->character == 0) {
    return std::unexpected("invalid key spec '" + std::string(text) + "'");
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
  auto lowered = ascii_lower_copy(text);
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

auto parse_parallel_hint(std::string_view text) -> bool {
  if (text.empty()) return false;

  unsigned int value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (ec != std::errc() || ptr != text.data() + text.size()) {
    return false;
  }
  return value > 0;
}

// [GNU] --batch-size is bounded by the number of files that can be open at
// once: RLIMIT_NOFILE - 3 (stdin/stdout/stderr). Windows has no rlimit; the
// MSYS2/Cygwin builds of GNU sort report a 3200 fd budget (OPEN_MAX), so the
// same cap keeps the diagnostics identical on this platform.
inline constexpr unsigned int MAX_BATCH_SIZE_HINT = 3200 - 3;

// Returns an empty string when the hint is valid, otherwise the GNU-style
// diagnostic to print.
auto validate_batch_size_hint(std::string_view text) -> std::string {
  uintmax_t value = 0;
  auto [ptr, ec] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || ec != std::errc() || ptr != text.data() + text.size()) {
    return "invalid --batch-size argument '" + std::string(text) + "'";
  }
  if (value < 2) {
    return "invalid --batch-size argument '" + std::string(text) +
           "'\nsort: minimum --batch-size argument is '2'";
  }
  if (value > MAX_BATCH_SIZE_HINT) {
    return "--batch-size argument '" + std::string(text) +
           "' too large\nsort: maximum --batch-size argument with current "
           "rlimit is " +
           std::to_string(MAX_BATCH_SIZE_HINT);
  }
  return {};
}

auto validate_compress_program_hint(std::string_view text) -> bool {
  return !text.empty();
}

auto validate_temporary_directory_hint(std::string_view text) -> bool {
  if (text.empty()) return false;

  std::error_code ec;
  const auto path = std::filesystem::u8path(text);
  return std::filesystem::exists(path, ec) &&
         std::filesystem::is_directory(path, ec) && !ec;
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
  // [GNU] -c and --check select the diagnose-first check mode, while -C,
  // --check-silent and --check=quiet/silent select the quiet mode. Mixing
  // the two modes is rejected at parse time ("options '-cC' are
  // incompatible"). --check values follow argmatch rules: unique prefixes
  // of {quiet, silent, diagnose-first} are accepted, an explicitly empty
  // "--check=" is an ambiguity, and anything else is an invalid argument.
  bool want_check = false;
  bool want_check_quiet = false;
  bool check_inline_empty = false;
  for (std::string_view raw : ctx.raw_args) {
    if (raw == "--") break;
    if (raw == "--check=") check_inline_empty = true;
  }
  for (const auto& occurrence : ctx.options.occurrences()) {
    if (!ctx.metas || occurrence.index >= SORT_OPTIONS.size()) continue;
    const auto& meta = (*ctx.metas)[occurrence.index];
    if (option_matches(meta, "-C", "--check-silent")) {
      want_check_quiet = true;
      continue;
    }
    if (meta.short_name == "-c" && meta.long_name.empty()) {
      want_check = true;
      continue;
    }
    if (meta.long_name != "--check") continue;

    const auto* value = std::get_if<std::string>(&occurrence.value);
    const std::string text = value != nullptr ? *value : std::string();
    if (text.empty()) {
      if (check_inline_empty) {
        // Printed specially by the command entry point.
        return std::unexpected("ambiguous argument '' for '--check'");
      }
      want_check = true;
      continue;
    }
    static constexpr std::array<std::string_view, 3> kCheckArgs{
        "quiet", "silent", "diagnose-first"};
    std::string_view matched;
    size_t match_count = 0;
    for (const auto candidate : kCheckArgs) {
      if (candidate.starts_with(text)) {
        matched = candidate;
        ++match_count;
      }
    }
    if (match_count > 1) {
      return std::unexpected("ambiguous argument '" + text + "' for '--check'");
    }
    if (match_count == 0) {
      // Printed specially by the command entry point.
      return std::unexpected("invalid argument '" + text + "' for '--check'");
    }
    if (matched == "diagnose-first") {
      want_check = true;
    } else {
      want_check_quiet = true;
    }
  }
  if (want_check && want_check_quiet) {
    return std::unexpected("options '-cC' are incompatible");
  }
  if (want_check) {
    cfg.mode = OperationMode::Check;
  } else if (want_check_quiet) {
    cfg.mode = OperationMode::CheckQuiet;
  }
  cfg.delimiter =
      (ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false))
          ? '\0'
          : '\n';

  // [GNU] only the last -o would matter, but more than one output file is
  // rejected outright ("multiple output files specified").
  if (ctx.count({"-o", "--output"}) > 1) {
    return std::unexpected("multiple output files specified");
  }
  cfg.output_file = ctx.get<std::string>("--output", "");
  if (cfg.output_file.empty()) cfg.output_file = ctx.get<std::string>("-o", "");
  if (cfg.mode != OperationMode::Sort && !cfg.output_file.empty()) {
    // [GNU] quotes the effective check option letter: "-co" for -c/--check
    // and "-Co" for -C/--check-silent/--check=quiet|silent.
    return std::unexpected(
        std::string("options '-") +
        (cfg.mode == OperationMode::CheckQuiet ? "Co" : "co") +
        "' are incompatible");
  }
  if (cfg.debug && cfg.mode != OperationMode::Sort) {
    return std::unexpected(std::string("options '-") +
                           (cfg.mode == OperationMode::CheckQuiet ? "C" : "c") +
                           " --debug' are incompatible");
  }
  if (cfg.debug && !cfg.output_file.empty()) {
    return std::unexpected("options '-o --debug' are incompatible");
  }
  cfg.files0_from = ctx.get<std::string>("--files0-from", "");
  cfg.batch_size_hint = ctx.get<std::string>("--batch-size", "");
  cfg.compress_program_hint = ctx.get<std::string>("--compress-program", "");
  cfg.temporary_directory_hint =
      ctx.get<std::string>("--temporary-directory", "");
  if (cfg.temporary_directory_hint.empty()) {
    cfg.temporary_directory_hint = ctx.get<std::string>("-T", "");
  }
  cfg.random_source = ctx.get<std::string>("--random-source", "");
  cfg.parallel_hint = ctx.get<std::string>("--parallel", "");

  std::string buffer_size = ctx.get<std::string>("--buffer-size", "");
  if (buffer_size.empty()) buffer_size = ctx.get<std::string>("-S", "");
  if ((ctx.has("--buffer-size") || ctx.has("-S")) &&
      !parse_buffer_size_hint(buffer_size)) {
    return std::unexpected("invalid buffer size");
  }

  if (ctx.has("--parallel") && !parse_parallel_hint(cfg.parallel_hint)) {
    // [GNU] reports the rejected operand (uutils #13016)
    return std::unexpected("invalid --parallel argument '" + cfg.parallel_hint +
                           "'");
  }

  if (ctx.has("--batch-size")) {
    auto batch_error = validate_batch_size_hint(cfg.batch_size_hint);
    if (!batch_error.empty()) return std::unexpected(batch_error);
  }

  if (ctx.has("--compress-program") &&
      !validate_compress_program_hint(cfg.compress_program_hint)) {
    return std::unexpected("invalid compress program");
  }

  if ((ctx.has("--temporary-directory") || ctx.has("-T")) &&
      !validate_temporary_directory_hint(cfg.temporary_directory_hint)) {
    return std::unexpected("invalid temporary directory");
  }

  std::string sep = ctx.get<std::string>("--field-separator", "");
  if (sep.empty()) sep = ctx.get<std::string>("-t", "");
  // [GNU] sort.c:4660: an explicitly empty -t separator is the error
  // "empty tab", not "no separator given".
  if (sep.empty() && (ctx.has("--field-separator") || ctx.has("-t"))) {
    return std::unexpected("empty tab");
  }
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

  if (cfg.mode != OperationMode::Sort && cfg.files.size() > 1) {
    // [GNU] reports the extra operand as "not allowed with -c" for every
    // check-mode variant, including -C and --check=quiet.
    return std::unexpected("extra operand '" + cfg.files[1] +
                           "' not allowed with -c");
  }

  return cfg;
}

auto load_records(const Config& cfg) -> cp::Result<std::vector<std::string>> {
  std::vector<std::string> records;
  for (size_t i = 0; i < cfg.files.size(); ++i) {
    auto content = (cfg.delimiter == '\0') ? read_binary_source(cfg.files[i])
                                           : read_source(cfg.files[i]);
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

auto load_record_chunks(const Config& cfg)
    -> cp::Result<std::vector<std::vector<std::string>>> {
  std::vector<std::vector<std::string>> chunks;
  chunks.reserve(cfg.files.size());
  for (size_t i = 0; i < cfg.files.size(); ++i) {
    auto content = (cfg.delimiter == '\0') ? read_binary_source(cfg.files[i])
                                           : read_source(cfg.files[i]);
    if (!content) {
      return std::unexpected(content.error());
    }
    chunks.push_back(split_records(*content, cfg.delimiter));
  }
  return chunks;
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

auto can_use_simple_lexical_compare(const Config& cfg) -> bool {
  return cfg.keys.empty() && !cfg.ignore_leading_blanks &&
         !cfg.dictionary_order && !cfg.ignore_case && !cfg.ignore_nonprinting &&
         !cfg.numeric_sort && !cfg.version_sort && !cfg.human_numeric &&
         !cfg.month_sort && !cfg.general_numeric && !cfg.random_sort;
}

auto write_records_to_file(std::ostream& out,
                           const std::vector<std::string>& records,
                           char delimiter) -> void {
  std::string output;
  output.reserve(1024 * 1024);

  auto flush = [&]() {
    if (output.empty()) return;
    out.write(output.data(), static_cast<std::streamsize>(output.size()));
    output.clear();
  };

  for (const auto& rec : records) {
    if (output.size() + rec.size() + 1 > output.capacity()) {
      flush();
    }
    output.append(rec);
    output.push_back(delimiter);
  }
  flush();
  out.flush();
}

auto write_records_to_stdout(const std::vector<std::string>& records,
                             char delimiter) -> void {
  std::string output;
  output.reserve(1024 * 1024);

  auto flush = [&]() {
    if (output.empty()) return;
    safePrint(std::string_view(output.data(), output.size()));
    output.clear();
  };

  for (const auto& rec : records) {
    if (output.size() + rec.size() + 1 > output.capacity()) {
      flush();
      if (is_stdout_pipe_closed()) return;
    }
    output.append(rec);
    output.push_back(delimiter);
  }
  flush();
}

auto split_record_views(std::string_view content, char delimiter)
    -> std::vector<std::string_view> {
  std::vector<std::string_view> out;
  out.reserve(content.size() / 20);

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

void write_record_views_to_file(std::ostream& out,
                                const std::vector<std::string_view>& records,
                                char delimiter) {
  std::string output;
  output.reserve(1024 * 1024);

  auto flush = [&]() {
    if (output.empty()) return;
    out.write(output.data(), static_cast<std::streamsize>(output.size()));
    output.clear();
  };

  for (const auto rec : records) {
    if (output.size() + rec.size() + 1 > output.capacity()) {
      flush();
    }
    output.append(rec);
    output.push_back(delimiter);
  }
  flush();
  out.flush();
}

void write_record_views_to_stdout(const std::vector<std::string_view>& records,
                                  char delimiter) {
  std::string output;
  output.reserve(1024 * 1024);

  auto flush = [&]() {
    if (output.empty()) return;
    safePrint(std::string_view(output.data(), output.size()));
    output.clear();
  };

  for (const auto rec : records) {
    if (output.size() + rec.size() + 1 > output.capacity()) {
      flush();
      if (is_stdout_pipe_closed()) return;
    }
    output.append(rec);
    output.push_back(delimiter);
  }
  flush();
}

auto try_run_simple_lexical_block_sort(const Config& cfg)
    -> std::optional<int> {
  if (cfg.mode != OperationMode::Sort || !can_use_simple_lexical_compare(cfg) ||
      cfg.merge || cfg.stable || cfg.unique || cfg.files.size() != 1) {
    return std::nullopt;
  }

  auto content = (cfg.delimiter == '\0')
                     ? read_binary_source(cfg.files[0])
                     : read_simple_lexical_source(cfg.files[0]);
  if (!content) {
    cp::report_custom_error(L"sort", utf8_to_wstring(content.error()));
    return 2;
  }

  auto records = split_record_views(*content, cfg.delimiter);
  if (cfg.reverse) {
    std::sort(records.begin(), records.end(), std::greater<>{});
  } else {
    std::sort(records.begin(), records.end());
  }

  std::ofstream file_out;
  if (!cfg.output_file.empty()) {
    file_out.open(cfg.output_file, std::ios::binary | std::ios::trunc);
    if (!file_out.is_open()) {
      cp::report_custom_error(L"sort", L"cannot open output file");
      return 2;
    }
    write_record_views_to_file(file_out, records, cfg.delimiter);
  } else {
    write_record_views_to_stdout(records, cfg.delimiter);
  }
  return 0;
}

struct MergeCursor {
  size_t chunk_index = 0;
  size_t record_index = 0;
};

auto merge_record_chunks(const Config& cfg)
    -> cp::Result<std::vector<std::string>> {
  auto loaded_chunks = load_record_chunks(cfg);
  if (!loaded_chunks) return std::unexpected(loaded_chunks.error());

  auto chunks = std::move(*loaded_chunks);
  size_t total_records = 0;
  for (const auto& chunk : chunks) total_records += chunk.size();

  auto better_cursor = [&](const MergeCursor& lhs, const MergeCursor& rhs) {
    const auto& left = chunks[lhs.chunk_index][lhs.record_index];
    const auto& right = chunks[rhs.chunk_index][rhs.record_index];
    if (is_before(left, right, cfg)) return true;
    if (is_before(right, left, cfg)) return false;
    if (lhs.chunk_index != rhs.chunk_index) {
      return lhs.chunk_index < rhs.chunk_index;
    }
    return lhs.record_index < rhs.record_index;
  };
  auto worse_cursor = [&](const MergeCursor& lhs, const MergeCursor& rhs) {
    return better_cursor(rhs, lhs);
  };

  std::priority_queue<MergeCursor, std::vector<MergeCursor>,
                      decltype(worse_cursor)>
      queue(worse_cursor);
  for (size_t i = 0; i < chunks.size(); ++i) {
    if (!chunks[i].empty()) queue.push(MergeCursor{i, 0});
  }

  std::vector<std::string> records;
  records.reserve(total_records);
  while (!queue.empty()) {
    auto cursor = queue.top();
    queue.pop();
    records.push_back(chunks[cursor.chunk_index][cursor.record_index]);
    ++cursor.record_index;
    if (cursor.record_index < chunks[cursor.chunk_index].size()) {
      queue.push(cursor);
    }
  }
  return records;
}

struct ExternalRun {
  std::filesystem::path path;
  std::ifstream input;
  std::string current;
  bool has_current = false;
};

auto external_sort(const Config& cfg) -> cp::Result<int> {
  constexpr size_t kChunkBytes = 8 * 1024 * 1024;
  std::vector<std::filesystem::path> temporary_paths;
  std::vector<ExternalRun> runs;
  size_t run_number = 0;

  std::filesystem::path temp_dir =
      cfg.temporary_directory_hint.empty()
          ? std::filesystem::temp_directory_path()
          // Wide form: the narrow path ctor decodes via the system ACP (#88).
          : std::filesystem::path(
                utf8_to_wstring(cfg.temporary_directory_hint));
  auto make_run = [&](std::vector<std::string>& records) -> cp::Result<bool> {
    if (records.empty()) return true;
    auto before = [&](const std::string& a, const std::string& b) {
      return is_before(a, b, cfg);
    };
    if (cfg.stable || cfg.unique) {
      std::stable_sort(records.begin(), records.end(), before);
    } else {
      std::sort(records.begin(), records.end(), before);
    }
    const auto path =
        temp_dir / ("winuxcmd-sort-" + std::to_string(GetCurrentProcessId()) +
                    "-" + std::to_string(run_number++) + ".tmp");
    auto output = file_io::create_binary_file(wstring_to_utf8(path.wstring()));
    if (!output.is_open())
      return std::unexpected("cannot create temporary file");
    for (const auto& record : records) {
      output.write(record.data(), static_cast<std::streamsize>(record.size()));
      output.put(cfg.delimiter);
    }
    if (!output) return std::unexpected("cannot write temporary file");
    output.close();
    temporary_paths.push_back(path);
    records.clear();
    return true;
  };

  std::vector<std::string> records;
  size_t bytes = 0;
  auto consume = [&](std::istream& input) -> cp::Result<bool> {
    std::string record;
    while (std::getline(input, record, cfg.delimiter)) {
      bytes += record.size() + 1;
      records.push_back(std::move(record));
      record.clear();
      if (bytes >= kChunkBytes) {
        auto result = make_run(records);
        if (!result) return std::unexpected(result.error());
        bytes = 0;
      }
    }
    if (input.bad()) return std::unexpected("error reading input");
    return true;
  };

  for (const auto& filename : cfg.files) {
    if (filename == "-") {
      auto stdin_content = read_source(filename);
      if (!stdin_content) return std::unexpected(stdin_content.error());
      std::istringstream decoded_input(*stdin_content);
      auto result = consume(decoded_input);
      if (!result) return std::unexpected(result.error());
    } else {
      auto input = file_io::open_binary_file(filename);
      if (!input.is_open()) {
        return std::unexpected("cannot read: " + filename + ": " +
                               describe_input_open_failure(filename));
      }
      auto result = consume(input);
      if (!result) return std::unexpected(result.error());
    }
  }
  auto final_run = make_run(records);
  if (!final_run) return std::unexpected(final_run.error());

  std::ofstream output_file;
  std::ostream* output = &std::cout;
  int stdout_mode = -1;
  if (!cfg.output_file.empty()) {
    output_file = file_io::create_binary_file(cfg.output_file);
    if (!output_file.is_open())
      return std::unexpected("cannot open output file");
    output = &output_file;
  } else {
    stdout_mode = _setmode(_fileno(stdout), _O_BINARY);
  }

  for (auto& path : temporary_paths) {
    ExternalRun run{path,
                    file_io::open_binary_file(wstring_to_utf8(path.wstring()))};
    if (!run.input.is_open())
      return std::unexpected("cannot open temporary file");
    run.has_current =
        static_cast<bool>(std::getline(run.input, run.current, cfg.delimiter));
    runs.push_back(std::move(run));
  }
  auto less = [&](size_t left, size_t right) {
    if (is_before(runs[left].current, runs[right].current, cfg)) return false;
    if (is_before(runs[right].current, runs[left].current, cfg)) return true;
    return left > right;
  };
  std::priority_queue<size_t, std::vector<size_t>, decltype(less)> queue(less);
  for (size_t i = 0; i < runs.size(); ++i)
    if (runs[i].has_current) queue.push(i);

  std::string previous;
  bool have_previous = false;
  while (!queue.empty()) {
    const size_t index = queue.top();
    queue.pop();
    auto& run = runs[index];
    const bool duplicate =
        have_previous &&
        compare_records_by_sort_key(previous, run.current, cfg) == 0;
    if (!cfg.unique || !duplicate) {
      output->write(run.current.data(),
                    static_cast<std::streamsize>(run.current.size()));
      output->put(cfg.delimiter);
      previous = run.current;
      have_previous = true;
    }
    run.current.clear();
    run.has_current =
        static_cast<bool>(std::getline(run.input, run.current, cfg.delimiter));
    if (run.has_current) queue.push(index);
  }
  output->flush();
  runs.clear();
  for (const auto& path : temporary_paths) std::filesystem::remove(path);
  if (stdout_mode != -1) _setmode(_fileno(stdout), stdout_mode);
  return 0;
}

auto run(const Config& cfg) -> int {
  emit_debug_diagnostics(cfg);

  const bool simple_lexical = can_use_simple_lexical_compare(cfg);
  std::vector<std::string> records;

  if (cfg.mode != OperationMode::Sort) {
    auto loaded = load_records(cfg);
    if (!loaded) {
      cp::report_custom_error(L"sort", utf8_to_wstring(loaded.error()));
      return 2;
    }
    records = std::move(*loaded);
    auto disorder = check_sorted(records, cfg);
    if (disorder) {
      if (cfg.mode == OperationMode::Check) {
        const std::string input_name =
            cfg.files.empty() ? "-" : cfg.files.front();
        const size_t line_number = *disorder + 1;
        std::string message = input_name + ":" + std::to_string(line_number) +
                              ": disorder: " + records[*disorder];
        cp::report_custom_error(L"sort", utf8_to_wstring(message));
      }
      return 1;
    }
    return 0;
  }

  if (cfg.merge) {
    auto merged = merge_record_chunks(cfg);
    if (!merged) {
      cp::report_custom_error(L"sort", utf8_to_wstring(merged.error()));
      return 2;
    }
    records = std::move(*merged);
  } else {
    // External runs keep ordinary sorts bounded by a fixed memory chunk.
    auto external = external_sort(cfg);
    if (!external) {
      cp::report_custom_error(L"sort", utf8_to_wstring(external.error()));
      return 2;
    }
    return *external;
  }

  if (cfg.unique) {
    std::vector<std::string> unique_records;
    unique_records.reserve(records.size());
    for (const auto& rec : records) {
      if (unique_records.empty()) {
        unique_records.push_back(rec);
        continue;
      }
      const bool distinct = simple_lexical
                                ? unique_records.back() != rec
                                : compare_records_by_sort_key(
                                      unique_records.back(), rec, cfg) != 0;
      if (distinct) {
        unique_records.push_back(rec);
      }
    }
    records = std::move(unique_records);
  }

  std::ofstream file_out;
  if (!cfg.output_file.empty()) {
    file_out.open(cfg.output_file, std::ios::binary | std::ios::trunc);
    if (!file_out.is_open()) {
      cp::report_custom_error(L"sort", L"cannot open output file");
      return 2;
    }
    write_records_to_file(file_out, records, cfg.delimiter);
  } else {
    write_records_to_stdout(records, cfg.delimiter);
  }
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
    // [GNU] argmatch-style diagnostic for a bad --check=ARG: the rejected
    // value, the valid-arguments list and the help hint. GNU sort exits 1
    // here (usage(EXIT_FAILURE)), unlike the exit 2 used for other usage
    // errors.
    const std::string& err = cfg.error();
    if ((err.starts_with("invalid argument '") ||
         err.starts_with("ambiguous argument '")) &&
        err.ends_with("' for '--check'")) {
      safeErrorPrintLn("sort: " + err);
      safeErrorPrintLn("Valid arguments are:");
      safeErrorPrintLn("  - 'quiet', 'silent'");
      safeErrorPrintLn("  - 'diagnose-first'");
      safeErrorPrintLn("Try 'sort --help' for more information.");
      return 1;
    }
    cp::report_error(cfg, L"sort");
    return 2;
  }

  return run(*cfg);
}
