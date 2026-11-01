// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU] seq has no -t/--terminator option; that pair was invented and
    // is removed (#1355). Output always ends with a newline.
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
  // [GNU] literal text around the single conversion directive of
  // --format, counted the way long_double_format() does (each '%%' in the
  // literal text counts once).  Used to strip the number back out of a
  // formatted line for the end-of-range check.
  size_t format_prefix_len = 0;
  size_t format_suffix_len = 0;
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

  if (end != value.c_str() && *end == '\0' && errno != ERANGE &&
      std::isnan(parsed)) {
    // [GNU] seq.c:157-162: a parseable NaN is its own diagnostic,
    // distinct from a malformed floating point argument.
    return error_result<double>("invalid 'not-a-number' argument: '" + value +
                                "'");
  }

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
  if (auto e_pos = text.find_first_of("eE"); e_pos != std::string_view::npos) {
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
  const std::string fmt(format);

  for (size_t i = 0; i < format.size(); ++i) {
    if (format[i] != '%') continue;
    ++i;
    if (i >= format.size()) {
      return error_result<bool>("format '" + fmt + "' ends in %");
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

    // [GNU] seq.c:238-272 (long_double_format): an optional 'L' length
    // modifier precedes the conversion ('%Lf' is valid).
    if (i < format.size() && format[i] == 'L') {
      ++i;
    }

    if (i >= format.size() || !is_floating_conversion(format[i])) {
      // GNU names the offending conversion: "format 'x' has unknown %d
      // directive".  A dangling specifier reports like the bare '%' case.
      if (i >= format.size()) {
        return error_result<bool>("format '" + fmt + "' ends in %");
      }
      return error_result<bool>("format '" + fmt + "' has unknown %" +
                                std::string(1, format[i]) + " directive");
    }

    ++conversions;
    if (conversions > 1) {
      return error_result<bool>("format '" + fmt +
                                "' has too many % directives");
    }
  }

  if (conversions != 1) {
    return error_result<bool>("format '" + fmt + "' has no % directive");
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
      if (arg == "--equal-width") {
        cfg.equal_width = true;
        continue;
      }

      if (arg == "--separator") {
        auto value = read_option_value(ctx.raw_args, i, "", false, arg);
        if (!value) return std::unexpected(value.error());
        cfg.separator = *value;
        continue;
      }

      if (arg.starts_with("--separator=")) {
        cfg.separator = std::string(arg.substr(12));
        continue;
      }

      if (arg == "-f" || arg == "--format") {
        auto value = read_option_value(ctx.raw_args, i, "", false, arg);
        if (!value) return std::unexpected(value.error());
        cfg.format = *value;
        continue;
      }

      if (arg.starts_with("--format=")) {
        cfg.format = std::string(arg.substr(9));
        continue;
      }

      if (!arg.starts_with("--")) {
        // [GNU] getopt walks a short-option cluster character by
        // character: a boolean member continues the walk, and an
        // argument-taking member consumes the rest of the token (a
        // leading '=' is dropped, as in the historical -s=VALUE form)
        // or the next argv element (issue #1356).
        for (size_t j = 1; j < arg.size(); ++j) {
          char c = arg[j];
          if (c == 'w') {
            cfg.equal_width = true;
            continue;
          }
          if (c == 's' || c == 't' || c == 'f') {
            std::string value;
            if (j + 1 < arg.size()) {
              std::string_view rest = arg.substr(j + 1);
              if (rest.starts_with('=')) rest.remove_prefix(1);
              value = std::string(rest);
            } else {
              auto v = read_option_value(ctx.raw_args, i, "", false,
                                         std::string("-") + c);
              if (!v) return std::unexpected(v.error());
              value = *v;
            }
            if (c == 's') {
              cfg.separator = value;
            } else if (c == 't') {
              cfg.terminator = value;
            } else {
              cfg.format = value;
            }
            break;  // the rest of the token was the value
          }
          // [GNU] getopt's short-option diagnostic form.
          return error_result<Config>("invalid option -- '" +
                                      std::string(1, c) + "'");
        }
        continue;
      }

      // [GNU] getopt's long-option diagnostic form.
      return error_result<Config>("unrecognized option '" +
                                  std::string(arg) + "'");
    }

    operands.push_back(arg);
  }

  size_t num_args = operands.size();

  if (num_args == 0) {
    return std::unexpected("missing operand");
  }

  if (num_args > 3) {
    return error_result<Config>("extra operand '" + std::string(operands[3]) +
                                "'");
  }

  // [GNU] seq.c imposes no iteration ceiling (#1376): generation is lazy
  // and streamed through the chunk buffer in run(), so a pipeline consumer
  // (head, grep -m, ...) decides how much is produced. An upfront count
  // cap made `seq 1000000001 | head -3` fail before printing anything.

  // [GNU] seq.c:637-691: the format is validated and the -w/-f conflict
  // is diagnosed before any operand is scanned, so those errors win
  // over malformed operands.
  if (!cfg.format.empty()) {
    auto format = validate_format(cfg.format);
    if (!format) return std::unexpected(format.error());

    // [GNU] record the literal prefix/suffix lengths of the validated
    // format so the generated text can be split into decoration and the
    // numeric part again (see print_numbers() in GNU seq.c).
    const std::string& f = cfg.format;
    size_t i = 0;
    for (; i < f.size() && !(f[i] == '%' && f[i + 1] != '%');
         i += (f[i] == '%') + 1) {
      ++cfg.format_prefix_len;
    }
    ++i;  // skip '%'
    while (i < f.size() &&
           std::string_view("-+ #0'").find(f[i]) != std::string_view::npos) {
      ++i;
    }
    while (i < f.size() &&
           std::isdigit(static_cast<unsigned char>(f[i])) != 0) {
      ++i;
    }
    if (i < f.size() && f[i] == '.') {
      ++i;
      while (i < f.size() &&
             std::isdigit(static_cast<unsigned char>(f[i])) != 0) {
        ++i;
      }
    }
    if (i < f.size() && f[i] == 'L') {
      ++i;  // length modifier, validated above
    }
    ++i;  // skip the conversion character (validate_format ensured it)
    for (; i < f.size(); i += (f[i] == '%') + 1) {
      ++cfg.format_suffix_len;
    }
  }

  if (cfg.equal_width && !cfg.format.empty()) {
    return error_result<Config>(
        "format string may not be specified when printing equal width strings");
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
  } else {
    // seq FIRST INCREMENT LAST
    auto first = parse_number(operands[0]);
    if (!first) return std::unexpected(first.error());
    auto increment = parse_number(operands[1]);
    if (!increment) return std::unexpected(increment.error());
    cfg.first = *first;
    cfg.increment = *increment;
    // [GNU] seq.c:683-688: a zero increment is diagnosed before LAST
    // is scanned, so it wins over a malformed LAST.
    if (cfg.increment == 0.0) {
      return error_result<Config>("invalid Zero increment value: '" +
                                  std::string(operands[1]) + "'");
    }
    auto last = parse_number(operands[2]);
    if (!last) return std::unexpected(last.error());
    cfg.last = *last;
  }

  // [GNU] get_default_format: the default precision is the maximum of
  // FIRST's and INCREMENT's precision only; LAST's precision never widens
  // the output (seq 1.5 prints "1", seq 0.5 1.55 prints "0.5 1.5").
  // LAST still decides whether the fixed-point default format applies.
  for (size_t k = 0; k < operands.size(); ++k) {
    auto precision = decimal_precision(operands[k]);
    if (!precision) {
      cfg.fixed_default_format = false;
      cfg.default_precision = 0;
      break;
    }
    if (k + 1 < operands.size()) {
      cfg.default_precision = std::max(cfg.default_precision, *precision);
    }
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

  const bool increasing = (cfg.increment > 0);
  const auto in_range = [&](double x) {
    return increasing ? x <= cfg.last : x >= cfg.last;
  };

  if (!in_range(cfg.first)) {
    return 0;
  }

  // [GNU] The value after LAST is printed anyway when its formatted text
  // parses back to exactly LAST and differs from the previous item.  This
  // rescues sequences such as `seq 1 0.1 1.3` where binary rounding pushes
  // the mathematical endpoint slightly past LAST (uutils #7186).
  const auto formatted_value = [&](const std::string& text) -> double {
    std::string_view middle(text);
    if (cfg.format_prefix_len + cfg.format_suffix_len <= middle.size()) {
      middle.remove_prefix(cfg.format_prefix_len);
      middle.remove_suffix(cfg.format_suffix_len);
    }
    return std::strtod(std::string(middle).c_str(), nullptr);
  };

  // Returns true when NEXT is the rescued endpoint that must still print.
  const auto print_extra_number = [&](double next, double prev) -> bool {
    const std::string next_str = format_number(next, cfg);
    if (formatted_value(next_str) != cfg.last) return false;
    return next_str != format_number(prev, cfg);
  };

  // For equal-width mode, first pass to find max width (no storage)
  size_t max_width = 0;
  if (cfg.equal_width && cfg.format.empty()) {
    double current = cfg.first;
    for (double i = 1;; ++i) {
      const double prev = current;
      max_width = std::max(max_width, format_number(current, cfg).size());
      const double next = cfg.first + i * cfg.increment;
      if (!std::isfinite(next) || !in_range(next)) {
        if (std::isfinite(next) && print_extra_number(next, prev)) {
          max_width = std::max(max_width, format_number(next, cfg).size());
        }
        break;
      }
      current = next;
    }
  }

  // Second pass: generate and output incrementally (streaming, no memory
  // blowup).  [GNU] iterates x = first + i*step which is mathematically
  // equivalent to x += step but less subject to accumulated rounding error.
  //
  // Output is assembled into a 64 KiB buffer and flushed in large chunks:
  // safePrint issues one WriteFile per call, so emitting per line made long
  // sequences pay a kernel transition two-three times per line (~30us/line
  // through a pipe, 100k lines in ~332ms while GNU needs ~5ms, #1173).
  // Batching keeps the byte stream identical while cutting the syscall
  // count to roughly one per buffer.
  constexpr size_t kFlushThreshold = 64 * 1024;
  std::string out_buffer;
  out_buffer.reserve(kFlushThreshold + 128);
  const auto flush_output = [&out_buffer]() {
    if (!out_buffer.empty()) {
      safePrint(out_buffer);
      out_buffer.clear();
    }
  };
  const auto buffer_output = [&](std::string_view text) {
    out_buffer.append(text);
    if (out_buffer.size() >= kFlushThreshold) {
      flush_output();
    }
  };

  // [GNU] seq.c drives integral first/increment sequences through integer
  // arithmetic instead of floating-point formatting.  When the operands
  // admit it — integral FIRST and INCREMENT, the default fixed format with
  // no fractional digits, no --format, not --equal-width, and every value
  // inside the exact-integer double range (<= 2^52 keeps the range test on
  // the next item exact too) — emit through std::to_chars: snprintf is what
  // still cost ~0.6us/line after batching (#1173).
  constexpr double kExactIntLimit = 4503599627370496.0;  // 2^52
  const bool integral_fast_path =
      cfg.format.empty() && cfg.fixed_default_format &&
      cfg.default_precision == 0 && !cfg.equal_width &&
      std::isfinite(cfg.first) && std::isfinite(cfg.increment) &&
      std::isfinite(cfg.last) && std::floor(cfg.first) == cfg.first &&
      std::floor(cfg.increment) == cfg.increment &&
      std::fabs(cfg.first) <= kExactIntLimit &&
      std::fabs(cfg.increment) <= kExactIntLimit &&
      std::fabs(cfg.last) <= kExactIntLimit;

  bool first_item = true;

  // #1368: a failed stdout write is fatal for GNU seq — seq.c calls
  // write_error() (system.h:960), printing one "seq: write error:
  // <strerror>" diagnostic and exiting 1. A closed downstream pipe keeps
  // the quiet SIGPIPE-style exit 0 established by #1376.
  const auto stdout_failure_rc = [&]() -> int {
    return report_stdout_write_error("seq", 1);
  };

  if (integral_fast_path) {
    const auto first_i = static_cast<long long>(cfg.first);
    const auto inc_i = static_cast<long long>(cfg.increment);
    char number_buf[32];
    for (long long x = first_i;;) {
      // [GNU] a consumer that stops reading (head, grep -m) ends the
      // stream: GNU seq dies of SIGPIPE; the console layer records the
      // broken pipe in a flag instead, so poll it per item (#1376).
      if (is_stdout_pipe_closed()) return 0;
      if (is_stdout_write_failed()) return stdout_failure_rc();
      auto [ptr, ec] =
          std::to_chars(number_buf, number_buf + sizeof(number_buf), x);
      if (!first_item) {
        buffer_output(cfg.separator);
      }
      buffer_output(std::string_view(number_buf, ptr - number_buf));
      first_item = false;

      x += inc_i;
      // The endpoint-rescue of the floating loop can never fire here: an
      // integral NEXT equal to LAST satisfies in_range, so anything past
      // the range also formats back to a value != LAST.
      if (increasing ? static_cast<double>(x) > cfg.last
                     : static_cast<double>(x) < cfg.last) {
        break;
      }
    }
    buffer_output(cfg.terminator);
    flush_output();
    return stdout_failure_rc();
  }

  double current = cfg.first;
  // Like format_number() but into caller storage: one snprintf per line
  // instead of a size-query + write pair, and the default fixed format
  // string is built once instead of per line (#1173).  Falls back to
  // format_number() when the text exceeds the stack buffer (exotic user
  // formats); equal-width padding copies before mutating.
  const std::string fixed_format =
      "%." + std::to_string(cfg.default_precision) + "f";
  char format_buf[1024];
  // Scratch for the rare overlong outputs that do not fit format_buf; the
  // returned view is consumed before the next format_value call.
  std::string fallback;
  const auto format_value = [&](double value) -> std::string_view {
    const char* fmt = nullptr;
    if (!cfg.format.empty()) {
      fmt = cfg.format.c_str();
    } else if (cfg.fixed_default_format) {
      fmt = fixed_format.c_str();
    } else {
      fmt = "%g";
    }
    const int len = std::snprintf(format_buf, sizeof(format_buf), fmt, value);
    if (len >= 0 && static_cast<size_t>(len) < sizeof(format_buf)) {
      return std::string_view(format_buf, static_cast<size_t>(len));
    }
    fallback = format_number(value, cfg);
    return fallback;
  };

  const auto emit = [&](double x) {
    if (cfg.equal_width && cfg.format.empty()) {
      std::string formatted = zero_pad(std::string(format_value(x)), max_width);
      if (!first_item) {
        buffer_output(cfg.separator);
      }
      buffer_output(formatted);
      first_item = false;
      return;
    }
    if (!first_item) {
      buffer_output(cfg.separator);
    }
    buffer_output(format_value(x));
    first_item = false;
  };

  for (double i = 1;; ++i) {
    if (is_stdout_pipe_closed()) return 0;  // see fast path note (#1376)
    if (is_stdout_write_failed()) return stdout_failure_rc();  // #1368
    const double prev = current;
    emit(current);

    const double next = cfg.first + i * cfg.increment;
    if (!std::isfinite(next)) {
      break;
    }
    if (!in_range(next)) {
      if (print_extra_number(next, prev)) {
        emit(next);
      }
      break;
    }
    current = next;
  }

  if (!first_item) {
    buffer_output(cfg.terminator);
  }
  flush_output();

  return stdout_failure_rc();
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
