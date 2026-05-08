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
#include "core/command_macros.h"
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr PRINTF_OPTIONS =
    std::array{OPTION("", "", "format and print data", STRING_TYPE)};

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

  auto format = std::string(ctx.positionals[0]);
  std::string result;
  size_t arg_index = 1;

  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] == '%' && i + 1 < format.size()) {
      ++i;
      char type = format[i];

      // Handle precision/width flags (basic support)
      while (i + 1 < format.size() &&
             (format[i + 1] == '-' || format[i + 1] == '+' ||
              format[i + 1] == ' ' || format[i + 1] == '0' ||
              format[i + 1] == '#' ||
              std::isdigit(static_cast<unsigned char>(format[i + 1])))) {
        ++i;
      }
      type = format[i];

      if (type == '%') {
        result += '%';
      } else if (type == 's') {
        if (arg_index < ctx.positionals.size()) {
          result += ctx.positionals[arg_index++];
        }
      } else if (type == 'c') {
        if (arg_index < ctx.positionals.size()) {
          auto arg = ctx.positionals[arg_index++];
          if (!arg.empty()) {
            result += arg[0];
          }
        }
      } else if (type == 'd' || type == 'i') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stoll(std::string(ctx.positionals[arg_index++]));
            result += std::to_string(val);
          } catch (...) {
            result += "0";
          }
        }
      } else if (type == 'u') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stoull(std::string(ctx.positionals[arg_index++]));
            result += std::to_string(val);
          } catch (...) {
            result += "0";
          }
        }
      } else if (type == 'x') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stoull(std::string(ctx.positionals[arg_index++]));
            char buf[32];
            snprintf(buf, sizeof(buf), "%llx",
                     static_cast<unsigned long long>(val));
            result += buf;
          } catch (...) {
            result += "0";
          }
        }
      } else if (type == 'X') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stoull(std::string(ctx.positionals[arg_index++]));
            char buf[32];
            snprintf(buf, sizeof(buf), "%llX",
                     static_cast<unsigned long long>(val));
            result += buf;
          } catch (...) {
            result += "0";
          }
        }
      } else if (type == 'o') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stoull(std::string(ctx.positionals[arg_index++]));
            char buf[32];
            snprintf(buf, sizeof(buf), "%llo",
                     static_cast<unsigned long long>(val));
            result += buf;
          } catch (...) {
            result += "0";
          }
        }
      } else if (type == 'f') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stod(std::string(ctx.positionals[arg_index++]));
            result += std::to_string(val);
          } catch (...) {
            result += "0.0";
          }
        }
      } else if (type == 'e') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stod(std::string(ctx.positionals[arg_index++]));
            char buf[64];
            snprintf(buf, sizeof(buf), "%e", val);
            result += buf;
          } catch (...) {
            result += "0.0";
          }
        }
      } else if (type == 'E') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stod(std::string(ctx.positionals[arg_index++]));
            char buf[64];
            snprintf(buf, sizeof(buf), "%E", val);
            result += buf;
          } catch (...) {
            result += "0.0";
          }
        }
      } else if (type == 'g') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stod(std::string(ctx.positionals[arg_index++]));
            char buf[64];
            snprintf(buf, sizeof(buf), "%g", val);
            result += buf;
          } catch (...) {
            result += "0";
          }
        }
      } else if (type == 'G') {
        if (arg_index < ctx.positionals.size()) {
          try {
            auto val = std::stod(std::string(ctx.positionals[arg_index++]));
            char buf[64];
            snprintf(buf, sizeof(buf), "%G", val);
            result += buf;
          } catch (...) {
            result += "0";
          }
        }
      } else if (type == 'b') {
        // Interpret backslash escapes
        if (arg_index < ctx.positionals.size()) {
          auto arg = ctx.positionals[arg_index++];
          std::string processed;
          for (size_t j = 0; j < arg.size(); ++j) {
            if (arg[j] == '\\' && j + 1 < arg.size()) {
              ++j;
              switch (arg[j]) {
                case 'n':
                  processed += '\n';
                  break;
                case 't':
                  processed += '\t';
                  break;
                case 'r':
                  processed += '\r';
                  break;
                case '\\':
                  processed += '\\';
                  break;
                case 'a':
                  processed += '\a';
                  break;
                case 'b':
                  processed += '\b';
                  break;
                case 'f':
                  processed += '\f';
                  break;
                case 'v':
                  processed += '\v';
                  break;
                case '0': {
                  int value = 0;
                  size_t k = j + 1;
                  for (; k < j + 4 && k < arg.size() &&
                         std::isdigit(static_cast<unsigned char>(arg[k]));
                       ++k) {
                    value = value * 8 + (arg[k] - '0');
                  }
                  if (k > j + 1) {
                    processed += static_cast<char>(value);
                    j = k - 1;
                  } else {
                    processed += '\0';
                  }
                  break;
                }
                default:
                  processed += arg[j];
                  break;
              }
            } else {
              processed += arg[j];
            }
          }
          result += processed;
        }
      } else {
        // Unknown format specifier, just output it
        result += '%';
        result += type;
      }
    } else if (format[i] == '\\' && i + 1 < format.size()) {
      // Handle backslash escapes in format string
      ++i;
      switch (format[i]) {
        case 'n':
          result += '\n';
          break;
        case 't':
          result += '\t';
          break;
        case 'r':
          result += '\r';
          break;
        case '\\':
          result += '\\';
          break;
        default:
          result += format[i];
          break;
      }
    } else {
      result += format[i];
    }
  }

  safePrint(std::string_view(result));

  return 0;
}
