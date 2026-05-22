/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: printf.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for printf command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include <cerrno>
#include <cstdio>

#include "core/command_macros.h"
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PRINTF_OPTIONS =
    std::array{OPTION("", "", "format and print data", STRING_TYPE)};

namespace {

struct EscapeResult {
  std::string text;
  bool stop_output = false;
};

struct FormatSpec {
  std::string flags;
  std::string width;
  bool has_precision = false;
  std::string precision;
  char conversion = '\0';
  bool consumes_argument = false;
  bool valid = false;
  bool left_adjust = false;
};

struct RenderResult {
  std::string text;
  bool stop_output = false;
  bool consumed_argument = false;
  bool had_argument_directive = false;
};

int hex_value(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool is_octal_digit(char c) { return c >= '0' && c <= '7'; }

void append_utf8(std::string& out, unsigned int codepoint) {
  if (codepoint <= 0x7F) {
    out += static_cast<char>(codepoint);
  } else if (codepoint <= 0x7FF) {
    out += static_cast<char>(0xC0 | (codepoint >> 6));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0xFFFF) {
    out += static_cast<char>(0xE0 | (codepoint >> 12));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  } else if (codepoint <= 0x10FFFF) {
    out += static_cast<char>(0xF0 | (codepoint >> 18));
    out += static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F));
    out += static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F));
    out += static_cast<char>(0x80 | (codepoint & 0x3F));
  }
}

EscapeResult interpret_escape_at(std::string_view input, size_t& i,
                                 bool percent_b_mode) {
  EscapeResult result;

  if (input[i] != '\\') {
    result.text += input[i];
    return result;
  }

  if (i + 1 >= input.size()) {
    result.text += '\\';
    return result;
  }

  char esc = input[++i];
  switch (esc) {
    case 'a':
      result.text += '\a';
      break;
    case 'b':
      result.text += '\b';
      break;
    case 'c':
      result.stop_output = true;
      return result;
    case 'e':
    case 'E':
      result.text += '\x1B';
      break;
    case 'f':
      result.text += '\f';
      break;
    case 'n':
      result.text += '\n';
      break;
    case 'r':
      result.text += '\r';
      break;
    case 't':
      result.text += '\t';
      break;
    case 'v':
      result.text += '\v';
      break;
    case '\\':
      result.text += '\\';
      break;
    case '\'':
      result.text += '\'';
      break;
    case '"':
      result.text += '"';
      break;
    case '?':
      result.text += '?';
      break;
    case 'x': {
      unsigned int value = 0;
      int digits = 0;
      while (i + 1 < input.size() && digits < 2 &&
             hex_value(input[i + 1]) >= 0) {
        value = (value * 16) + static_cast<unsigned int>(hex_value(input[++i]));
        ++digits;
      }
      result.text += digits == 0 ? 'x' : static_cast<char>(value & 0xFF);
      break;
    }
    case 'u':
    case 'U': {
      int wanted = esc == 'u' ? 4 : 8;
      unsigned int value = 0;
      int digits = 0;
      while (i + 1 < input.size() && digits < wanted &&
             hex_value(input[i + 1]) >= 0) {
        value = (value * 16) + static_cast<unsigned int>(hex_value(input[++i]));
        ++digits;
      }
      if (digits == wanted && !(value >= 0xD800 && value <= 0xDFFF)) {
        append_utf8(result.text, value);
      } else {
        result.text += '\\';
        result.text += esc;
      }
      break;
    }
    default:
      if (is_octal_digit(esc) && (!percent_b_mode || esc == '0')) {
        unsigned int value =
            percent_b_mode ? 0 : static_cast<unsigned int>(esc - '0');
        int digits = percent_b_mode ? 0 : 1;
        while (i + 1 < input.size() && digits < 3 &&
               is_octal_digit(input[i + 1])) {
          value = (value * 8) + static_cast<unsigned int>(input[++i] - '0');
          ++digits;
        }
        result.text += static_cast<char>(value & 0xFF);
      } else {
        result.text += esc;
      }
      break;
  }

  return result;
}

EscapeResult interpret_escapes(std::string_view input, bool percent_b_mode) {
  EscapeResult result;

  for (size_t i = 0; i < input.size(); ++i) {
    if (input[i] != '\\') {
      result.text += input[i];
      continue;
    }

    auto escaped = interpret_escape_at(input, i, percent_b_mode);
    result.text += escaped.text;
    if (escaped.stop_output) {
      result.stop_output = true;
      return result;
    }
  }

  return result;
}

FormatSpec parse_format_spec(std::string_view format, size_t& pos) {
  FormatSpec spec;
  size_t i = pos + 1;

  while (i < format.size()) {
    char c = format[i];
    if (c == '-' || c == '+' || c == ' ' || c == '#' || c == '0') {
      spec.flags += c;
      if (c == '-') spec.left_adjust = true;
      ++i;
    } else if (c == '\'') {
      ++i;
    } else {
      break;
    }
  }

  while (i < format.size() &&
         std::isdigit(static_cast<unsigned char>(format[i]))) {
    spec.width += format[i++];
  }

  if (i < format.size() && format[i] == '.') {
    spec.has_precision = true;
    ++i;
    while (i < format.size() &&
           std::isdigit(static_cast<unsigned char>(format[i]))) {
      spec.precision += format[i++];
    }
  }

  while (i < format.size() &&
         (format[i] == 'h' || format[i] == 'l' || format[i] == 'j' ||
          format[i] == 'z' || format[i] == 't' || format[i] == 'L')) {
    ++i;
  }

  if (i >= format.size()) {
    pos = format.size() - 1;
    return spec;
  }

  spec.conversion = format[i];
  spec.valid = true;
  pos = i;
  spec.consumes_argument =
      std::string_view("bcdiuoxXfFeEgGaAs").find(spec.conversion) !=
      std::string_view::npos;
  return spec;
}

std::string normalized_format(const FormatSpec& spec, char conversion,
                              std::string_view length = {}) {
  std::string fmt = "%";
  fmt += spec.flags;
  fmt += spec.width;
  if (spec.has_precision) {
    fmt += ".";
    fmt += spec.precision;
  }
  fmt += length;
  fmt += conversion;
  return fmt;
}

template <typename... Args>
std::string format_with_snprintf(const std::string& fmt, Args... args) {
  int size = std::snprintf(nullptr, 0, fmt.c_str(), args...);
  if (size <= 0) return {};

  std::string output(static_cast<size_t>(size) + 1, '\0');
  std::snprintf(output.data(), output.size(), fmt.c_str(), args...);
  output.resize(static_cast<size_t>(size));
  return output;
}

size_t parse_count(std::string_view text, size_t fallback) {
  if (text.empty()) return fallback;

  size_t value = 0;
  for (char c : text) {
    if (!std::isdigit(static_cast<unsigned char>(c))) return fallback;
    size_t digit = static_cast<size_t>(c - '0');
    if (value > (std::numeric_limits<size_t>::max() - digit) / 10) {
      return fallback;
    }
    value = (value * 10) + digit;
  }
  return value;
}

std::string format_string_bytes(std::string value, const FormatSpec& spec) {
  if (spec.has_precision) {
    value.resize(std::min(value.size(), parse_count(spec.precision, 0)));
  }

  size_t width = parse_count(spec.width, 0);
  if (width <= value.size()) return value;

  std::string padding(width - value.size(), ' ');
  if (spec.left_adjust) return value + padding;
  return padding + value;
}

std::string consume_string_argument(const std::vector<std::string_view>& args,
                                    size_t& arg_index) {
  if (arg_index >= args.size()) return {};
  return std::string(args[arg_index++]);
}

void warn_numeric(std::string_view arg, std::string_view message,
                  bool& had_error) {
  safeErrorPrintLn("printf: '" + std::string(arg) +
                   "': " + std::string(message));
  had_error = true;
}

bool consume_character_constant(std::string_view arg, long long& value,
                                bool& had_error) {
  if (arg.empty() || (arg[0] != '\'' && arg[0] != '"')) return false;

  if (arg.size() < 2) {
    warn_numeric(arg, "expected a numeric value", had_error);
    value = 0;
    return true;
  }

  value = static_cast<unsigned char>(arg[1]);
  if (arg.size() > 2) {
    warn_numeric(arg,
                 "character(s) following character constant have been ignored",
                 had_error);
  }
  return true;
}

long long parse_signed_argument(const std::vector<std::string_view>& args,
                                size_t& arg_index, bool& had_error) {
  if (arg_index >= args.size()) return 0;

  std::string arg(args[arg_index++]);
  long long character_value = 0;
  if (consume_character_constant(arg, character_value, had_error)) {
    return character_value;
  }

  errno = 0;
  char* end = nullptr;
  long long value = std::strtoll(arg.c_str(), &end, 0);
  if (end == arg.c_str()) {
    warn_numeric(arg, "expected a numeric value", had_error);
    return 0;
  }
  if (errno == ERANGE) {
    warn_numeric(arg, "numerical result out of range", had_error);
  } else if (*end != '\0') {
    warn_numeric(arg, "value not completely converted", had_error);
  }
  return value;
}

unsigned long long parse_unsigned_argument(
    const std::vector<std::string_view>& args, size_t& arg_index,
    bool& had_error) {
  if (arg_index >= args.size()) return 0;

  std::string arg(args[arg_index++]);
  long long character_value = 0;
  if (consume_character_constant(arg, character_value, had_error)) {
    return static_cast<unsigned long long>(character_value);
  }

  errno = 0;
  char* end = nullptr;
  unsigned long long value = std::strtoull(arg.c_str(), &end, 0);
  if (end == arg.c_str()) {
    warn_numeric(arg, "expected a numeric value", had_error);
    return 0;
  }
  if (errno == ERANGE) {
    warn_numeric(arg, "numerical result out of range", had_error);
  } else if (*end != '\0') {
    warn_numeric(arg, "value not completely converted", had_error);
  }
  return value;
}

double parse_float_argument(const std::vector<std::string_view>& args,
                            size_t& arg_index, bool& had_error) {
  if (arg_index >= args.size()) return 0.0;

  std::string arg(args[arg_index++]);
  long long character_value = 0;
  if (consume_character_constant(arg, character_value, had_error)) {
    return static_cast<double>(character_value);
  }

  errno = 0;
  char* end = nullptr;
  double value = std::strtod(arg.c_str(), &end);
  if (end == arg.c_str()) {
    warn_numeric(arg, "expected a numeric value", had_error);
    return 0.0;
  }
  if (errno == ERANGE) {
    warn_numeric(arg, "numerical result out of range", had_error);
  } else if (*end != '\0') {
    warn_numeric(arg, "value not completely converted", had_error);
  }
  return value;
}

std::string render_directive(const FormatSpec& spec,
                             const std::vector<std::string_view>& args,
                             size_t& arg_index, bool& had_error,
                             bool& stop_output) {
  switch (spec.conversion) {
    case '%':
      return "%";
    case 's':
      return format_string_bytes(consume_string_argument(args, arg_index),
                                 spec);
    case 'b': {
      auto escaped =
          interpret_escapes(consume_string_argument(args, arg_index), true);
      stop_output = escaped.stop_output;
      return format_string_bytes(std::move(escaped.text), spec);
    }
    case 'c': {
      std::string arg = consume_string_argument(args, arg_index);
      int ch = arg.empty() ? 0 : static_cast<unsigned char>(arg[0]);
      return format_with_snprintf(normalized_format(spec, 'c'), ch);
    }
    case 'd':
    case 'i':
      return format_with_snprintf(
          normalized_format(spec, spec.conversion, "ll"),
          parse_signed_argument(args, arg_index, had_error));
    case 'u':
    case 'o':
    case 'x':
    case 'X':
      return format_with_snprintf(
          normalized_format(spec, spec.conversion, "ll"),
          parse_unsigned_argument(args, arg_index, had_error));
    case 'f':
    case 'F':
    case 'e':
    case 'E':
    case 'g':
    case 'G':
    case 'a':
    case 'A':
      return format_with_snprintf(
          normalized_format(spec, spec.conversion),
          parse_float_argument(args, arg_index, had_error));
    default:
      return "%" + std::string(1, spec.conversion);
  }
}

RenderResult render_once(std::string_view format,
                         const std::vector<std::string_view>& args,
                         size_t& arg_index, bool& had_error) {
  RenderResult result;

  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] == '\\') {
      auto escaped = interpret_escape_at(format, i, false);
      result.text += escaped.text;
      if (escaped.stop_output) {
        result.stop_output = true;
        return result;
      }
      continue;
    }

    if (format[i] != '%') {
      result.text += format[i];
      continue;
    }

    FormatSpec spec = parse_format_spec(format, i);
    if (!spec.valid) {
      result.text += '%';
      continue;
    }

    if (spec.consumes_argument) {
      result.had_argument_directive = true;
      if (arg_index < args.size()) result.consumed_argument = true;
    }

    bool directive_stopped = false;
    result.text +=
        render_directive(spec, args, arg_index, had_error, directive_stopped);
    if (directive_stopped) {
      result.stop_output = true;
      return result;
    }
  }

  return result;
}

}  // namespace

REGISTER_COMMAND(printf_cmd,
                 /* name */
                 "printf",

                 /* synopsis */
                 "printf FORMAT [ARGUMENT]...",
                 "Format and print data.\n"
                 "\n"
                 "Print ARGUMENT(s) according to FORMAT.\n"
                 "\n"
                 "Supported format specifiers:\n"
                 "  %%      Literal '%'\n"
                 "  %c      Character\n"
                 "  %s      String\n"
                 "  %d, %i  Integer\n"
                 "  %u      Unsigned integer\n"
                 "  %x, %X  Hexadecimal (lowercase/uppercase)\n"
                 "  %o      Octal\n"
                 "  %f      Floating point\n"
                 "  %e, %E  Scientific notation\n"
                 "  %g, %G  %f or %e depending on value\n"
                 "  %b      Interpret backslash escapes (like echo -e)\n",
                 "  printf 'Hello %s!\\n' 'World'\n"
                 "  printf 'Decimal: %d, Hex: %x\\n' 255 255\n"
                 "  printf 'Value: %.2f\\n' 3.14159",

                 /* see also */
                 "echo(1), sprintf(3)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 PRINTF_OPTIONS) {
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("printf: missing format argument");
    return 1;
  }

  std::string_view format = ctx.positionals[0];
  std::string result;
  size_t arg_index = 1;
  bool had_error = false;

  do {
    auto rendered = render_once(format, ctx.positionals, arg_index, had_error);
    result += rendered.text;
    if (rendered.stop_output || !rendered.had_argument_directive ||
        !rendered.consumed_argument) {
      break;
    }
  } while (arg_index < ctx.positionals.size());

  safePrint(std::string_view(result));

  return had_error ? 1 : 0;
}
