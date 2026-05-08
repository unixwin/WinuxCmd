/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: echo.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for echo command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Constants
// ======================================================
namespace echo_constants {
constexpr int MIN_REPEAT = 1;
constexpr int MAX_REPEAT = 100000;
}  // namespace echo_constants

// ======================================================
// Options (constexpr)
// ======================================================

/**
 * @brief ECHO command options definition
 *
 * This array defines all the options supported by the echo command.
 * Each option is described with its short form, long form, and description.
 * The implementation status is also indicated for each option.
 *
 * @par Options:
 * - @a -n: Do not append a newline [IMPLEMENTED]
 * - @a -e: Enable interpretation of backslash escapes [IMPLEMENTED]
 * - @a -E: Explicitly suppress interpretation of backslash escapes
 * [IMPLEMENTED]
 * - @a -u, @a --upper: Convert text to uppercase [IMPLEMENTED]
 * - @a -r, @a --repeat: Repeat output N times [IMPLEMENTED]
 */
auto constexpr ECHO_OPTIONS =
    std::array{OPTION("-n", "", "do not append a newline"),
               OPTION("-e", "", "enable backslash escapes"),
               OPTION("-E", "", "suppress backslash escapes"),
               OPTION("-u", "--upper", "convert text to uppercase"),
               OPTION("-r", "--repeat", "repeat output N times", INT_TYPE)};

// ======================================================
// Pipeline components
// ======================================================
namespace echo_pipeline {
namespace cp = core::pipeline;
// ----------------------------------------------
// 1. Build text
// ----------------------------------------------
/**
 * @brief Build text from command line arguments
 *
 * This function constructs a single string from the provided command line
 * arguments, separating them with spaces.
 *
 * @param args Command line arguments to build text from
 * @return A Result containing the built text, or an error if no arguments are
 * provided
 */
auto build_text(std::span<const std::string_view> args)
    -> cp::Result<std::string> {
  if (args.empty()) return std::unexpected("no arguments provided");
  std::string text;
  for (auto arg : args) {
    if (!text.empty()) text += ' ';
    text += arg;
  }
  return text;
}

// ----------------------------------------------
// 2. Uppercase transformation
// ----------------------------------------------
/**
 * @brief Convert text to uppercase
 *
 * This function converts all characters in the provided text to uppercase,
 * but only if the enabled flag is true.
 *
 * @param text Text to transform
 * @param enabled Flag indicating whether to perform uppercase transformation
 * @return A Result containing the transformed text
 */
auto to_uppercase(std::string text, bool enabled) -> cp::Result<std::string> {
  if (enabled) {
    for (char& c : text)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  }
  return text;
}

// ----------------------------------------------
// 3. Process backslash escapes
// ----------------------------------------------
/**
 * @brief Process backslash escape sequences in text
 *
 * This function processes backslash escape sequences in the provided text,
 * but only if the enabled flag is true. It supports various escape sequences
 * as defined in the Linux echo command documentation.
 *
 * @par Supported escape sequences:
 * - @a \a: Alert (bell)
 * - @a \b: Backspace
 * - @a \c: Suppress further output
 * - @a \e, @a \E: Escape character
 * - @a \f: Form feed
 * - @a \n: New line
 * - @a \r: Carriage return
 * - @a \t: Horizontal tab
 * - @a \v: Vertical tab
 * - @a \\: Backslash
 * - @a \0nnn: The character whose ASCII code is NNN (octal)
 * - @a \xHH: The eight-bit character whose value is HH (hexadecimal)
 * - @a \uHHHH: The Unicode character whose value is the hexadecimal value HHHH
 * (Not fully implemented on Windows)
 * - @a \UHHHHHHHH: The Unicode character whose value is the hexadecimal value
 * HHHHHHHH (Not fully implemented on Windows)
 *
 * @param text Text to process escape sequences in
 * @param enabled Flag indicating whether to process escape sequences
 * @return A Result containing the text with escape sequences processed
 */
auto process_escapes(std::string text, bool enabled)
    -> cp::Result<std::string> {
  if (!enabled) return text;

  std::string result;
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size()) {
      ++i;
      switch (text[i]) {
        case 'a':
          result += '\a';
          break;
        case 'b':
          result += '\b';
          break;
        case 'c':
          return result;  // Suppress further output
        case 'e':
          result += '\x1B';
          break;
        case 'E':
          result += '\x1B';
          break;
        case 'f':
          result += '\f';
          break;
        case 'n':
          result += '\n';
          break;
        case 'r':
          result += '\r';
          break;
        case 't':
          result += '\t';
          break;
        case 'v':
          result += '\v';
          break;
        case '\\':
          result += '\\';
          break;
        case '0': {
          // Octal escape \0nnn
          int value = 0;
          size_t j = i + 1;
          for (; j < i + 4 && j < text.size() &&
                 std::isdigit(static_cast<unsigned char>(text[j]));
               ++j) {
            value = value * 8 + (text[j] - '0');
          }
          if (j > i + 1) {
            result += static_cast<char>(value);
            i = j - 1;
          } else {
            result += '\0';
          }
          break;
        }
        case 'x': {
          // Hex escape \xHH
          int value = 0;
          size_t j = i + 1;
          for (; j < i + 3 && j < text.size() &&
                 (std::isdigit(static_cast<unsigned char>(text[j])) ||
                  (std::tolower(static_cast<unsigned char>(text[j])) >= 'a' &&
                   std::tolower(static_cast<unsigned char>(text[j])) <= 'f'));
               ++j) {
            char c = std::tolower(static_cast<unsigned char>(text[j]));
            value =
                value * 16 + (c >= '0' && c <= '9' ? c - '0' : c - 'a' + 10);
          }
          if (j > i + 1) {
            result += static_cast<char>(value);
            i = j - 1;
          } else {
            result += 'x';
          }
          break;
        }
        case 'u':
        case 'U': {
          // Unicode escapes - not fully implemented on Windows
          // \uHHHH and \UHHHHHHHH
          result += text[i - 1];
          result += text[i];
          break;
        }
        default:
          result += text[i];
          break;
      }
    } else {
      result += text[i];
    }
  }
  return result;
}

// ----------------------------------------------
// 4. Repeat count validation
// ----------------------------------------------
/**
 * @brief Validate repeat count
 *
 * This function validates that the repeat count is within the allowed range
 * defined by MIN_REPEAT and MAX_REPEAT constants.
 *
 * @param count Repeat count to validate
 * @return A Result containing the validated repeat count, or an error if it's
 * out of range
 */
auto validate_repeat(int count) -> cp::Result<int> {
  if (count < echo_constants::MIN_REPEAT || count > echo_constants::MAX_REPEAT)
    return std::unexpected("repeat count out of range");
  return count;
}

// ----------------------------------------------
// 5. Main pipeline
// ----------------------------------------------
/**
 * @brief Main command processing pipeline
 *
 * This function implements the main processing pipeline for the echo command.
 * It processes the command context, builds the text, applies transformations,
 * and returns the processed text along with repeat count and newline flag.
 *
 * @par Processing steps:
 * 1. Check if escape sequences should be processed (-e and -E options)
 * 2. Build text from command line arguments
 * 3. Convert text to uppercase if --upper option is enabled
 * 4. Process backslash escape sequences if -e option is enabled
 * 5. Validate repeat count
 * 6. Determine if newline should be suppressed (-n option)
 *
 * @tparam N Number of options in the command context
 * @param ctx Command context containing options and arguments
 * @return A Result containing a tuple with:
 *         - Processed text
 *         - Repeat count
 *         - Flag indicating whether to suppress newline
 */
template <size_t N>
auto process_command(const CommandContext<N>& ctx)
    -> cp::Result<std::tuple<std::string, int, bool>> {
  bool enable_escapes = ctx.get<bool>("-e", false);
  bool suppress_escapes = ctx.get<bool>("-E", false);
  // If both -e and -E are specified, -E takes precedence
  bool process_escape = enable_escapes && !suppress_escapes;

  return build_text(ctx.positionals)
      .and_then([&](std::string text) {
        return to_uppercase(std::move(text), ctx.get<bool>("--upper", false));
      })
      .and_then([&](std::string utext) {
        return process_escapes(std::move(utext), process_escape);
      })
      .and_then([&](std::string etext) {
        return validate_repeat(ctx.get<int>("--repeat", 1))
            .transform([&](int repeat) {
              bool no_newline = ctx.get<bool>("-n", false);
              return std::tuple{std::move(etext), repeat, no_newline};
            });
      });
}

}  // namespace echo_pipeline

// ======================================================
// Command registration
// ======================================================

REGISTER_COMMAND(
    echo,
    /* name */
    "echo",

    /* synopsis */
    "echo [-neE] [arg ...]",

    /* description */
    "Write arguments to the standard output.\n"
    "\n"
    "Display the ARGs, separated by a single space character and followed by "
    "a\n"
    "newline, on the standard output.\n"
    "\n"
    "Options:\n"
    "  -n        do not append a newline\n"
    "  -e        enable interpretation of backslash escapes\n"
    "  -E        explicitly suppress interpretation of backslash escapes\n"
    "\n"
    "`echo` interprets the following backslash-escaped characters:\n"
    "  \\a        alert (bell)\n"
    "  \\b        backspace\n"
    "  \\c        suppress further output\n"
    "  \\e        escape character\n"
    "  \\E        escape character\n"
    "  \\f        form feed\n"
    "  \\n        new line\n"
    "  \\r        carriage return\n"
    "  \\t        horizontal tab\n"
    "  \\v        vertical tab\n"
    "  \\\\        backslash\n"
    "  \\0nnn     the character whose ASCII code is NNN (octal).  NNN can be\n"
    "            0 to 3 octal digits\n"
    "  \\xHH      the eight-bit character whose value is HH (hexadecimal).  "
    "HH\n"
    "            can be one or two hex digits\n"
    "  \\uHHHH    the Unicode character whose value is the hexadecimal value "
    "HHHH.\n"
    "            HHHH can be one to four hex digits. (Not fully implemented on "
    "Windows)\n"
    "  \\UHHHHHHHH the Unicode character whose value is the hexadecimal value\n"
    "            HHHHHHHH. HHHHHHHH can be one to eight hex digits. (Not fully "
    "implemented on Windows)\n",

    /* examples */
    "  echo hello world\n"
    "  echo -n no newline\n"
    "  echo -e line1\\nline2\n"
    "  echo --upper hello\n"
    "  echo --repeat 3 hello",

    /* see also */
    "printf(1)",

    /* author */
    "WinuxCmd",

    /* copyright */
    "Copyright © 2026 WinuxCmd",

    /* options */
    ECHO_OPTIONS) {
  using namespace echo_pipeline;

  auto result = process_command(ctx);
  if (!result) {
    cp::report_error(result, L"echo");
    return 1;
  }

  auto [text, repeat, no_newline] = *result;

  for (int i = 0; i < repeat; ++i) {
    if (no_newline) {
      safePrint(std::string_view(text));
    } else {
      safePrintLn(std::string_view(text));
    }
  }

  return 0;
}
