// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // [GNU]
    OPTION("-d", "--delimiter",
           "use X instead of whitespace for field delimiter", STRING_TYPE),
    // [GNU]
    OPTION("", "--from", "autoconvert from X", STRING_TYPE),
    // [GNU]
    OPTION("", "--to", "autoconvert to X", STRING_TYPE),
    // [GNU]
    OPTION("", "--round", "use METHOD for rounding", STRING_TYPE),
    // [GNU]
    OPTION("", "--padding", "pad numbers to width N", INT_TYPE),
    // [EXT]
    OPTION("", "--pad", "pad numbers to width N", INT_TYPE),
    // [GNU]
    OPTION("", "--suffix", "add STRING after formatted numbers", STRING_TYPE),
    // [GNU] --field accepts cut(1)-style ranges (N, N-M, N-, -M, -, and
    // comma separated lists); default is field 1 only.
    OPTION("", "--field", "replace the numbers in these input fields",
           STRING_TYPE),
    // [GNU]
    OPTION("-f", "--format", "use printf style floating-point FORMAT",
           STRING_TYPE),
    // [GNU] --header[=N]: optional value; a bare --header means N=1 and the
    // next argument stays an input operand (uutils #13272)
    OPTION("", "--header", "print the first N header lines unchanged",
           OPTIONAL_INT_TYPE),
    // [GNU]
    OPTION("", "--grouping", "group digits with locale thousands separator",
           BOOL_TYPE),
    // [GNU]
    OPTION("", "--invalid",
           "set policy for invalid values: 'abort' (default), 'warn', 'ignore'",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--debug", "print conversion diagnostics", BOOL_TYPE),
    // [GNU] hidden developer option, spelled "---debug" on the command
    // line (numfmt.c {"-debug"}); aliases --debug. Empty description keeps
    // it out of --help like GNU.
    OPTION("", "---debug", "", BOOL_TYPE),
    // [GNU] --from-unit: multiply input numbers by UNIT
    OPTION("", "--from-unit", "multiply input numbers by UNIT", STRING_TYPE),
    // [GNU] --to-unit: divide numbers by UNIT before formatting
    OPTION("", "--to-unit", "divide numbers by UNIT before formatting",
           STRING_TYPE),
    // [GNU] --unit-separator: use STRING between number and unit
    OPTION("", "--unit-separator", "use STRING between number and unit",
           STRING_TYPE),
    // [GNU] --zero-terminated: line delimiter is NUL, not newline
    OPTION("-z", "--zero-terminated", "line delimiter is NUL, not newline",
           BOOL_TYPE),
    // [GNU] -M: use IEC units (powers of 1024)
    OPTION("-M", "", "use IEC units (powers of 1024)", BOOL_TYPE),
    // [GNU] -l: use locale grouping
    OPTION("-l", "", "use locale grouping", BOOL_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {

// [GNU] The default --round method is "from-zero" (away from zero), not
// "nearest"; the rounding applies at the precision chosen for output.
double apply_rounding(double value, const std::string& mode) {
  if (mode == "up") {
    return std::ceil(value);
  } else if (mode == "down") {
    return std::floor(value);
  } else if (mode == "towards-zero") {
    return std::trunc(value);
  } else if (mode == "nearest") {
    return (value >= 0) ? std::floor(value + 0.5) : std::ceil(value - 0.5);
  }
  // "from-zero" (GNU default) and fallback for anything else.
  return (value >= 0) ? std::ceil(value) : std::floor(value);
}

// Field/blank separator approximation of GNU's c32issep().
bool is_field_blank(char c) { return c == ' ' || c == '\t'; }

// [GNU] valid suffix letters and their powers ("KkMGTPEZYRQ").
int suffix_power(char c) {
  switch (c) {
    case 'k':
    case 'K':
      return 1;
    case 'M':
      return 2;
    case 'G':
      return 3;
    case 'T':
      return 4;
    case 'P':
      return 5;
    case 'E':
      return 6;
    case 'Z':
      return 7;
    case 'Y':
      return 8;
    case 'R':
      return 9;
    case 'Q':
      return 10;
    default:
      return -1;
  }
}

const char* suffix_power_char(int power) {
  static const char* const kChars[] = {"",  "K", "M", "G", "T", "P",
                                       "E", "Z", "Y", "R", "Q"};
  if (power < 0 || power > 10) return "(error)";
  return kChars[power];
}

enum class NumParseStatus {
  Ok,
  InvalidNumber,    // "invalid number: 'X'"
  ForbiddenSuffix,  // "rejecting suffix in input: 'X' (consider using --from)"
  InvalidSuffix,    // "invalid suffix in input: 'X'"
  InvalidSuffixTail,  // "invalid suffix in input 'X': 'tail'"
  MissingI,           // "missing 'i' suffix in input: 'X' (e.g Ki/Mi/Gi)"
  Overflow            // "value too large to be converted: 'X'"
};

struct ParsedInput {
  NumParseStatus status = NumParseStatus::Ok;
  double value = 0.0;
  // Number of fractional digits written by the user; [GNU] resets this to 0
  // once a unit suffix is present so the scale selects the precision.
  int precision = 0;
  // Trailing text after a valid suffix letter (for InvalidSuffixTail).
  std::string tail;
};

// [GNU] simple_strtod_human: parses "NNNN[.NNNNN][sep][SUFFIX]".  There is
// no exponent support, '+' is not a sign, a single blank or the
// --unit-separator may separate the number from the suffix, and trailing
// blanks are allowed.
ParsedInput parse_human(std::string_view input, const std::string& scale_from,
                        const std::string& unit_sep) {
  ParsedInput r;
  const std::string s(input);
  size_t i = 0;
  if (i < s.size() && s[i] == '-') ++i;

  const size_t int_beg = i;
  size_t digits = 0;
  bool seen_nonzero = false;
  const auto scan_digits = [&]() -> size_t {
    const size_t beg = i;
    while (i < s.size() &&
           std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
      if (seen_nonzero || s[i] != '0') {
        seen_nonzero = true;
        ++digits;
      }
      ++i;
    }
    return i - beg;
  };

  const size_t int_digits = scan_digits();
  size_t frac_digits = 0;
  if (i < s.size() && s[i] == '.') {
    ++i;
    frac_digits = scan_digits();
    // [GNU] "5." is rejected: a decimal point must be followed by digits.
    if (frac_digits == 0) {
      r.status = NumParseStatus::InvalidNumber;
      return r;
    }
  }
  if (int_digits == 0 && frac_digits == 0) {
    r.status = NumParseStatus::InvalidNumber;
    return r;
  }
  if (digits > 33) {
    r.status = NumParseStatus::Overflow;
    return r;
  }

  try {
    r.value = std::stod(s.substr(0, i));
  } catch (...) {
    r.status = NumParseStatus::InvalidNumber;
    return r;
  }
  r.precision = static_cast<int>(frac_digits);

  if (i >= s.size()) return r;

  // [GNU] an explicit --unit-separator, or otherwise a single blank, may
  // precede the suffix.
  size_t j = i;
  bool matched_sep = false;
  if (!unit_sep.empty() && s.compare(j, unit_sep.size(), unit_sep) == 0) {
    j += unit_sep.size();
    matched_sep = true;
  }
  if (!matched_sep && j < s.size() && is_field_blank(s[j])) ++j;
  if (j >= s.size()) return r;

  const int power = suffix_power(s[j]);
  if (power < 0) {
    // [GNU] trailing blanks are allowed; anything else is an invalid
    // suffix.
    while (j < s.size() && is_field_blank(s[j])) ++j;
    if (j < s.size()) r.status = NumParseStatus::InvalidSuffix;
    return r;
  }

  if (scale_from.empty() || scale_from == "none") {
    r.status = NumParseStatus::ForbiddenSuffix;
    return r;
  }
  ++j;
  double base =
      (scale_from == "iec" || scale_from == "iec-i") ? 1024.0 : 1000.0;
  if (scale_from == "auto" && j < s.size() && s[j] == 'i') {
    // [GNU] auto-scaling switches to base 1024 for "Ki/Mi/Gi" style input.
    base = 1024.0;
    ++j;
  } else if (scale_from == "iec-i") {
    if (j < s.size() && s[j] == 'i') {
      ++j;
    } else {
      r.status = NumParseStatus::MissingI;
      return r;
    }
  }
  r.precision = 0;

  while (j < s.size() && is_field_blank(s[j])) ++j;
  if (j < s.size()) {
    r.status = NumParseStatus::InvalidSuffixTail;
    r.tail = s.substr(j);
    return r;
  }

  r.value *= std::pow(base, power);
  return r;
}

// [GNU] unit_to_umax: --from-unit/--to-unit take an integer with an
// optional single suffix letter.  "K" means 1000 while "Ki" means 1024
// (a trailing 'i' after the letter selects base 1024); a bare suffix
// letter counts as one unit.  Fractional, zero or junk values are
// rejected with "invalid unit size".
std::optional<double> unit_size(const std::string& text) {
  if (text.empty()) return std::nullopt;
  std::string digits = text;
  double base = 1.0;
  int power = 0;
  const char last = text.back();
  if (std::isdigit(static_cast<unsigned char>(last)) == 0) {
    if (last == 'i' && text.size() >= 2 &&
        std::isdigit(static_cast<unsigned char>(text[text.size() - 2])) == 0) {
      digits.pop_back();
      base = 1024.0;
    } else {
      base = 1000.0;
    }
    if (digits.empty()) return std::nullopt;
    power = suffix_power(digits.back());
    if (power < 0) return std::nullopt;
    digits.pop_back();
  }
  for (const char d : digits) {
    if (std::isdigit(static_cast<unsigned char>(d)) == 0) {
      return std::nullopt;
    }
  }
  unsigned long long n = 1;
  if (!digits.empty()) {
    try {
      n = std::stoull(digits);
    } catch (...) {
      return std::nullopt;
    }
  }
  if (n == 0) return std::nullopt;
  return static_cast<double>(n) * std::pow(base, power);
}

// [GNU] parse_format_string: a --format value is not handed to printf
// directly; only %[0]['][-][N][.][N]f is supported.  The literal text
// before/after the directive becomes the output prefix/suffix, ' selects
// grouping, 0N selects zero padding, [-]N selects space padding and .N
// fixes the precision.
struct FmtSpec {
  std::string prefix;
  std::string suffix;
  bool grouping = false;
  int zero_pad = 0;
  long long pad_width = 0;
  int user_precision = -1;
};

std::optional<std::string> parse_format(const std::string& fmt, FmtSpec& out) {
  size_t i = 0;
  size_t prefix_len = 0;
  for (;; i += (fmt[i] == '%') + 1) {
    if (i >= fmt.size() || fmt[i] == '\0') {
      return ::winux::i18n::format("command.numfmt.error.format_no_directive",
                                   "format '{}' has no % directive", fmt);
    }
    if (fmt[i] == '%' && fmt[i + 1] != '%') break;
    ++prefix_len;
  }

  ++i;
  bool zero_padding = false;
  while (true) {
    size_t skip = 0;
    while (i + skip < fmt.size() && fmt[i + skip] == ' ') ++skip;
    i += skip;
    if (i < fmt.size() && fmt[i] == '\'') {
      out.grouping = true;
      ++i;
    } else if (i < fmt.size() && fmt[i] == '0') {
      zero_padding = true;
      ++i;
    } else if (skip == 0) {
      break;
    }
  }

  char* endptr = nullptr;
  const long long pad = std::strtoll(fmt.c_str() + i, &endptr, 10);
  i = static_cast<size_t>(endptr - fmt.c_str());
  if (pad != 0) {
    if (pad < 0) {
      out.pad_width = pad;
    } else if (zero_padding) {
      out.zero_pad = pad > INT_MAX ? INT_MAX : static_cast<int>(pad);
    } else {
      out.pad_width = pad;
    }
  }

  if (i >= fmt.size()) {
    return ::winux::i18n::format("command.numfmt.error.format_ends_percent",
                                 "format '{}' ends in %", fmt);
  }

  if (fmt[i] == '.') {
    ++i;
    if (i < fmt.size() && (fmt[i] == '+' || fmt[i] == ' ' || fmt[i] == '\t')) {
      return ::winux::i18n::format("command.numfmt.error.format_bad_precision",
                                   "invalid precision in format '{}'", fmt);
    }
    errno = 0;
    const long prec = std::strtol(fmt.c_str() + i, &endptr, 10);
    if (errno == ERANGE || prec < 0) {
      return ::winux::i18n::format("command.numfmt.error.format_bad_precision",
                                   "invalid precision in format '{}'", fmt);
    }
    out.user_precision = static_cast<int>(prec);
    i = static_cast<size_t>(endptr - fmt.c_str());
  }

  if (i >= fmt.size() || fmt[i] != 'f') {
    return ::winux::i18n::format(
        "command.numfmt.error.format_not_f",
        "invalid format '{}', directive must be %[0]['][-][N][.][N]f", fmt);
  }
  ++i;
  const size_t suffix_pos = i;

  for (; i < fmt.size(); i += (fmt[i] == '%') + 1) {
    if (fmt[i] == '%' && i + 1 < fmt.size() && fmt[i + 1] != '%') {
      return ::winux::i18n::format("command.numfmt.error.format_too_many",
                                   "format '{}' has too many % directives",
                                   fmt);
    }
  }

  out.prefix = fmt.substr(0, prefix_len);
  out.suffix = fmt.substr(suffix_pos);
  return std::nullopt;
}

// Insert ',' every three digits in the integer part of S (before any '.');
// the sign and the fractional part are untouched ([GNU] %'f semantics).
std::string group_integer_part(const std::string& s) {
  const size_t beg = (!s.empty() && (s[0] == '-' || s[0] == '+')) ? 1 : 0;
  const size_t dot = s.find('.');
  const size_t end = dot == std::string::npos ? s.size() : dot;
  const size_t n = end - beg;
  if (n <= 3) return s;
  size_t first = n % 3;
  if (first == 0) first = 3;
  std::string out = s.substr(0, beg);
  out.append(s, beg, first);
  for (size_t k = beg + first; k < end; k += 3) {
    out += ',';
    out.append(s, k, 3);
  }
  out.append(s, end, std::string::npos);
  return out;
}

// "%'0ZW.*f" equivalent: fixed-point text with optional zero padding;
// GROUPING inserts thousands separators into the integer part.
std::string numeric_text(double val, int precision, bool grouping,
                         int zero_pad) {
  char fmt[32];
  if (zero_pad > 0) {
    std::snprintf(fmt, sizeof(fmt), "%%0%d.%df", zero_pad, precision);
  } else {
    std::snprintf(fmt, sizeof(fmt), "%%.%df", precision);
  }
  char buf[512];
  std::snprintf(buf, sizeof(buf), fmt, val);
  std::string out = buf;
  if (grouping) out = group_integer_part(out);
  return out;
}

// Number of decimal digits before the point minus one, i.e. the exponent X
// of GNU expld(val, 10, &x) (never negative for |val| < 1).
int decimal_exponent(double val) {
  int x = 0;
  val = std::abs(val);
  if (!std::isfinite(val)) return 34;
  while (val >= 10.0) {
    val /= 10.0;
    ++x;
  }
  return x;
}

// cut(1)-style --field spec: N, N-, N-M, -M, - and comma separated lists.
bool parse_field_spec(
    const std::string& spec,
    std::vector<std::pair<unsigned long, unsigned long>>& ranges,
    std::string& err) {
  static constexpr unsigned long kMax = ~0UL;
  size_t pos = 0;
  while (pos <= spec.size()) {
    const size_t comma = spec.find(',', pos);
    const std::string item = spec.substr(
        pos, comma == std::string::npos ? std::string::npos : comma - pos);
    if (item == "-") {
      ranges.emplace_back(1UL, kMax);
    } else {
      const size_t dash = item.find('-');
      const std::string lo_text =
          dash == std::string::npos ? item : item.substr(0, dash);
      const std::string hi_text =
          dash == std::string::npos ? item : item.substr(dash + 1);
      const auto to_ul = [](const std::string& t, unsigned long& v) {
        if (t.empty()) return false;
        unsigned long long parsed = 0;
        auto res = std::from_chars(t.data(), t.data() + t.size(), parsed);
        if (res.ec != std::errc() || res.ptr != t.data() + t.size() ||
            parsed > kMax) {
          return false;
        }
        v = static_cast<unsigned long>(parsed);
        return true;
      };
      unsigned long lo = 0, hi = 0;
      const bool open_hi = dash != std::string::npos && hi_text.empty();
      const bool open_lo = dash == 0;
      if (!open_lo && !to_ul(lo_text, lo)) {
        err = ::winux::i18n::format("command.numfmt.error.field_value",
                                    "invalid field value '{}'", item);
        return false;
      }
      if (!open_hi && !to_ul(hi_text, hi)) {
        err = ::winux::i18n::format("command.numfmt.error.field_value",
                                    "invalid field value '{}'", item);
        return false;
      }
      if (open_lo) lo = 1;
      if (open_hi) hi = kMax;
      if (lo == 0 || hi == 0) {
        err = ::winux::i18n::translate(
            "command.numfmt.error.field_numbered_from_1",
            "fields are numbered from 1");
        return false;
      }
      if (hi < lo) {
        err = ::winux::i18n::translate("command.numfmt.error.field_decreasing",
                                       "invalid decreasing range");
        return false;
      }
      ranges.emplace_back(lo, hi);
    }
    if (comma == std::string::npos) break;
    pos = comma + 1;
  }
  return true;
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
  const std::string from_unit_value = ctx.get<std::string>("--from-unit", "");
  const std::string to_unit_value = ctx.get<std::string>("--to-unit", "");
  const std::string unit_separator =
      ctx.get<std::string>("--unit-separator", "");
  const bool zero_terminated =
      ctx.get<bool>("--zero-terminated", false) || ctx.get<bool>("-z", false);
  const bool iec_short = ctx.get<bool>("-M", false);
  // [GNU] --to accepts only none/si/iec/iec-i ("auto" is --from-only);
  // uutils #11662.
  if (!to_unit.empty() && to_unit != "none" && to_unit != "si" &&
      to_unit != "iec" && to_unit != "iec-i") {
    safeErrorPrint("numfmt: invalid argument '" + to_unit + "' for '--to'\n");
    safeErrorPrint(::winux::i18n::format("command.numfmt.error.valid_to_args",
                                         "Valid arguments are:\n  - 'none'\n  "
                                         "- 'si'\n  - 'iec'\n  - 'iec-i'\n"));
    return 1;
  }
  bool to_si = to_unit == "si";
  bool to_iec = to_unit == "iec";
  bool to_iec_i = to_unit == "iec-i";
  std::string from_unit = ctx.get<std::string>("--from", "");
  // [GNU] --from accepts none/auto/si/iec/iec-i.
  if (!from_unit.empty() && from_unit != "none" && from_unit != "auto" &&
      from_unit != "si" && from_unit != "iec" && from_unit != "iec-i") {
    safeErrorPrint("numfmt: invalid argument '" + from_unit +
                   "' for '--from'\n");
    safeErrorPrint(
        ::winux::i18n::format("command.numfmt.error.valid_from_args",
                              "Valid arguments are:\n  - 'none'\n  - 'auto'\n"
                              "  - 'si'\n  - 'iec'\n  - 'iec-i'\n"));
    return 1;
  }
  std::string format_str = ctx.get<std::string>("--format", "");
  if (format_str.empty()) {
    format_str = ctx.get<std::string>("-f", "");
  }
  std::string delimiter = ctx.get<std::string>("--delimiter", "");
  if (delimiter.empty()) {
    delimiter = ctx.get<std::string>("-d", "");
  }
  int header = 0;
  header = ctx.get<int>("--header", 0);
  if (header < 0) {
    // bare --header (no =N): GNU defaults to one header line
    header = 1;
  }
  bool grouping = ctx.get<bool>("--grouping", false);
  const bool locale_grouping = grouping || ctx.get<bool>("-l", false);
  std::string invalid_policy = ctx.get<std::string>("--invalid", "abort");
  bool debug =
      ctx.get<bool>("--debug", false) || ctx.get<bool>("---debug", false);
  std::string round_mode = ctx.get<std::string>("--round", "");
  int padding = ctx.get<int>("--padding", ctx.get<int>("--pad", 0));
  std::string suffix = ctx.get<std::string>("--suffix", "");
  std::string field_spec = ctx.get<std::string>("--field", "");

  // [GNU] --round accepts exactly these methods.
  if (!round_mode.empty() && round_mode != "up" && round_mode != "down" &&
      round_mode != "from-zero" && round_mode != "towards-zero" &&
      round_mode != "nearest") {
    safeErrorPrint("numfmt: invalid argument '" + round_mode +
                   "' for '--round'\n");
    safeErrorPrint(::winux::i18n::format(
        "command.numfmt.error.valid_round_args",
        "Valid arguments are:\n  - 'up'\n  - 'down'\n  - 'from-zero'\n"
        "  - 'towards-zero'\n  - 'nearest'\n"));
    safeErrorPrintLn("Try 'numfmt --help' for more information.");
    return 1;
  }

  // [GNU] --invalid accepts abort/fail/warn/ignore.
  if (invalid_policy != "abort" && invalid_policy != "fail" &&
      invalid_policy != "warn" && invalid_policy != "ignore") {
    safeErrorPrint("numfmt: invalid argument '" + invalid_policy +
                   "' for '--invalid'\n");
    safeErrorPrint(::winux::i18n::format(
        "command.numfmt.error.valid_invalid_args",
        "Valid arguments are:\n  - 'abort'\n  - 'fail'\n  - 'warn'\n"
        "  - 'ignore'\n"));
    safeErrorPrintLn("Try 'numfmt --help' for more information.");
    return 1;
  }

  // [GNU] --grouping conflicts with --format and with --to.
  if (locale_grouping && !format_str.empty()) {
    safeErrorPrintLn("numfmt: " +
                     ::winux::i18n::translate(
                         "command.numfmt.error.grouping_vs_format",
                         "--grouping cannot be combined with --format"));
    return 1;
  }
  if (locale_grouping && (to_si || to_iec || to_iec_i)) {
    safeErrorPrintLn("numfmt: " + ::winux::i18n::translate(
                                      "command.numfmt.error.grouping_vs_to",
                                      "grouping cannot be combined with --to"));
    return 1;
  }

  // [GNU] cut(1)-style field selection; without --field only field 1 is
  // converted.
  std::vector<std::pair<unsigned long, unsigned long>> field_ranges;
  if (!field_spec.empty()) {
    std::string field_err;
    if (!parse_field_spec(field_spec, field_ranges, field_err)) {
      safeErrorPrintLn("numfmt: " + field_err);
      safeErrorPrintLn("Try 'numfmt --help' for more information.");
      return 1;
    }
  }
  const auto include_field = [&](unsigned long field) {
    if (field_ranges.empty()) return field == 1;
    for (const auto& [lo, hi] : field_ranges) {
      if (lo <= field && field <= hi) return true;
    }
    return false;
  };

  // [GNU] --format is decomposed, not passed to printf (fixes crashes such
  // as "--format %s" and wrong precision/width semantics).
  FmtSpec fmt_spec;
  if (!format_str.empty()) {
    if (auto err = parse_format(format_str, fmt_spec)) {
      safeErrorPrintLn("numfmt: " + *err);
      return 1;
    }
    // [GNU] the %'f flag selects grouping and conflicts with --to.
    if (fmt_spec.grouping && (to_si || to_iec || to_iec_i)) {
      safeErrorPrintLn(
          "numfmt: " +
          ::winux::i18n::translate("command.numfmt.error.grouping_vs_to",
                                   "grouping cannot be combined with --to"));
      return 1;
    }
  }

  auto debug_log = [&](const std::string& msg) {
    if (debug) {
      safeErrorPrint("numfmt: debug: " + msg + "\n");
    }
  };

  const auto unit_or_error = [&](const std::string& text) -> double {
    if (text.empty()) return 1.0;
    const auto v = unit_size(text);
    if (!v) return -1.0;
    return *v;
  };
  const double from_scale = unit_or_error(from_unit_value);
  const double to_scale = unit_or_error(to_unit_value);
  if (from_scale < 0.0 || to_scale < 0.0) {
    safeErrorPrintLn("numfmt: " +
                     ::winux::i18n::format(
                         "command.numfmt.error.invalid_unit_size",
                         "invalid unit size: '{}'",
                         from_scale < 0.0 ? from_unit_value : to_unit_value));
    return 1;
  }
  if (iec_short && to_unit.empty()) {
    to_unit = "iec-i";
    to_iec_i = true;
  }

  // [GNU] double_to_human: for scale_none the value is rounded to the
  // chosen precision and printed fixed-point; for a --to scale it is
  // normalised to [1, base), rounded at an adjusted decimal power
  // (user precision clamps to power*3), rescaled when rounding overflows,
  // and only values below 10 keep a visible decimal point.
  const int scale_kind = to_si ? 1 : to_iec ? 2 : to_iec_i ? 3 : 0;
  const auto to_human = [&](double val, int precision_used) -> std::string {
    if (scale_kind == 0) {
      const double p10 = std::pow(10.0, precision_used);
      val = apply_rounding(val * p10, round_mode) / p10;
      return numeric_text(val, precision_used,
                          fmt_spec.grouping || locale_grouping,
                          fmt_spec.zero_pad) +
             suffix;
    }
    const double base = scale_kind == 1 ? 1000.0 : 1024.0;
    int power = 0;
    if (std::isfinite(val)) {
      while (std::abs(val) >= base) {
        val /= base;
        ++power;
      }
    }
    int power_adjust = 0;
    if (fmt_spec.user_precision != -1) {
      power_adjust = std::min(power * 3, fmt_spec.user_precision);
    } else if (std::abs(val) < 10) {
      // [GNU] values below 10 keep one decimal digit, so the rounding
      // precision is adjusted before rounding.
      power_adjust = 1;
    }
    const double p10 = std::pow(10.0, power_adjust);
    val = apply_rounding(val * p10, round_mode) / p10;
    // [GNU] a rounded "999.9" can become 1000 and must rescale up.
    if (std::abs(val) >= base) {
      val /= base;
      ++power;
    }
    const int show_decimal = (val != 0) && (std::abs(val) < 10) && (power > 0);
    const int prec =
        fmt_spec.user_precision == -1 ? show_decimal : fmt_spec.user_precision;
    std::string out = numeric_text(
        val, prec, fmt_spec.grouping || locale_grouping, fmt_spec.zero_pad);
    if (power > 0) {
      out += unit_separator;
      out += suffix_power_char(power);
      if (scale_kind == 3) out += 'i';
    }
    out += suffix;
    return out;
  };

  // [GNU] --padding=N or the width inside --format; a negative width
  // left-aligns.  With no explicit width and no --delimiter the field
  // width is reused so converted numbers stay aligned (auto-padding).
  long long padding_width =
      fmt_spec.pad_width != 0 ? fmt_spec.pad_width : padding;
  const bool auto_padding = padding_width == 0 && delimiter.empty();

  bool had_invalid = false;
  const auto invalid_message = [&](NumParseStatus status,
                                   const std::string& text,
                                   const std::string& tail) -> std::string {
    switch (status) {
      case NumParseStatus::ForbiddenSuffix:
        return ::winux::i18n::format(
            "command.numfmt.error.rejecting_suffix",
            "rejecting suffix in input: '{}' (consider using --from)", text);
      case NumParseStatus::InvalidSuffixTail:
        return ::winux::i18n::format("command.numfmt.error.invalid_suffix_tail",
                                     "invalid suffix in input '{}': '{}'", text,
                                     tail);
      case NumParseStatus::MissingI:
        return ::winux::i18n::format(
            "command.numfmt.error.missing_i",
            "missing 'i' suffix in input: '{}' (e.g Ki/Mi/Gi)", text);
      case NumParseStatus::InvalidSuffix:
        return ::winux::i18n::format("command.numfmt.error.invalid_suffix",
                                     "invalid suffix in input: '{}'", text);
      case NumParseStatus::Overflow:
        return ::winux::i18n::format(
            "command.numfmt.error.value_too_large_convert",
            "value too large to be converted: '{}'", text);
      default:
        return ::winux::i18n::format("command.numfmt.error.invalid_number",
                                     "invalid number: '{}'", text);
    }
  };

  // Process one field; returns false when the "abort" policy must stop
  // the run (GNU exit status 2).
  const auto process_field = [&](const std::string& text,
                                 unsigned long field) -> bool {
    if (!include_field(field)) {
      safePrint(text);
      return true;
    }
    // [GNU] leading blanks are skipped before parsing.
    size_t nb = 0;
    while (nb < text.size() && is_field_blank(text[nb])) ++nb;
    const std::string number_text = text.substr(nb);
    // [GNU] auto-padding reuses the input field's display width when the
    // number had leading blanks or is not the first field.
    long long pad_w = padding_width;
    if (auto_padding) {
      pad_w = (nb > 0 || field > 1) ? static_cast<long long>(text.size()) : 0;
    }

    const ParsedInput pi = parse_human(number_text, from_unit, unit_separator);
    if (pi.status != NumParseStatus::Ok) {
      if (debug) {
        debug_log("failed to parse input '" + number_text + "'");
      }
      had_invalid = true;
      if (invalid_policy != "ignore") {
        safeErrorPrint("numfmt: " +
                       invalid_message(pi.status, number_text, pi.tail) + "\n");
      }
      if (invalid_policy == "abort") {
        return false;
      }
      // [GNU] warn/fail/ignore print the unconverted field unchanged.
      safePrint(text);
      return true;
    }

    double val = pi.value;
    if (from_scale != 1.0 || to_scale != 1.0) {
      val = val * from_scale / to_scale;
    }
    const int precision_used =
        fmt_spec.user_precision == -1 ? pi.precision : fmt_spec.user_precision;

    // [GNU] prepare_padded_number: without scaling, values needing more
    // than 18 digits cannot be printed; scaling tops out at 999Q.
    const int x = decimal_exponent(val);
    if (scale_kind == 0 && x + precision_used > 18) {
      had_invalid = true;
      if (invalid_policy != "ignore") {
        char value_text[64];
        std::snprintf(value_text, sizeof(value_text), "%g", val);
        if (precision_used > 0) {
          safeErrorPrint("numfmt: " +
                         ::winux::i18n::format(
                             "command.numfmt.error.value_precision_too_large",
                             "value/precision too large to be printed: '{}/{}' "
                             "(consider using --to)",
                             value_text, precision_used) +
                         "\n");
        } else {
          safeErrorPrint(
              "numfmt: " +
              ::winux::i18n::format(
                  "command.numfmt.error.value_too_large_print",
                  "value too large to be printed: '{}' (consider using --to)",
                  value_text) +
              "\n");
        }
      }
      if (invalid_policy == "abort") return false;
      safePrint(text);
      return true;
    }
    if (x > 32) {
      had_invalid = true;
      if (invalid_policy != "ignore") {
        char value_text[64];
        std::snprintf(value_text, sizeof(value_text), "%g", val);
        safeErrorPrint(
            "numfmt: " +
            ::winux::i18n::format(
                "command.numfmt.error.value_too_large_scale",
                "value too large to be printed: '{}' (cannot handle values "
                "> 999Q)",
                value_text) +
            "\n");
      }
      if (invalid_policy == "abort") return false;
      safePrint(text);
      return true;
    }

    std::string out = to_human(val, precision_used);
    // [GNU] padding pads the whole number+unit buffer; a negative width
    // appends the spaces instead.
    if (pad_w > 0 && static_cast<long long>(out.size()) < pad_w) {
      out.insert(0, static_cast<size_t>(pad_w - out.size()), ' ');
    } else if (pad_w < 0 && static_cast<long long>(out.size()) < -pad_w) {
      out.append(static_cast<size_t>(-pad_w - out.size()), ' ');
    }
    if (debug) {
      debug_log("converted '" + number_text + "' -> '" + out + "'");
    }
    safePrint(fmt_spec.prefix);
    safePrint(out);
    safePrint(fmt_spec.suffix);
    return true;
  };

  int line_num = 0;
  auto process_line = [&](const std::string& line, bool is_stdin) -> bool {
    // [GNU] --header only applies to standard input.
    if (is_stdin && line_num < header) {
      safePrint(line);
      safePrint(zero_terminated ? std::string(1, '\0') : std::string("\n"));
      ++line_num;
      return true;
    }
    // [GNU] process_line: with --delimiter the line is split on that
    // character; otherwise fields are runs of non-blank text (leading
    // blanks stay part of the field) and a single blank is printed
    // between fields.
    unsigned long field = 0;
    size_t pos = 0;
    while (true) {
      ++field;
      const size_t fstart = pos;
      size_t fend = pos;
      if (!delimiter.empty()) {
        fend = line.find(delimiter, fstart);
        if (fend == std::string::npos) fend = line.size();
      } else {
        while (fend < line.size() && is_field_blank(line[fend])) ++fend;
        while (fend < line.size() && !is_field_blank(line[fend])) ++fend;
      }
      const std::string field_text = line.substr(fstart, fend - fstart);
      if (!process_field(field_text, field)) return false;
      if (fend >= line.size()) break;
      safePrint(delimiter.empty() ? std::string(" ") : delimiter);
      pos = fend + (delimiter.empty() ? 1 : delimiter.size());
    }
    safePrint(zero_terminated ? std::string(1, '\0') : std::string("\n"));
    ++line_num;
    return true;
  };

  const bool reading_stdin = ctx.positionals.empty();
  if (!reading_stdin) {
    for (const auto& arg : ctx.positionals) {
      if (!process_line(std::string(arg), false)) return 2;
    }
  } else {
    std::string input;
    input.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
    std::istringstream iss(input);
    std::string line;
    const char delimiter_char = zero_terminated ? '\0' : '\n';
    while (std::getline(iss, line, delimiter_char)) {
      if (!process_line(line, true)) return 2;
    }
  }

  // [GNU] exit status: abort dies on the first bad value (2); fail reports
  // every bad value and still exits 2; warn and ignore succeed.
  if (had_invalid && invalid_policy == "fail") return 2;
  return 0;
}
