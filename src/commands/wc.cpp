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
// Add constants here
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
    OPTION("-c", "--bytes", "print the byte counts"),
    OPTION("-m", "--chars", "print the character counts"),
    OPTION("-l", "--lines", "print the newline counts"),
    OPTION("", "--debug", "print line-count implementation diagnostics"),
    OPTION(
        "", "--files0-from",
        "read input from the files specified by NUL-terminated names in file F",
        STRING_TYPE),
    OPTION("-L", "--max-line-length", "print the maximum display width"),
    OPTION("-w", "--words", "print the word counts"),
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
};

struct DecodedChar {
  char32_t codepoint = 0;
  size_t bytes = 1;
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

auto read_files0_from(const std::string& path)
    -> cp::Result<std::vector<std::string>> {
  std::istream* input = nullptr;
  std::ifstream file;
  if (path == "-") {
    input = &std::cin;
  } else {
    file.open(path, std::ios::binary);
    if (!file) {
      return std::unexpected("cannot open file list '" + path + "'");
    }
    input = &file;
  }

  std::vector<std::string> paths;
  std::string name;
  while (std::getline(*input, name, '\0')) {
    if (!name.empty()) {
      paths.push_back(name);
    }
  }
  return paths;
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

/**
 * @brief Count lines, words, chars, bytes, and max line length in a file

 * *
 * This function reads a file and counts the number of lines, words,
 * characters, bytes, and the maximum line length.
 *
 * @param path Path to the file to count
 * @return A Result containing the count result
 */
auto count_stream(std::istream& input, std::string filename,
                  bool display_filename) -> cp::Result<CountResult> {
  CountResult result;
  result.filename = std::move(filename);
  result.display_filename = display_filename;

  std::string data((std::istreambuf_iterator<char>(input)),
                   std::istreambuf_iterator<char>());
  result.bytes = data.size();

  std::uintmax_t current_line_length = 0;
  bool in_word = false;

  for (size_t i = 0; i < data.size();) {
    DecodedChar decoded = decode_utf8_char(data, i);
    i += decoded.bytes;
    result.chars++;
    char32_t cp = decoded.codepoint;

    if (cp == U'\n') {
      result.lines++;
      if (current_line_length > result.max_line_length) {
        result.max_line_length = current_line_length;
      }
      current_line_length = 0;
      in_word = false;
    } else if (cp == U'\t') {
      current_line_length += 8 - (current_line_length % 8);
      in_word = false;
    } else if (is_ascii_space(cp)) {
      current_line_length += display_width(cp);
      in_word = false;
    } else {
      current_line_length += display_width(cp);
      if (!in_word) {
        result.words++;
        in_word = true;
      }
    }
  }

  if (current_line_length > result.max_line_length) {
    result.max_line_length = current_line_length;
  }

  return result;
}

auto count_stdin(bool explicit_stdin) -> cp::Result<CountResult> {
  return count_stream(std::cin, "-", explicit_stdin);
}

auto count_file(const std::string& path) -> cp::Result<CountResult> {
  if (path == "-") {
    return count_stdin(true);
  }

  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return std::unexpected("cannot open file '" + path + "'");
  }

  return count_stream(file, path, true);
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
auto process_command(const CommandContext<N>& ctx) -> cp::Result<CountBatch> {
  std::vector<std::string> paths;
  const std::string files0_from =
      ctx.template get<std::string>("--files0-from", "");
  if (!files0_from.empty()) {
    if (!ctx.positionals.empty()) {
      return std::unexpected(
          "--files0-from disallows processing files named on the command line");
    }
    auto file_list = read_files0_from(files0_from);
    if (!file_list) return std::unexpected(file_list.error());
    paths = std::move(*file_list);
  } else {
    auto validated = validate_arguments(ctx.positionals);
    if (!validated) return std::unexpected(validated.error());
    paths = std::move(*validated);
  }

  CountBatch batch;
  if (paths.empty() && files0_from.empty()) {
    auto stdin_result = count_stdin(false);
    if (!stdin_result) return std::unexpected(stdin_result.error());
    batch.results.push_back(*stdin_result);
    return batch;
  }

  for (const auto& path : paths) {
    auto file_result = count_file(path);
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

  auto result = process_command(ctx);
  if (!result) {
    cp::report_error(result, L"wc");
    return 1;
  }

  auto batch = *result;
  auto count_results = batch.results;

  if (ctx.get<bool>("--debug", false)) {
    safeErrorPrint(
        "wc: debug: line count implementation: portable byte scan\n");
  }

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
    print_total = count_results.size() > 1;
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

  // Print results
  auto print_result = [&](const CountResult& result, bool show_filename) {
    // OPTIMIZED: Use snprintf instead of to_wstring and avoid wstring
    // concatenation
    char buf[256];
    int offset = 0;

    if (print_lines) {
      int len =
          snprintf(buf + offset, sizeof(buf) - offset, "%ju ", result.lines);
      offset += len;
    }
    if (print_words) {
      int len =
          snprintf(buf + offset, sizeof(buf) - offset, "%ju ", result.words);
      offset += len;
    }
    if (print_chars) {
      int len =
          snprintf(buf + offset, sizeof(buf) - offset, "%ju ", result.chars);
      offset += len;
    }
    if (print_bytes) {
      int len =
          snprintf(buf + offset, sizeof(buf) - offset, "%ju ", result.bytes);
      offset += len;
    }
    if (print_max_line_length) {
      int len = snprintf(buf + offset, sizeof(buf) - offset, "%ju ",
                         result.max_line_length);
      offset += len;
    }

    // Remove trailing space
    if (offset > 0 && buf[offset - 1] == ' ') {
      buf[offset - 1] = '\0';
      offset--;
    }

    if (show_filename) {
      if (offset > 0) {
        buf[offset++] = ' ';
      }
      // Copy filename (assuming it fits in remaining buffer)
      size_t filename_len = result.filename.length();
      if (offset + filename_len < sizeof(buf)) {
        memcpy(buf + offset, result.filename.c_str(), filename_len);
        offset += filename_len;
      }
      buf[offset] = '\0';
    }

    safePrintLn(std::string_view(buf, offset));
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
