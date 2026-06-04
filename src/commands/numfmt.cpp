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
 *  - File: numfmt.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for numfmt command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr NUMFMT_OPTIONS = std::array{
    OPTION("-d", "--delimiter",
           "use X instead of whitespace for field delimiter", STRING_TYPE),
    OPTION("", "--from", "autoconvert from X", STRING_TYPE),
    OPTION("", "--to", "autoconvert to X", STRING_TYPE),
    OPTION("", "--round", "use METHOD for rounding", STRING_TYPE),
    OPTION("", "--padding", "pad numbers to width N", INT_TYPE),
    OPTION("-f", "--format",
           "use printf style floating-point FORMAT", STRING_TYPE),
    OPTION("", "--header",
           "print the first N header lines unchanged", INT_TYPE),
    OPTION("", "--grouping",
           "group digits with locale thousands separator", BOOL_TYPE),
    OPTION("", "--invalid",
           "set policy for invalid values: 'abort' (default), 'warn', 'ignore'",
           STRING_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Apply rounding mode to a double value
double apply_rounding(double value, const std::string& mode) {
  if (mode.empty() || mode == "nearest") {
    // Default: round half away from zero
    return (value >= 0) ? std::floor(value + 0.5) : std::ceil(value - 0.5);
  } else if (mode == "up") {
    return std::ceil(value);
  } else if (mode == "down") {
    return std::floor(value);
  } else if (mode == "from-zero") {
    return (value >= 0) ? std::ceil(value) : std::floor(value);
  } else if (mode == "towards-zero") {
    return std::trunc(value);
  }
  // Unknown mode, default to nearest
  return (value >= 0) ? std::floor(value + 0.5) : std::ceil(value - 0.5);
}

// Parse number with SI suffixes (K, M, G, T, P)
bool parse_number(const std::string& s, long long& result,
                  const std::string& round_mode = "") {
  std::string num_str;
  char suffix = 0;

  // Extract numeric part and suffix
  for (size_t i = 0; i < s.size(); ++i) {
    if (std::isdigit(s[i]) || s[i] == '.' || s[i] == '-') {
      num_str += s[i];
    } else {
      suffix = std::toupper(s[i]);
      break;
    }
  }

  try {
    double num = std::stod(num_str);

    // Apply suffix multiplier
    switch (suffix) {
      case 'K':
        num *= 1024;
        break;
      case 'M':
        num *= 1024 * 1024;
        break;
      case 'G':
        num *= 1024 * 1024 * 1024;
        break;
      case 'T':
        num *= 1024LL * 1024 * 1024 * 1024;
        break;
      case 'P':
        num *= 1024LL * 1024 * 1024 * 1024 * 1024;
        break;
      case 0:
        break;
      default:
        return false;
    }

    // Apply rounding mode when converting from human-readable
    result = static_cast<long long>(apply_rounding(num, round_mode));
    return true;
  } catch (...) {
    return false;
  }
}

// Format number with SI suffix
std::string format_number(long long num, const std::string& unit = "") {
  const char* suffixes[] = {"", "K", "M", "G", "T", "P"};
  int suffix_index = 0;
  double value = static_cast<double>(num);

  while (value >= 1024 && suffix_index < 5) {
    value /= 1024;
    suffix_index++;
  }

  char buffer[64];
  if (suffix_index == 0) {
    sprintf_s(buffer, sizeof(buffer), "%lld", num);
  } else {
    sprintf_s(buffer, sizeof(buffer), "%.2f", value);
  }

  std::string result = buffer;
  result += suffixes[suffix_index];
  if (!unit.empty()) {
    result += unit;
  }

  return result;
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    numfmt,
    /* cmd_name */ "numfmt",
    /* cmd_synopsis */ "numfmt [OPTION]... [NUMBER]...",
    /* cmd_desc */
    "Convert numbers from/to human-readable strings.\n"
    "Convert numbers to/from human-readable strings (e.g., 1K, 1M).\n"
    "If no number is specified, read from standard input.",
    /* examples */
    "  numfmt --to=si 1024\n"
    "  echo 1M | numfmt --from=si\n"
    "  numfmt --to=iec --padding=10 1024",
    /* see_also */ "human-readable, bytesize",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ NUMFMT_OPTIONS) {
  std::string to_unit = ctx.get<std::string>("--to", "");
  bool to_si = to_unit == "si";
  bool to_iec = to_unit == "iec";
  std::string from_unit = ctx.get<std::string>("--from", "");
  std::string format_str = ctx.get<std::string>("--format", "");
  if (format_str.empty()) {
    format_str = ctx.get<std::string>("-f", "");
  }
  std::string delimiter = ctx.get<std::string>("--delimiter", "");
  if (delimiter.empty()) {
    delimiter = ctx.get<std::string>("-d", "");
  }
  int header = 0;
  auto header_str = ctx.get<std::string>("--header", "0");
  if (!header_str.empty()) {
    try { header = std::stoi(header_str); } catch (...) {}
  }
  bool grouping = ctx.get<bool>("--grouping", false);
  std::string invalid_policy = ctx.get<std::string>("--invalid", "abort");
  std::string round_mode = ctx.get<std::string>("--round", "");
  int padding = ctx.get<int>("--padding", 0);

  auto add_grouping = [](const std::string& s) -> std::string {
    // Add thousands separator commas
    bool negative = !s.empty() && s[0] == '-';
    std::string digits = negative ? s.substr(1) : s;
    std::string result;
    int count = 0;
    for (int i = static_cast<int>(digits.size()) - 1; i >= 0; --i) {
      if (count > 0 && count % 3 == 0) result = "," + result;
      result = digits[i] + result;
      ++count;
    }
    return negative ? "-" + result : result;
  };

  auto apply_format = [&](long long num) -> std::string {
    std::string result;
    if (!format_str.empty()) {
      // Simple printf-style format support
      char buf[256];
      snprintf(buf, sizeof(buf), format_str.c_str(), num);
      result = buf;
    } else {
      result = std::to_string(num);
    }
    if (grouping) {
      result = add_grouping(result);
    }
    if (padding > 0 && static_cast<int>(result.size()) < padding) {
      result = std::string(padding - result.size(), ' ') + result;
    }
    return result;
  };

  auto process_number = [&](const std::string& s) -> std::string {
    long long num;
    if (!parse_number(s, num, round_mode)) {
      if (invalid_policy == "warn") {
        safeErrorPrint("numfmt: invalid number: '" + s + "'\n");
      } else if (invalid_policy == "abort") {
        safeErrorPrint("numfmt: invalid number: '" + s + "'\n");
        return s;
      }
      // "ignore" - return as-is
      return s;
    }

    // Apply --from conversion
    if (!from_unit.empty()) {
      if (from_unit == "si") {
        // parse_number already handles SI suffixes
      } else if (from_unit == "iec") {
        // parse_number already handles IEC suffixes
      }
    }

    if (to_si || to_iec) {
      return format_number(num);
    }
    return apply_format(num);
  };

  int line_num = 0;
  auto process_line = [&](const std::string& line) {
    if (line_num < header) {
      safePrintLn(line);
      ++line_num;
      return;
    }
    if (!delimiter.empty()) {
      // Split by delimiter, process each field
      std::istringstream ss(line);
      std::string field;
      bool first = true;
      while (std::getline(ss, field, delimiter[0])) {
        if (!first) safePrint(delimiter);
        safePrint(process_number(field));
        first = false;
      }
      safePrint("\n");
    } else {
      safePrintLn(process_number(line));
    }
    ++line_num;
  };

  if (!ctx.positionals.empty()) {
    for (const auto& arg : ctx.positionals) {
      process_line(std::string(arg));
    }
  } else {
    std::string input;
    input.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
    std::istringstream iss(input);
    std::string line;
    while (std::getline(iss, line)) {
      process_line(line);
    }
  }

  return 0;
}
