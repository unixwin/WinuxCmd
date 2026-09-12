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
 *  - File: wc.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"
import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Constants
// ======================================================
namespace wc_constants {
constexpr size_t READ_BUFFER_SIZE = 64 * 1024;
}

// ======================================================
// Options (constexpr)
// ======================================================

/**
 * @brief WC command options definition
 *
 * This array defines all the options supported by the wc command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -c, @a --bytes: Print the byte counts [IMPLEMENTED]
 * - @a -m, @a --chars: Print the character counts [IMPLEMENTED]
 * - @a -l, @a --lines: Print the newline counts [IMPLEMENTED]
 * - @a --debug:
 * Print line-count implementation diagnostics [IMPLEMENTED]
 * - @a
 * --files0-from=F: Read input from the files specified by NUL-terminated
 *
 * names in file F [IMPLEMENTED]
 * - @a -L, @a --max-line-length: Print the
 * maximum display width [IMPLEMENTED]
 * - @a -w, @a --words: Print the word counts [IMPLEMENTED]
 * - @a --total=WHEN: When to print a line with total counts [IMPLEMENTED]
 * - @a --version: Output version information and exit [IMPLEMENTED]
 */
auto constexpr WC_OPTIONS = std::array{
    // [GNU] option
    OPTION("-c", "--bytes", "print the byte counts"),
    // [GNU] option
    OPTION("-m", "--chars", "print the character counts"),
    // [GNU] option
    OPTION("-l", "--lines", "print the newline counts"),
    // [GNU] option
    OPTION("", "--debug", "print line-count implementation diagnostics"),
    // [GNU] option
    OPTION(
        "", "--files0-from",
        "read input from the files specified by NUL-terminated names in file F",
        STRING_TYPE),
    // [GNU] option
    OPTION("-L", "--max-line-length", "print the maximum display width"),
    // [GNU] option
    OPTION("-w", "--words", "print the word counts"),
    // [GNU] option
    OPTION("", "--total", "when to print a line with total counts",
           STRING_TYPE)};

// ======================================================
// Pipeline components
// ======================================================
namespace wc_pipeline {
namespace cp = core::pipeline;

// ----------------------------------------------
// 1. Types
// ----------------------------------------------
/**
 * @brief Structure to store count results
 */
struct CountResult {
  std::uintmax_t lines = 0;
  std::uintmax_t words = 0;
  std::uintmax_t chars = 0;
  std::uintmax_t bytes = 0;
  std::uintmax_t max_line_length = 0;
  std::string filename;
  bool display_filename = true;
};

struct CountBatch {
  std::vector<CountResult> results;
  bool any_error = false;
  size_t requested_input_count = 0;
};

struct Files0ReadResult {
  std::vector<std::string> paths;
  bool from_stdin = false;
};

struct DecodedChar {
  char32_t codepoint = 0;
  size_t bytes = 1;
};

struct CountRequest {
  bool lines = false;
  bool words = false;
  bool chars = false;
  bool bytes = false;
  bool max_line_length = false;

  [[nodiscard]] auto only_bytes() const -> bool {
    return bytes && !lines && !words && !chars && !max_line_length;
  }

  [[nodiscard]] auto lines_and_optional_bytes_only() const -> bool {
    return lines && !words && !chars && !max_line_length;
  }
};

// ----------------------------------------------
// 2. Validate arguments
// ----------------------------------------------
/**
 * @brief Validate command arguments
 *
 * This function validates the command arguments and returns a list of files to
 * process. If no files are provided, it returns an empty list indicating stdin
 * should be used.
 *
 * @param args Command line arguments
 * @return A Result containing the list of files to process
 */
auto validate_arguments(std::span<const std::string_view> args)
    -> cp::Result<std::vector<std::string>> {
  std::vector<std::string> paths;
  for (auto arg : args) {
    std::string file_arg(arg);
    if (contains_wildcard(file_arg)) {
      auto glob_result = glob_expand(file_arg);
      if (glob_result.expanded) {
        for (const auto& file : glob_result.files) {
          paths.push_back(wstring_to_utf8(file));
        }
        continue;
      }
    }
    paths.push_back(file_arg);
  }
  return paths;
}

auto wc_input_open_error(std::string_view path) -> std::string {
  std::error_code ec;
  auto status = std::filesystem::status(std::filesystem::u8path(path), ec);
  if (!ec && status.type() == std::filesystem::file_type::directory) {
    return std::string(path) + ": Is a directory";
  }
  return "cannot open '" + std::string(path) +
         "' for reading: No such file or directory";
}

auto read_files0_from(const std::string& path) -> cp::Result<Files0ReadResult> {
  std::istream* input = nullptr;
  std::ifstream file;
  Files0ReadResult result;
  if (path == "-") {
    input = &std::cin;
    result.from_stdin = true;
  } else {
    file.open(native_path::normalize_api_operand(path), std::ios::binary);
    if (!file) {
      return std::unexpected(wc_input_open_error(path));
    }
    input = &file;
  }
  std::string name;
  while (std::getline(*input, name, '\0')) {
    if (name.empty()) {
      return std::unexpected("invalid zero-length file name");
    }
    if (result.from_stdin && name == "-") {
      return std::unexpected(
          "when reading file names from stdin, no file name of '-' allowed");
    }
    result.paths.push_back(name);
  }
  return result;
}

// ----------------------------------------------
// 3. Count file contents
// ----------------------------------------------
auto decode_utf8_char(std::string_view text, size_t pos) -> DecodedChar {
  unsigned char first = static_cast<unsigned char>(text[pos]);
  if (first < 0x80) return {first, 1};

  auto continuation = [&](size_t index) {
    return index < text.size() &&
           (static_cast<unsigned char>(text[index]) & 0xC0) == 0x80;
  };

  if ((first & 0xE0) == 0xC0 && continuation(pos + 1)) {
    char32_t cp = ((first & 0x1F) << 6) |
                  (static_cast<unsigned char>(text[pos + 1]) & 0x3F);
    if (cp >= 0x80) return {cp, 2};
  }

  if ((first & 0xF0) == 0xE0 && continuation(pos + 1) &&
      continuation(pos + 2)) {
    char32_t cp = ((first & 0x0F) << 12) |
                  ((static_cast<unsigned char>(text[pos + 1]) & 0x3F) << 6) |
                  (static_cast<unsigned char>(text[pos + 2]) & 0x3F);
    if (cp >= 0x800 && !(cp >= 0xD800 && cp <= 0xDFFF)) return {cp, 3};
  }

  if ((first & 0xF8) == 0xF0 && continuation(pos + 1) &&
      continuation(pos + 2) && continuation(pos + 3)) {
    char32_t cp = ((first & 0x07) << 18) |
                  ((static_cast<unsigned char>(text[pos + 1]) & 0x3F) << 12) |
                  ((static_cast<unsigned char>(text[pos + 2]) & 0x3F) << 6) |
                  (static_cast<unsigned char>(text[pos + 3]) & 0x3F);
    if (cp >= 0x10000 && cp <= 0x10FFFF) return {cp, 4};
  }

  return {first, 1};
}

auto is_ascii_space(char32_t cp) -> bool {
  return cp <= 0x7F && std::isspace(static_cast<unsigned char>(cp)) != 0;
}

auto is_wide_codepoint(char32_t cp) -> bool {
  return (cp >= 0x1100 && cp <= 0x115F) || (cp >= 0x2329 && cp <= 0x232A) ||
         (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xAC00 && cp <= 0xD7A3) ||
         (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE10 && cp <= 0xFE19) ||
         (cp >= 0xFE30 && cp <= 0xFE6F) || (cp >= 0xFF00 && cp <= 0xFF60) ||
         (cp >= 0xFFE0 && cp <= 0xFFE6) || (cp >= 0x1F300 && cp <= 0x1FAFF) ||
         (cp >= 0x20000 && cp <= 0x3FFFD);
}

auto display_width(char32_t cp) -> std::uintmax_t {
  if (cp < 0x20 || (cp >= 0x7F && cp < 0xA0)) return 0;
  return is_wide_codepoint(cp) ? 2 : 1;
}

auto utf8_expected_bytes(unsigned char first) -> size_t {
  if (first < 0x80) return 1;
  if ((first & 0xE0) == 0xC0) return 2;
  if ((first & 0xF0) == 0xE0) return 3;
  if ((first & 0xF8) == 0xF0) return 4;
  return 1;
}

/**
 * @brief Count lines, words, chars, bytes, and max line length in a file
 *
 * Hot paths mirror GNU wc.c: byte-only and line-only shapes avoid UTF-8
 * decoding, word-state tracking, and display-width work.
 */
auto make_empty_result(std::string filename, bool display_filename)
    -> CountResult {
  CountResult result;
  result.filename = std::move(filename);
  result.display_filename = display_filename;
  return result;
}

auto count_bytes_stream(std::istream& input, std::string filename,
                        bool display_filename) -> cp::Result<CountResult> {
  CountResult result = make_empty_result(std::move(filename), display_filename);
  std::array<char, wc_constants::READ_BUFFER_SIZE> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize bytes_read = input.gcount();
    if (bytes_read <= 0) break;
    result.bytes += static_cast<std::uintmax_t>(bytes_read);
  }
  return result;
}

auto count_lines_bytes_stream(std::istream& input, std::string filename,
                              bool display_filename)
    -> cp::Result<CountResult> {
  CountResult result = make_empty_result(std::move(filename), display_filename);
  std::array<char, wc_constants::READ_BUFFER_SIZE> buffer{};
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize bytes_read = input.gcount();
    if (bytes_read <= 0) break;
    result.bytes += static_cast<std::uintmax_t>(bytes_read);
    const char* cur = buffer.data();
    const char* end_ptr = cur + bytes_read;
    while (cur < end_ptr) {
      const void* found =
          std::memchr(cur, '\n', static_cast<size_t>(end_ptr - cur));
      if (found == nullptr) break;
      ++result.lines;
      cur = static_cast<const char*>(found) + 1;
    }
  }
  return result;
}

auto count_stream(std::istream& input, std::string filename,
                  bool display_filename, const CountRequest& request)
    -> cp::Result<CountResult> {
  if (request.only_bytes()) {
    return count_bytes_stream(input, std::move(filename), display_filename);
  }
  if (request.lines_and_optional_bytes_only()) {
    return count_lines_bytes_stream(input, std::move(filename),
                                    display_filename);
  }

  CountResult result = make_empty_result(std::move(filename), display_filename);
  std::uintmax_t current_line_length = 0;
  bool in_word = false;
  std::array<char, wc_constants::READ_BUFFER_SIZE> buffer{};
  std::string carry;
  std::string combined;
  auto reset_word = [&]() {
    if (request.words) in_word = false;
  };
  auto finish_display_line = [&]() {
    if (!request.max_line_length) return;
    if (current_line_length > result.max_line_length) {
      result.max_line_length = current_line_length;
    }
    current_line_length = 0;
  };
  auto consume_codepoint = [&](char32_t cp) {
    if (request.chars) ++result.chars;
    switch (cp) {
      case U'\n':
        if (request.lines) ++result.lines;
        finish_display_line();
        reset_word();
        break;
      case U'\r':
      case U'\f':
        finish_display_line();
        reset_word();
        break;
      case U'\t':
        if (request.max_line_length) {
          current_line_length += 8 - (current_line_length % 8);
        }
        reset_word();
        break;
      case U' ':
        if (request.max_line_length) ++current_line_length;
        reset_word();
        break;
      case U'\v':
        reset_word();
        break;
      default:
        if (is_ascii_space(cp)) {
          if (request.max_line_length) current_line_length += display_width(cp);
          reset_word();
          break;
        }
        if (request.max_line_length) current_line_length += display_width(cp);
        if (request.words && !in_word) {
          ++result.words;
          in_word = true;
        }
        break;
    }
  };
  auto process_buffer = [&](std::string_view data, bool final_chunk) {
    size_t i = 0;
    while (i < data.size()) {
      const unsigned char first = static_cast<unsigned char>(data[i]);
      if (first < 0x80) {
        consume_codepoint(first);
        ++i;
        continue;
      }
      const size_t expected = utf8_expected_bytes(first);
      if (!final_chunk && expected > data.size() - i) {
        carry.assign(data.substr(i));
        return;
      }
      DecodedChar decoded = decode_utf8_char(data, i);
      consume_codepoint(decoded.codepoint);
      i += decoded.bytes;
    }
    carry.clear();
  };
  while (input) {
    input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
    const std::streamsize bytes_read = input.gcount();
    if (bytes_read <= 0) break;
    result.bytes += static_cast<std::uintmax_t>(bytes_read);
    const std::string_view chunk(buffer.data(),
                                 static_cast<size_t>(bytes_read));
    if (carry.empty()) {
      process_buffer(chunk, false);
    } else {
      combined.assign(carry);
      combined.append(chunk);
      carry.clear();
      process_buffer(combined, false);
    }
  }
  if (!carry.empty()) {
    combined.assign(carry);
    carry.clear();
    process_buffer(combined, true);
  }
  finish_display_line();

  return result;
}

auto count_stdin(bool explicit_stdin, const CountRequest& request)
    -> cp::Result<CountResult> {
  return count_stream(std::cin, "-", explicit_stdin, request);
}

auto count_file(const std::string& path, const CountRequest& request)
    -> cp::Result<CountResult> {
  if (path == "-") {
    return count_stdin(true, request);
  }

  std::ifstream file(native_path::normalize_api_operand(path),
                     std::ios::binary);
  if (!file) {
    return std::unexpected(wc_input_open_error(path));
  }

  if (request.only_bytes()) {
    std::error_code ec;
    const auto size =
        std::filesystem::file_size(std::filesystem::u8path(path), ec);
    if (!ec) {
      CountResult result = make_empty_result(path, true);
      result.bytes = static_cast<std::uintmax_t>(size);
      return result;
    }
  }

  return count_stream(file, path, true, request);
}

auto decimal_digits(std::uintmax_t value) -> size_t {
  size_t digits = 1;
  while (value >= 10) {
    value /= 10;
    ++digits;
  }
  return digits;
}

auto decimal_string(std::uintmax_t value) -> std::string {
  std::array<char, 64> buf{};
  auto [end, ec] = std::to_chars(buf.data(), buf.data() + buf.size(), value);
  if (ec != std::errc{}) return std::to_string(value);
  return std::string(buf.data(), end);
}

auto compute_number_width(const CountBatch& batch,
                          const std::vector<CountResult>& results,
                          size_t selected_count, bool total_only) -> size_t {
  if (total_only || batch.requested_input_count == 0 ||
      (batch.requested_input_count == 1 && selected_count == 1) ||
      results.empty()) {
    return 1;
  }

  std::uintmax_t regular_total = 0;
  bool saw_non_regular = false;

  for (const auto& result : results) {
    if (!result.display_filename || result.filename == "-") {
      saw_non_regular = true;
      continue;
    }

    const auto path = std::filesystem::u8path(result.filename);
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::is_regular_file(status)) {
      saw_non_regular = true;
      continue;
    }

    const auto size = std::filesystem::file_size(path, ec);
    if (ec) {
      saw_non_regular = true;
      continue;
    }

    const auto remaining =
        std::numeric_limits<std::uintmax_t>::max() - regular_total;
    if (size > remaining) {
      regular_total = std::numeric_limits<std::uintmax_t>::max();
    } else {
      regular_total += size;
    }
  }

  size_t width = decimal_digits(regular_total);
  if (saw_non_regular) width = std::max<size_t>(width, 7);
  return width;
}

void append_aligned_count(std::string& line, std::uintmax_t value, size_t width,
                          bool& first_field) {
  const auto text = decimal_string(value);
  if (!first_field) line.push_back(' ');
  if (text.size() < width) line.append(width - text.size(), ' ');
  line.append(text);
  first_field = false;
}

// ----------------------------------------------
// 4. Count stdin contents
// ----------------------------------------------
/**
 * @brief Count lines, words, chars, bytes, and max line length from stdin
 *
 * This function reads from standard input and counts the number of lines,
 * words, characters, bytes, and the maximum line length.
 *
 * @return A Result containing the count result
 */
// ----------------------------------------------
// 5. Main pipeline
// ----------------------------------------------
/**
 * @brief Main command processing pipeline
 *
 * This function implements the main processing pipeline for the wc command.
 * It processes the command context, validates arguments, and counts file
 * contents.
 *
 * @tparam N Number of options in the command context
 * @param ctx Command context containing options and arguments
 * @return A Result containing the list of count results
 */
template <size_t N>
auto process_command(const CommandContext<N>& ctx, const CountRequest& request)
    -> cp::Result<CountBatch> {
  std::vector<std::string> paths;
  bool files0_from_stdin = false;
  const std::string files0_from =
      ctx.template get<std::string>("--files0-from", "");
  if (!files0_from.empty()) {
    if (!ctx.positionals.empty()) {
      return std::unexpected(
          "--files0-from disallows processing files named on the command line");
    }
    auto file_list = read_files0_from(files0_from);
    if (!file_list) return std::unexpected(file_list.error());
    files0_from_stdin = file_list->from_stdin;
    paths = std::move(file_list->paths);
  } else {
    auto validated = validate_arguments(ctx.positionals);
    if (!validated) return std::unexpected(validated.error());
    paths = std::move(*validated);
  }

  CountBatch batch;
  if (paths.empty() && files0_from.empty()) {
    auto stdin_result = count_stdin(false, request);
    if (!stdin_result) return std::unexpected(stdin_result.error());
    batch.results.push_back(*stdin_result);
    batch.requested_input_count = 1;
    return batch;
  }

  if (paths.empty() && !files0_from.empty()) {
    batch.any_error = true;
    return batch;
  }

  batch.requested_input_count = paths.size();
  for (const auto& path : paths) {
    auto file_result = count_file(path, request);
    if (!file_result) {
      safeErrorPrint("wc: ");
      safeErrorPrint(file_result.error());
      safeErrorPrint("\n");
      batch.any_error = true;
      continue;
    }
    batch.results.push_back(*file_result);
  }

  return batch;
}

}  // namespace wc_pipeline

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    wc,
    /* name */
    "wc",

    /* synopsis */
    "wc [OPTION]... [FILE]...",

    /* description */
    "Print newline, word, and byte counts for each FILE, and a total line if\n"
    "more than one FILE is specified.  A word is a non-zero-length sequence "
    "of\n"
    "printable characters delimited by white space.\n"
    "\n"
    "With no FILE, or when FILE is -, read standard input.\n"
    "\n"
    "The options below may be used to select which counts are printed, always "
    "in\n"
    "the following order: newline, word, character, byte, maximum line "
    "length.\n"
    "  -c, --bytes            print the byte counts\n"
    "  -m, --chars            print the character counts\n"
    "  -l, --lines            print the newline counts\n"
    "      --files0-from=F    read input from the files specified by\n"
    "                           NUL-terminated names in file F;\n"
    "                           If F is - then read names from standard input\n"
    "  -L, --max-line-length  print the maximum display width\n"
    "  -w, --words            print the word counts\n"
    "      --total=WHEN       when to print a line with total counts;\n"
    "                           WHEN can be: auto, always, only, never\n"
    "      --help        display this help and exit\n"
    "      --version     output version information and exit",

    /* examples */
    "  wc file.txt           # Count lines, words, and bytes in file.txt\n"
    "  wc -l file.txt        # Count only lines in file.txt\n"
    "  wc -w file.txt        # Count only words in file.txt\n"
    "  wc -c file.txt        # Count only bytes in file.txt\n"
    "  wc -m file.txt        # Count only characters in file.txt\n"
    "  wc -L file.txt        # Print maximum line length in file.txt\n"
    "  wc file1.txt file2.txt # Count multiple files and show total",

    /* see_also */
    "cat(1), grep(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    WC_OPTIONS) {
  using namespace wc_pipeline;

#ifdef _WIN32
  // [GNU] stdin must be counted in binary mode: redirected Windows stdin in
  // text mode treats 0x1A (Ctrl-Z) as EOF and silently truncates the count
  // (e.g. `wc -c < random.bin`). cat.cpp does the same at its entry.
  _setmode(_fileno(stdin), _O_BINARY);
#endif

  // Determine which counts to print
  bool print_lines =
      ctx.get<bool>("--lines", false) || ctx.get<bool>("-l", false);
  bool print_words =
      ctx.get<bool>("--words", false) || ctx.get<bool>("-w", false);
  bool print_chars =
      ctx.get<bool>("--chars", false) || ctx.get<bool>("-m", false);
  bool print_bytes =
      ctx.get<bool>("--bytes", false) || ctx.get<bool>("-c", false);
  bool print_max_line_length =
      ctx.get<bool>("--max-line-length", false) || ctx.get<bool>("-L", false);

  // If no options specified, print lines, words, and bytes
  if (!print_lines && !print_words && !print_chars && !print_bytes &&
      !print_max_line_length) {
    print_lines = true;
    print_words = true;
    print_bytes = true;
  }

  CountRequest request{print_lines, print_words, print_chars, print_bytes,
                       print_max_line_length};

  auto result = process_command(ctx, request);
  if (!result) {
    cp::report_error(result, L"wc");
    return 1;
  }

  auto batch = *result;
  auto count_results = batch.results;

  if (ctx.get<bool>("--debug", false)) {
    const char* path = request.only_bytes() ? "byte-count fast path"
                       : request.lines_and_optional_bytes_only()
                           ? "line-count byte scan"
                           : "multicolumn utf-8 scanner";
    safeErrorPrint("wc: debug: line count implementation: ");
    safeErrorPrint(path);
    safeErrorPrint("\n");
  }

  // Determine when to print total
  std::string total_when = ctx.get<std::string>("--total", "auto");
  bool print_total = false;

  if (total_when == "always") {
    print_total = true;
  } else if (total_when == "never") {
    print_total = false;
  } else if (total_when == "only") {
    print_total = true;
  } else if (total_when == "auto") {
    print_total = batch.requested_input_count > 1;
  } else {
    safeErrorPrint("wc: invalid --total value '");
    safeErrorPrint(total_when);
    safeErrorPrint("'\n");
    return 1;
  }

  // Calculate total
  CountResult total_result;
  total_result.filename = "total";

  for (const auto& result : count_results) {
    total_result.lines += result.lines;
    total_result.words += result.words;
    total_result.chars += result.chars;
    total_result.bytes += result.bytes;
    if (result.max_line_length > total_result.max_line_length) {
      total_result.max_line_length = result.max_line_length;
    }
  }

  const size_t selected_count =
      static_cast<size_t>(print_lines) + static_cast<size_t>(print_words) +
      static_cast<size_t>(print_chars) + static_cast<size_t>(print_bytes) +
      static_cast<size_t>(print_max_line_length);
  const size_t number_width = compute_number_width(
      batch, count_results, selected_count, total_when == "only");

  // Print results
  auto print_result = [&](const CountResult& result, bool show_filename) {
    std::string line;
    bool first_field = true;

    if (print_lines) {
      append_aligned_count(line, result.lines, number_width, first_field);
    }
    if (print_words) {
      append_aligned_count(line, result.words, number_width, first_field);
    }
    if (print_chars) {
      append_aligned_count(line, result.chars, number_width, first_field);
    }
    if (print_bytes) {
      append_aligned_count(line, result.bytes, number_width, first_field);
    }
    if (print_max_line_length) {
      append_aligned_count(line, result.max_line_length, number_width,
                           first_field);
    }

    if (show_filename) {
      if (!line.empty()) line.push_back(' ');
      line.append(result.filename);
    }

    safePrintLn(line);
  };

  if (total_when == "only") {
    print_result(total_result, false);
  } else {
    for (const auto& result : count_results) {
      print_result(result, result.display_filename);
    }

    if (print_total) {
      print_result(total_result, true);
    }
  }

  return batch.any_error ? 1 : 0;
}
