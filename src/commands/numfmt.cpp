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
    OPTION("", "--padding", "pad numbers to width N", INT_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Parse number with SI suffixes (K, M, G, T, P)
bool parse_number(const std::string& s, long long& result) {
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

    result = static_cast<long long>(num);
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

  auto process_number = [&](const std::string& s) -> std::string {
    if (to_si || to_iec) {
      long long num;
      if (parse_number(s, num)) {
        return format_number(num);
      }
      return s;
    } else {
      long long num;
      if (parse_number(s, num)) {
        return std::to_string(num);
      }
      return s;
    }
  };

  if (!ctx.positionals.empty()) {
    for (const auto& arg : ctx.positionals) {
      safePrintLn(process_number(std::string(arg)));
    }
  } else {
    std::string input;
    input.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
    std::istringstream iss(input);
    std::string line;
    while (std::getline(iss, line)) {
      safePrintLn(process_number(line));
    }
  }

  return 0;
}
