/*
 *  Copyright © 2026 [caomengxuan666]
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
 *  - File: seq.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for seq.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include <clocale>

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr SEQ_OPTIONS = std::array{
    // [GNU]
    OPTION("-f", "--format", "use printf style floating-point FORMAT",
           STRING_TYPE),
    // [GNU]
    OPTION("-s", "--separator", "use STRING to separate numbers", STRING_TYPE),
    // [GNU]
    OPTION("-t", "--terminator",
           "use STRING to terminate the output instead of newline",
           STRING_TYPE),
    // [GNU]
    OPTION("-w", "--equal-width",
           "equalize width by padding with leading zeroes", BOOL_TYPE),
    // The shared option parser runs before seq can inspect operands. These
    // hidden sentinels let negative numeric operands reach seq's parser.
    // [GNU]
    OPTION("-0", "", "", BOOL_TYPE), OPTION("-1", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-2", "", "", BOOL_TYPE), OPTION("-3", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-4", "", "", BOOL_TYPE), OPTION("-5", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-6", "", "", BOOL_TYPE), OPTION("-7", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-8", "", "", BOOL_TYPE), OPTION("-9", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-.", "", "", BOOL_TYPE), OPTION("-+", "", "", BOOL_TYPE),
    // [EXT]
    OPTION("--", "", "", BOOL_TYPE), OPTION("-a", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-A", "", "", BOOL_TYPE), OPTION("-e", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-E", "", "", BOOL_TYPE), OPTION("-F", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-i", "", "", BOOL_TYPE), OPTION("-I", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-n", "", "", BOOL_TYPE), OPTION("-N", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-inf", "", "", BOOL_TYPE), OPTION("-Inf", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-INF", "", "", BOOL_TYPE), OPTION("-infinity", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-Infinity", "", "", BOOL_TYPE),
    // [GNU]
    OPTION("-INFINITY", "", "", BOOL_TYPE)};

namespace seq_pipeline {
namespace cp = core::pipeline;

struct Config {
  std::string format;
  std::string separator;
  std::string terminator;
  bool equal_width = false;
  bool fixed_default_format = true;
  int default_precision = 0;
  double first = 1.0;
  double increment = 1.0;
  double last = 1.0;
};

template <typename T>
auto error_result(std::string message) -> cp::Result<T> {
  return std::unexpected<cp::Error>(std::move(message));
}

auto parse_number(std::string_view text) -> cp::Result<double> {
  std::string value(text);

  // Detect inf/infinity before strtod: on Windows/MSVC strtod may set errno
  // to ERANGE for infinity strings, which would otherwise reject valid
  // operands.
  {
    std::string lower;
    lower.reserve(value.size());
    for (char c : value) {
      lower.push_back(
          static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    if (lower == "inf" || lower == "infinity") return INFINITY;
    if (lower == "-inf" || lower == "-infinity") return -INFINITY;
  }

  char* end = nullptr;
  errno = 0;
  double parsed = std::strtod(value.c_str(), &end);

  if (end == value.c_str() || *end != '\0' || errno == ERANGE ||
      std::isnan(parsed)) {
    // [GNU] message uses a colon between the argument kind and the quoted
    // value ("invalid floating point argument: '4e4000003'"), and strtod
    // overflow (ERANGE) must be rejected exactly like a malformed operand so
    // out-of-range exponents never enter the loop below (#958).
    return error_result<double>("invalid floating point argument: '" + value +
                                "'");
  }

  return parsed;
}

auto decimal_precision(std::string_view text) -> std::optional<int> {
  if (text.empty()) return std::nullopt;

  // [GNU] scientific-notation operands still yield a fixed-point default
  // format: precision is the mantissa's fractional digit count adjusted by
  // the exponent (8.0e-1 -> 2, 8e-1 -> 1, 1.0e5 -> 0).
  if (auto e_pos = text.find_first_of("eE");
      e_pos != std::string_view::npos) {
    std::string_view mantissa = text.substr(0, e_pos);
    std::string_view exp_part = text.substr(e_pos + 1);
    long exponent = 0;
    if (!exp_part.empty()) {
      // from_chars does not accept a leading '+'; GNU seq does (1.0e+5).
      if (!exp_part.empty() && exp_part.front() == '+') {
        exp_part.remove_prefix(1);
      }
      if (exp_part.empty()) {
        return std::nullopt;
      }
      auto [ptr, ec] = std::from_chars(
          exp_part.data(), exp_part.data() + exp_part.size(), exponent);
      if (ec != std::errc() || ptr != exp_part.data() + exp_part.size()) {
        return std::nullopt;
      }
    } else {
      return std::nullopt;
    }
    auto dot = mantissa.find('.');
    int frac = dot == std::string_view::npos
                   ? 0
                   : static_cast<int>(mantissa.size() - dot - 1);
    long long adjusted = static_cast<long long>(frac) - exponent;
    if (adjusted < 0) adjusted = 0;
    if (adjusted > 400) adjusted = 400;
    return static_cast<int>(adjusted);
  }

  auto first_digit = text.find_first_of("0123456789");
  if (first_digit == std::string_view::npos) {
    return std::nullopt;
  }

  auto dot = text.find('.');
  if (dot == std::string_view::npos) return 0;

  size_t end = text.size();
  return static_cast<int>(end - dot - 1);
}

auto looks_like_number(std::string_view text) -> bool {
  auto parsed = parse_number(text);
  return parsed.has_value();
}

auto is_floating_conversion(char ch) -> bool {
  switch (ch) {
    case 'a':
    case 'A':
    case 'e':
    case 'E':
    case 'f':
    case 'F':
    case 'g':
    case 'G':
      return true;
    default:
      return false;
  }
}

auto validate_format(std::string_view format) -> cp::Result<bool> {
  int conversions = 0;

  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%') continue;
    ++i;
    if (i >= format.size()) {
      return error_result<bool>(
          "format must contain exactly one floating-point conversion");
    }
    if (format[i] == '%') continue;

    while (i < format.size() && std::string_view("-+ #0'").find(format[i]) !=
                                    std::string_view::npos) {
      ++i;
    }

    while (i < format.size() &&
           std::isdigit(static_cast<unsigned char>(format[i])) != 0) {
      ++i;
    }

    if (i < format.size() && format[i] == '.') {
      ++i;
      while (i < format.size() &&
             std::isdigit(static_cast<unsigned char>(format[i])) != 0) {
        ++i;
      }
    }

    if (i >= format.size() || !is_floating_conversion(format[i])) {
      return error_result<bool>(
          "format must contain exactly one floating-point conversion");
    }

    ++conversions;
    if (conversions > 1) {
      return error_result<bool>(
          "format must contain exactly one floating-point conversion");
    }
  }

  if (conversions != 1) {
    return error_result<bool>(
        "format must contain exactly one floating-point conversion");
  }

  return true;
}

auto read_option_value(std::span<const std::string_view> args, size_t& i,
                       std::string_view inline_value, bool has_inline_value,
                       std::string_view option_name)
    -> cp::Result<std::string> {
  if (has_inline_value) return std::string(inline_value);
  if (i + 1 >= args.size()) {
    return error_result<std::string>("option '" + std::string(option_name) +
                                     "' requires an argument");
  }
  return std::string(args[++i]);
}

auto build_config(const CommandContext<SEQ_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;
  cfg.separator = "\n";
  cfg.terminator = "\n";
  std::vector<std::string_view> operands;

  bool parsing_options = true;
  for (size_t i = 0; i < ctx.raw_args.size(); ++i) {
    std::string_view arg = ctx.raw_args[i];

    if (parsing_options && arg == "--") {
      parsing_options = false;
      continue;
    }

    if (parsing_options && arg.starts_with('-') && arg != "-" &&
        !looks_like_number(arg)) {
      if (arg == "-w" || arg == "--equal-width") {
        cfg.equal_width = true;
        continue;
      }

      if (arg == "-s" || arg == "--separator") {
        auto value = read_option_value(ctx.raw_args, i, "", false, arg);
        if (!value) return std::unexpected(value.error());
        cfg.separator = *value;
        continue;
      }

      if (arg.starts_with("-s") && arg.size() > 2) {
        // Handle both -sVALUE and -s=VALUE forms.
        cfg.separator =
            std::string(arg[2] == '=' ? arg.substr(3) : arg.substr(2));
        continue;
      }

      if (arg.starts_with("--separator=")) {
        cfg.separator = std::string(arg.substr(12));
        continue;
      }

      if (arg == "-t" || arg == "--terminator") {
        auto value = read_option_value(ctx.raw_args, i, "", false, arg);
        if (!value) return std::unexpected(value.error());
        cfg.terminator = *value;
        continue;
      }

      if (arg.starts_with("-t") && arg.size() > 2) {
        // Handle both -tVALUE and -t=VALUE forms.
        cfg.terminator =
            std::string(arg[2] == '=' ? arg.substr(3) : arg.substr(2));
        continue;
      }

      if (arg.starts_with("--terminator=")) {
        cfg.terminator = std::string(arg.substr(13));
        continue;
      }

      if (arg == "-f" || arg == "--format") {
        auto value = read_option_value(ctx.raw_args, i, "", false, arg);
        if (!value) return std::unexpected(value.error());
        cfg.format = *value;
        continue;
      }

      if (arg.starts_with("-f") && arg.size() > 2) {
        // Handle both -fVALUE and -f=VALUE forms.
        cfg.format = std::string(arg[2] == '=' ? arg.substr(3) : arg.substr(2));
        continue;
      }

      if (arg.starts_with("--format=")) {
        cfg.format = std::string(arg.substr(9));
        continue;
      }

      return error_result<Config>("invalid option '" + std::string(arg) + "'");
    }

    operands.push_back(arg);
  }

  size_t num_args = operands.size();

  if (num_args == 0) {
    return std::unexpected("missing operand");
  }

  if (num_args == 1) {
    // seq LAST
    auto last = parse_number(operands[0]);
    if (!last) return std::unexpected(last.error());
    cfg.last = *last;
  } else if (num_args == 2) {
    // seq FIRST LAST
    auto first = parse_number(operands[0]);
    if (!first) return std::unexpected(first.error());
    auto last = parse_number(operands[1]);
    if (!last) return std::unexpected(last.error());
    cfg.first = *first;
    cfg.last = *last;
  } else if (num_args == 3) {
    // seq FIRST INCREMENT LAST
    auto first = parse_number(operands[0]);
    if (!first) return std::unexpected(first.error());
    auto increment = parse_number(operands[1]);
    if (!increment) return std::unexpected(increment.error());
    auto last = parse_number(operands[2]);
    if (!last) return std::unexpected(last.error());
    cfg.first = *first;
    cfg.increment = *increment;
    cfg.last = *last;
  } else {
    return error_result<Config>("extra operand '" + std::string(operands[3]) +
                                "'");
  }

  if (cfg.increment == 0.0) {
    return error_result<Config>("invalid Zero increment value: '" +
                                std::string(operands[num_args == 3 ? 1 : 0]) +
                                "'");
  }

  // Pre-compute the iteration count to guard against infinite loops caused by
  // extremely small increments (e.g. seq 0 1e-20 1 would otherwise loop ~10^20
  // times). Reject sequences with more than ~10^9 iterations.
  {
    const double count = (cfg.last - cfg.first) / cfg.increment + 1.0;
    if (count > 1e9) {
      return error_result<Config>("too many iterations");
    }
  }

  if (!cfg.format.empty()) {
    auto format = validate_format(cfg.format);
    if (!format) return std::unexpected(format.error());
  }

  if (cfg.equal_width && !cfg.format.empty()) {
    return error_result<Config>(
        "format string may not be specified when printing equal width strings");
  }

  for (auto operand : operands) {
    auto precision = decimal_precision(operand);
    if (!precision) {
      cfg.fixed_default_format = false;
      cfg.default_precision = 0;
      break;
    }
    cfg.default_precision = std::max(cfg.default_precision, *precision);
  }

  return cfg;
}

auto snprintf_string(const std::string& fmt, double value) -> std::string {
  int len = std::snprintf(nullptr, 0, fmt.c_str(), value);
  if (len < 0) return {};

  std::string out(static_cast<size_t>(len) + 1, '\0');
  std::snprintf(out.data(), out.size(), fmt.c_str(), value);
  out.pop_back();
  return out;
}

auto format_number(double value, const Config& cfg) -> std::string {
  if (!cfg.format.empty()) {
    return snprintf_string(cfg.format, value);
  }

  if (cfg.fixed_default_format) {
    return snprintf_string("%." + std::to_string(cfg.default_precision) + "f",
                           value);
  }

  return snprintf_string("%g", value);
}

auto zero_pad(std::string value, size_t width) -> std::string {
  if (value.size() >= width) return value;

  size_t zeros = width - value.size();
  if (!value.empty() && (value[0] == '-' || value[0] == '+')) {
    value.insert(1, zeros, '0');
  } else {
    value.insert(0, zeros, '0');
  }
  return value;
}

auto run(const Config& cfg) -> int {
  // Ensure C locale for consistent numeric formatting (decimal point '.')
  setlocale(LC_NUMERIC, "C");

  // Determine direction
  bool increasing = (cfg.increment > 0);

  // Determine if we should output anything
  bool should_output = false;
  if (increasing) {
    should_output = (cfg.first <= cfg.last);
  } else {
    should_output = (cfg.first >= cfg.last);
  }

  if (!should_output) {
    return 0;
  }

  // For equal-width mode, first pass to find max width (no storage)
  size_t max_width = 0;
  if (cfg.equal_width && cfg.format.empty()) {
    double current = cfg.first;
    while ((increasing && current <= cfg.last) ||
           (!increasing && current >= cfg.last)) {
      auto formatted = format_number(current, cfg);
      max_width = std::max(max_width, formatted.size());
      const double next = current + cfg.increment;
      if (!std::isfinite(next) || next == current) {
        break;
      }
      current = next;
    }
  }

  // Second pass: generate and output incrementally (streaming, no memory
  // blowup)
  bool first_item = true;
  double current = cfg.first;

  while ((increasing && current <= cfg.last) ||
         (!increasing && current >= cfg.last)) {
    std::string formatted = format_number(current, cfg);
    if (cfg.equal_width && cfg.format.empty()) {
      formatted = zero_pad(std::move(formatted), max_width);
    }
    if (!first_item) {
      safePrint(cfg.separator);
    }
    safePrint(formatted);
    first_item = false;

    const double next = current + cfg.increment;
    if (!std::isfinite(next) || next == current) {
      break;
    }
    current = next;
  }

  if (!first_item) {
    safePrint(cfg.terminator);
  }

  return 0;
}

}  // namespace seq_pipeline

REGISTER_COMMAND(seq, "seq",
                 "seq [OPTION]... LAST\n"
                 "seq [OPTION]... FIRST LAST\n"
                 "seq [OPTION]... FIRST INCREMENT LAST",
                 "Print numbers from FIRST to LAST, in steps of INCREMENT.",
                 "  seq 10\n"
                 "  seq 1 10\n"
                 "  seq 1 2 10\n"
                 "  seq -s ' ' 5 10\n"
                 "  seq -w 1 10",
                 "printf(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 SEQ_OPTIONS) {
  using namespace seq_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    if (cfg_result.error() == "missing operand") {
      safeErrorPrintLn("seq: missing operand");
      safeErrorPrintLn("Try 'seq --help' for more information.");
      return 1;
    }
    if (cfg_result.error().starts_with("extra operand '")) {
      cp::report_error(cfg_result, L"seq");
      safeErrorPrintLn("Try 'seq --help' for more information.");
      return 1;
    }
    cp::report_error(cfg_result, L"seq");
    safeErrorPrintLn("Try 'seq --help' for more information.");
    return 1;
  }

  return run(*cfg_result);
}
