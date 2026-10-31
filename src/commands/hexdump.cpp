// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for hexdump.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr HEXDUMP_OPTIONS = std::array{
    // [GNU]
    OPTION("-b", "", "one-byte octal display"),
    // [GNU]
    OPTION("-c", "", "one-byte character display"),
    // [GNU]
    OPTION("-C", "", "canonical hex+ASCII display"),
    // [GNU]
    OPTION("-d", "", "two-byte decimal display"),
    // [GNU]
    OPTION("-o", "", "two-byte octal display"),
    // [GNU]
    OPTION("-x", "", "two-byte hex display"),
    // [GNU]
    OPTION("-e", "", "format string", STRING_TYPE),
    // [GNU]
    OPTION("-f", "", "format file", STRING_TYPE),
    // [GNU]
    OPTION("-n", "", "interpret only LENGTH bytes of input", STRING_TYPE),
    // [GNU]
    OPTION("-s", "", "skip offset bytes from the beginning", STRING_TYPE),
    // [GNU]
    OPTION("-v", "", "display all input, no squeeze"),
    // [GNU] -L, --color: interpret color formatting specifiers
    OPTION("-L", "--color", "interpret color formatting specifiers")};

namespace hexdump_pipeline {
namespace cp = core::pipeline;

enum class DisplayMode {
  Octal1,
  Char,
  Canonical,
  Decimal2,
  Octal2,
  DefaultHex2,
  Hex2
};

// ---------------------------------------------------------------------------
// Format-string engine (-e/-f, #1373)
//
// Each -e operand (and each line of a -f file) is one format string: a
// sequence of format units
//   [count][/byte_count] "format"
// count is a decimal integer or '*' (repeat until input is exhausted);
// byte_count is the number of input bytes one application of the quoted
// format consumes. Default byte counts follow the reference: 1 for
// %c/%_c/%_p/%_u, 4 for the integer conversions. Inside the quotes,
// conversions are printf-style (%d %i %o %u %x %X %c %s, with
// flags/width/precision) plus the hexdump specials %_Ad/%_Ao/%_Ax
// (input offset, consumes nothing), %_p (printable character or '.')
// and %_u (character with C escapes).
//
// Input is manipulated in BLOCKS: a block is the largest amount of
// data specified by any one format string, and EVERY format string is
// applied to each block starting at the block's first byte (this is
// what lets a separate -e '"%_p..."' string re-read the bytes a hex
// string just printed). A format string interpreting less than a block
// whose last unit consumes input without a specified iteration count
// has that count incremented until the block is processed.
// ---------------------------------------------------------------------------

enum class ConvKind { Int, Char, Str, Addr, PrintOrDot, Escaped };

struct FmtItem {
  bool is_conversion = false;
  std::string literal;
  ConvKind kind = ConvKind::Int;
  char conv = 0;               // printf conversion letter (or A base letter)
  std::string flags;           // printf flags/width/precision text
};

struct FormatUnit {
  long count = 1;              // -1 = '*' (until end of input)
  bool count_specified = false;
  int byte_count = 0;          // 0 = derive from the conversion kind
  std::vector<FmtItem> items;
  bool consumes_input = false;
};

struct FormatString {
  std::vector<FormatUnit> units;
};

struct FormatProgram {
  std::vector<FormatString> strings;
};

auto decode_format_escapes(std::string_view raw)
    -> std::expected<std::string, std::string> {
  std::string out;
  for (size_t i = 0; i < raw.size(); ++i) {
    char c = raw[i];
    if (c != '\\') {
      out.push_back(c);
      continue;
    }
    if (++i >= raw.size()) return std::unexpected("trailing backslash");
    switch (raw[i]) {
      case 'a': out.push_back('\a'); break;
      case 'b': out.push_back('\b'); break;
      case 'f': out.push_back('\f'); break;
      case 'n': out.push_back('\n'); break;
      case 'r': out.push_back('\r'); break;
      case 't': out.push_back('\t'); break;
      case 'v': out.push_back('\v'); break;
      case '\\': out.push_back('\\'); break;
      case '"': out.push_back('"'); break;
      case '\'': out.push_back('\''); break;
      default: return std::unexpected("unknown escape sequence");
    }
  }
  return out;
}

// Parse the inside of one quoted format string into items.
auto parse_format_body(std::string_view body)
    -> std::expected<std::vector<FmtItem>, std::string> {
  auto decoded = decode_format_escapes(body);
  if (!decoded) return std::unexpected(decoded.error());
  const std::string& s = *decoded;

  std::vector<FmtItem> items;
  std::string literal;
  auto flush_literal = [&]() {
    if (!literal.empty()) {
      FmtItem item;
      item.literal = std::move(literal);
      literal.clear();
      items.push_back(std::move(item));
    }
  };

  for (size_t i = 0; i < s.size();) {
    if (s[i] != '%') {
      literal.push_back(s[i++]);
      continue;
    }
    if (i + 1 < s.size() && s[i + 1] == '%') {
      literal.push_back('%');
      i += 2;
      continue;
    }
    flush_literal();
    FmtItem item;
    item.is_conversion = true;
    ++i;  // consume '%'
    // flags, width, precision
    std::string spec;
    while (i < s.size() &&
           std::string_view("-+ #0'").find(s[i]) != std::string_view::npos) {
      spec.push_back(s[i++]);
    }
    while (i < s.size() &&
           std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
      spec.push_back(s[i++]);
    }
    if (i < s.size() && s[i] == '.') {
      spec.push_back(s[i++]);
      while (i < s.size() &&
             std::isdigit(static_cast<unsigned char>(s[i])) != 0) {
        spec.push_back(s[i++]);
      }
    }
    item.flags = spec;
    if (i >= s.size()) return std::unexpected("missing conversion character");
    if (s[i] == '_') {
      if (i + 1 >= s.size()) return std::unexpected("bad %_ conversion");
      char special = s[i + 1];
      if (special == 'A') {
        if (i + 2 >= s.size() ||
            std::string_view("dox").find(s[i + 2]) == std::string_view::npos) {
          return std::unexpected("bad %_A conversion");
        }
        item.kind = ConvKind::Addr;
        item.conv = s[i + 2];
        i += 3;
      } else if (special == 'p') {
        item.kind = ConvKind::PrintOrDot;
        item.conv = 'p';
        i += 2;
      } else if (special == 'u') {
        item.kind = ConvKind::Escaped;
        item.conv = 'u';
        i += 2;
      } else if (special == 'c') {
        // [GNU] %_c is the escaped-character conversion used by the
        // oracle's own -c format string (hexdump.c, the -c fs); it
        // renders through the same escape table as %_u.
        item.kind = ConvKind::Escaped;
        item.conv = 'c';
        i += 2;
      } else {
        return std::unexpected("bad %_ conversion");
      }
    } else {
      char c = s[i];
      switch (c) {
        case 'd': case 'i': case 'o': case 'u': case 'x': case 'X':
          item.kind = ConvKind::Int;
          break;
        case 'c':
          item.kind = ConvKind::Char;
          break;
        case 's':
          item.kind = ConvKind::Str;
          break;
        default:
          return std::unexpected("bad conversion character");
      }
      item.conv = c;
      ++i;
    }
    items.push_back(std::move(item));
  }
  flush_literal();
  return items;
}

// Parse one format string (one -e operand, or one line of a -f file).
auto parse_format_string(std::string_view text, FormatProgram& program)
    -> std::expected<void, std::string> {
  size_t i = 0;
  auto skip_ws = [&]() {
    while (i < text.size() &&
           std::isspace(static_cast<unsigned char>(text[i])) != 0) {
      ++i;
    }
  };
  FormatString format_string;
  skip_ws();
  while (i < text.size()) {
    FormatUnit unit;
    // Optional repeat count.
    if (text[i] == '*') {
      unit.count = -1;
      unit.count_specified = true;
      ++i;
    } else if (std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
      long count = 0;
      while (i < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
        count = count * 10 + (text[i] - '0');
        ++i;
      }
      unit.count = count;
      unit.count_specified = true;
    }
    skip_ws();
    // Optional byte count.
    if (i < text.size() && text[i] == '/') {
      ++i;
      skip_ws();
      if (i >= text.size() ||
          std::isdigit(static_cast<unsigned char>(text[i])) == 0) {
        return std::unexpected("bad byte count");
      }
      int bcnt = 0;
      while (i < text.size() &&
             std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
        bcnt = bcnt * 10 + (text[i] - '0');
        ++i;
      }
      if (bcnt <= 0 || bcnt > 8) {
        return std::unexpected("byte count out of range");
      }
      unit.byte_count = bcnt;
      skip_ws();
    }
    if (i >= text.size() || text[i] != '"') {
      return std::unexpected("missing format string quote");
    }
    ++i;
    const size_t body_start = i;
    bool closed = false;
    while (i < text.size()) {
      if (text[i] == '\\') {
        i += 2;
        continue;
      }
      if (text[i] == '"') {
        closed = true;
        break;
      }
      ++i;
    }
    if (!closed) return std::unexpected("unterminated format string");
    auto items = parse_format_body(text.substr(body_start, i - body_start));
    if (!items) return std::unexpected(items.error());
    unit.items = std::move(*items);
    ++i;  // closing quote
    size_t consuming = 0;
    for (const auto& item : unit.items) {
      if (item.is_conversion && item.kind != ConvKind::Addr) {
        unit.consumes_input = true;
        ++consuming;
        // Reference byte counts: integer conversions accept 1, 2, 4
        // or 8 only.
        if (item.kind == ConvKind::Int && unit.byte_count > 0 &&
            unit.byte_count != 1 && unit.byte_count != 2 &&
            unit.byte_count != 4 && unit.byte_count != 8) {
          return std::unexpected("bad byte count for integer conversion");
        }
      }
    }
    // A byte count may not be shared by multiple consuming conversions
    // (all but one conversion must be %_A in that case).
    if (unit.byte_count > 0 && consuming > 1) {
      return std::unexpected(
          "byte count specified with multiple conversions");
    }
    format_string.units.push_back(std::move(unit));
    skip_ws();
  }
  if (!format_string.units.empty()) {
    program.strings.push_back(std::move(format_string));
  }
  return {};
}

// Load a -f format file: each line is a format string.
auto load_format_file(const std::string& path, FormatProgram& program)
    -> std::expected<void, std::string> {
  std::ifstream file(native_path::normalize_api_operand(path));
  if (!file) {
    // [GNU] err shape from addfile (hexdump-parse.c:85).
    return std::unexpected("can't read " + path +
                           ": No such file or directory");
  }
  std::string line;
  while (std::getline(file, line)) {
    // [GNU] addfile (hexdump-parse.c:88-96) skips blank lines and lines
    // whose first non-space character is '#'; every other line is
    // parsed verbatim — there are no trailing comments.
    const size_t first = line.find_first_not_of(" \t\r\f\v");
    if (first == std::string::npos || line[first] == '#') continue;
    auto parsed = parse_format_string(line, program);
    if (!parsed) return std::unexpected(parsed.error());
  }
  return {};
}

struct Config {
  DisplayMode mode = DisplayMode::DefaultHex2;
  size_t length = 0;  // 0 = all
  size_t skip = 0;
  bool no_squeeze = false;
  bool has_formats = false;
  FormatProgram program;
  SmallVector<std::string, 64> files;
};

// [GNU] -n/-s values go through strtosize (hexdump.c parse_args calls
// strtosize_or_err): a strtoumax base-0 integer (0x hex, leading-0
// octal), an optional fraction, and an optional size suffix — K/M/G/
// T/P/E scale by powers of 1024, the XB forms by powers of 1000 and
// the XiB forms by powers of 1024 (lib/strutils.c parse_size).  On
// failure strtosize_or_err reports through err(), so the message
// carries the errno text: EINVAL for structural errors, ERANGE for
// range errors (parse_size returns -EINVAL / -errno respectively);
// the error strings below mirror that split.
auto parse_size_value(std::string_view text)
    -> std::expected<size_t, const char*> {
  if (text.empty()) return std::unexpected("Invalid argument");
  size_t pos = 0;
  if (text[pos] == '+') ++pos;

  int base = 10;
  if (text.size() - pos > 2 && text[pos] == '0' &&
      (text[pos + 1] == 'x' || text[pos + 1] == 'X')) {
    base = 16;
    pos += 2;
  } else if (text.size() - pos > 1 && text[pos] == '0') {
    base = 8;
  }

  unsigned long long value = 0;
  const size_t digits_begin = pos;
  for (; pos < text.size(); ++pos) {
    const char c = text[pos];
    int digit = -1;
    if (c >= '0' && c <= '9') digit = c - '0';
    if (base == 16 && c >= 'a' && c <= 'f') digit = c - 'a' + 10;
    if (base == 16 && c >= 'A' && c <= 'F') digit = c - 'A' + 10;
    if (digit < 0 || digit >= base) break;
    if (value >
        (std::numeric_limits<unsigned long long>::max() -
         static_cast<unsigned long long>(digit)) /
            static_cast<unsigned long long>(base)) {
      return std::unexpected("Numerical result out of range");
    }
    value = value * static_cast<unsigned long long>(base) +
            static_cast<unsigned long long>(digit);
  }
  if (pos == digits_begin) return std::unexpected("Invalid argument");

  unsigned long long frac_num = 0;
  unsigned long long frac_denom = 1;
  bool has_frac = false;
  if (pos < text.size() && text[pos] == '.') {
    size_t fpos = pos + 1;
    unsigned long long f = 0;
    unsigned long long denom = 1;
    bool any = false;
    while (fpos < text.size() && text[fpos] >= '0' && text[fpos] <= '9') {
      f = f * 10 + static_cast<unsigned long long>(text[fpos] - '0');
      denom *= 10;
      ++fpos;
      any = true;
    }
    if (!any) return std::unexpected("Invalid argument");
    has_frac = true;
    frac_num = f;
    frac_denom = denom;
    pos = fpos;
    // parse_size: a fraction without a suffix is an error.
    if (pos >= text.size()) return std::unexpected("Invalid argument");
  }
  if (pos >= text.size()) return static_cast<size_t>(value);

  const std::string_view suffix = text.substr(pos);
  static constexpr std::string_view kLetters = "KMGTPE";
  const size_t letter = kLetters.find(static_cast<char>(
      std::toupper(static_cast<unsigned char>(suffix.front()))));
  if (letter == std::string_view::npos)
    return std::unexpected("Invalid argument");
  const int power = static_cast<int>(letter) + 1;
  unsigned long long scale_base = 1024;
  if (suffix.size() == 3 && suffix[1] == 'i' &&
      (suffix[2] == 'B' || suffix[2] == 'b')) {
    scale_base = 1024;  // XiB
  } else if (suffix.size() == 2 && (suffix[1] == 'B' || suffix[1] == 'b')) {
    scale_base = 1000;  // XB
  } else if (suffix.size() != 1) {
    return std::unexpected("Invalid argument");
  }

  unsigned long long scale = 1;
  for (int i = 0; i < power; ++i) {
    if (scale > std::numeric_limits<unsigned long long>::max() / scale_base) {
      return std::unexpected("Numerical result out of range");
    }
    scale *= scale_base;
  }
  if (value > std::numeric_limits<unsigned long long>::max() / scale) {
    return std::unexpected("Numerical result out of range");
  }
  unsigned long long result = value * scale;
  if (has_frac) {
    result += (scale / frac_denom) * frac_num +
              ((scale % frac_denom) * frac_num) / frac_denom;
  }
  return static_cast<size_t>(result);
}

auto build_config(const CommandContext<HEXDUMP_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  // Determine display mode
  if (ctx.get<bool>("-b", false))
    cfg.mode = DisplayMode::Octal1;
  else if (ctx.get<bool>("-c", false))
    cfg.mode = DisplayMode::Char;
  else if (ctx.get<bool>("-C", false))
    cfg.mode = DisplayMode::Canonical;
  else if (ctx.get<bool>("-d", false))
    cfg.mode = DisplayMode::Decimal2;
  else if (ctx.get<bool>("-o", false))
    cfg.mode = DisplayMode::Octal2;
  else if (ctx.get<bool>("-x", false))
    cfg.mode = DisplayMode::Hex2;

  cfg.no_squeeze = ctx.get<bool>("-v", false);

  // [GNU] -e/-f supply the display formats (the core of the tool, #1373):
  // -e was rejected outright and -f was silently ignored. Both are now
  // honored, in argv order, via the parser's occurrence list.
  for (const auto& occurrence : ctx.string_occurrences({"-e", "-f"})) {
    cfg.has_formats = true;
    if (occurrence.short_name == "-f") {
      auto loaded = load_format_file(occurrence.value, cfg.program);
      if (!loaded) return std::unexpected(loaded.error());
    } else {
      auto parsed = parse_format_string(occurrence.value, cfg.program);
      if (!parsed) {
        return std::unexpected("bad format string: " + parsed.error());
      }
    }
  }
  if (cfg.has_formats) {
    bool any_consumption = false;
    for (const auto& format_string : cfg.program.strings) {
      for (const auto& unit : format_string.units) {
        if (unit.consumes_input) any_consumption = true;
      }
    }
    if (!any_consumption) {
      return std::unexpected("format string consumes no input");
    }
  }

  auto length_opt = ctx.get<std::string>("-n", "");
  if (!length_opt.empty()) {
    auto parsed = parse_size_value(length_opt);
    if (!parsed) {
      // [GNU] strtosize_or_err reports through err() (strutils.c:
      // strtosize_or_err): the quoted value is followed by the errno
      // text, e.g. "failed to parse length: 'bogus': Invalid
      // argument".
      return std::unexpected("failed to parse length: '" + length_opt +
                             "': " + parsed.error());
    }
    cfg.length = *parsed;
  }

  auto skip_opt = ctx.get<std::string>("-s", "");
  if (!skip_opt.empty()) {
    auto parsed = parse_size_value(skip_opt);
    if (!parsed) {
      return std::unexpected("failed to parse offset: '" + skip_opt +
                             "': " + parsed.error());
    }
    cfg.skip = *parsed;
  }

  // [native-glob] #1374: file operands expand through the shared helper
  // in WINUX_SHELL_GLOB=native mode, like cat's operands do.
  for (const auto& pos : ctx.positionals) {
    for (auto& file : expand_file_operand(pos)) {
      cfg.files.push_back(std::move(file));
    }
  }

  return cfg;
}

// --- Built-in display modes -------------------------------------------------
// Each printer renders one 16-byte line at a time so identical consecutive
// lines can be squeezed to a single "*" (#1373), and returns the rendered
// lines through `emit`.

template <typename Emit>
auto dump_lines(const std::vector<uint8_t>& data, size_t base_offset,
                bool no_squeeze, Emit&& emit_line) -> void {
  // Squeeze compares the input DATA of each 16-byte row (the reference
  // compares input blocks); the rendered lines can never be equal
  // because their offsets differ.
  std::vector<uint8_t> previous;
  bool star_printed = false;
  bool have_previous = false;
  for (size_t i = 0; i < data.size(); i += 16) {
    const size_t chunk_len = std::min<size_t>(16, data.size() - i);
    std::vector<uint8_t> chunk(data.begin() +
                                   static_cast<std::ptrdiff_t>(i),
                               data.begin() +
                                   static_cast<std::ptrdiff_t>(i + chunk_len));
    if (!no_squeeze && have_previous && chunk_len == 16 &&
        chunk == previous) {
      if (!star_printed) {
        safePrintLn("*");
        star_printed = true;
      }
      continue;
    }
    safePrintLn(emit_line(i));
    previous = std::move(chunk);
    have_previous = chunk_len == 16;
    star_printed = false;
  }
  (void)base_offset;
}

auto line_canonical(const std::vector<uint8_t>& data, size_t base_offset,
                      size_t i) -> std::string {
  std::string line = std::format("{:08x}  ", base_offset + i);
  for (size_t j = 0; j < 16; ++j) {
    if (i + j < data.size()) {
      line += std::format("{:02x}", data[i + j]);
    } else {
      line += "  ";
    }
    if (j == 7) line += " ";
    if (j < 15) line += " ";
  }
  line += "  |";
  for (size_t j = 0; j < 16 && i + j < data.size(); ++j) {
    unsigned char ch = data[i + j];
    line += (ch >= 32 && ch <= 126) ? static_cast<char>(ch) : '.';
  }
  line += "|";
  return line;
}

// One 2-byte-unit line shared by -x/default/-d/-o: a trailing odd byte is
// printed as a zero-padded unit (#1373 — it used to vanish).
template <typename FormatUnit>
auto line_units2(const std::vector<uint8_t>& data, size_t base_offset,
                 size_t i, bool padded_words, FormatUnit&& format_unit)
    -> std::string {
  std::string line = std::format("{:07x} ", base_offset + i);
  const size_t chunk = std::min<size_t>(16, data.size() - i);
  // [GNU] a partial block is blank-padded to the full row width: bytes
  // past the end of the block print as bpad blanks of the conversion's
  // field width (hexdump-display.c:215-233), and the trailing space of
  // the row's last unit is suppressed (the nospace rule,
  // hexdump-parse.c:474), so every row of a mode has the same width.
  const std::string blank_unit(
      padded_words ? format_unit(0, true).size() : size_t{4}, ' ');
  for (size_t j = 0; j < 16; j += 2) {
    const bool present = j < chunk;
    uint16_t val = 0;
    if (present) {
      val = static_cast<uint16_t>(data[i + j]);
      if (j + 1 < chunk) {
        val = static_cast<uint16_t>(val |
                                    (static_cast<uint16_t>(data[i + j + 1])
                                     << 8));
      }
    }
    if (padded_words) {
      line += present ? format_unit(val, true) : blank_unit;
    } else {
      if (j > 0) line += " ";
      line += present ? format_unit(val, false) : blank_unit;
    }
  }
  if (padded_words && !line.empty() && line.back() == ' ') line.pop_back();
  return line;
}

auto line_octal1(const std::vector<uint8_t>& data, size_t base_offset,
                 size_t i) -> std::string {
  std::string line = std::format("{:07x} ", base_offset + i);
  for (size_t j = 0; j < 16; ++j) {
    if (i + j < data.size()) {
      line += std::format("{:03o} ", data[i + j]);
    } else {
      line += "    ";
    }
  }
  // [GNU] the row is fixed at 71 columns: 16 slots of 4 with the final
  // trailing space trimmed (#1133).
  if (!line.empty() && line.back() == ' ') line.pop_back();
  return line;
}

auto line_char(const std::vector<uint8_t>& data, size_t base_offset, size_t i)
    -> std::string {
  std::string line = std::format("{:07x} ", base_offset + i);
  for (size_t j = 0; j < 16; ++j) {
    if (i + j >= data.size()) {
      line += "    ";
      continue;
    }
    unsigned char ch = data[i + j];
    if (ch == 0)
      line += " \\0 ";
    else if (ch == 7)
      line += " \\a ";
    else if (ch == 8)
      line += " \\b ";
    else if (ch == 9)
      line += " \\t ";
    else if (ch == 10)
      line += " \\n ";
    else if (ch == 11)
      line += " \\v ";
    else if (ch == 12)
      line += " \\f ";
    else if (ch == 13)
      line += " \\r ";
    else if (ch >= 32 && ch <= 126)
      line += std::format("  {} ", static_cast<char>(ch));
    else
      line += std::format("{:03o} ", ch);
  }
  // [GNU] fixed 71-column row: final trailing space trimmed (#1133).
  if (!line.empty() && line.back() == ' ') line.pop_back();
  return line;
}

auto print_builtin(const std::vector<uint8_t>& data, size_t base_offset,
                   const Config& cfg) -> void {
  switch (cfg.mode) {
    case DisplayMode::Canonical:
      dump_lines(data, base_offset, cfg.no_squeeze, [&](size_t i) {
        return line_canonical(data, base_offset, i);
      });
      break;
    case DisplayMode::DefaultHex2:
      dump_lines(data, base_offset, cfg.no_squeeze, [&](size_t i) {
        return line_units2(data, base_offset, i, false,
                           [](uint16_t v, bool) {
                             return std::format("{:04x}", v);
                           });
      });
      break;
    case DisplayMode::Hex2:
      dump_lines(data, base_offset, cfg.no_squeeze, [&](size_t i) {
        return line_units2(data, base_offset, i, true,
                           [](uint16_t v, bool) {
                             return std::format("   {:04x} ", v);
                           });
      });
      break;
    case DisplayMode::Octal2:
      dump_lines(data, base_offset, cfg.no_squeeze, [&](size_t i) {
        return line_units2(data, base_offset, i, true,
                           [](uint16_t v, bool) {
                             // [GNU] the -o unit is " %06o " (8 wide,
                             // hexdump.c the -o fs), not 9.
                             return std::format(" {:06o} ", v);
                           });
      });
      break;
    case DisplayMode::Decimal2:
      dump_lines(data, base_offset, cfg.no_squeeze, [&](size_t i) {
        return line_units2(data, base_offset, i, true,
                           [](uint16_t v, bool) {
                             return std::format("  {:05d} ", v);
                           });
      });
      break;
    case DisplayMode::Octal1:
      dump_lines(data, base_offset, cfg.no_squeeze, [&](size_t i) {
        return line_octal1(data, base_offset, i);
      });
      break;
    case DisplayMode::Char:
      dump_lines(data, base_offset, cfg.no_squeeze, [&](size_t i) {
        return line_char(data, base_offset, i);
      });
      break;
  }
  if (!data.empty()) {
    if (cfg.mode == DisplayMode::Canonical) {
      safePrintLn(std::format("{:08x}", base_offset + data.size()));
    } else {
      safePrintLn(std::format("{:07x}", base_offset + data.size()));
    }
  }
}

// --- Format-program execution ----------------------------------------------

auto unit_byte_count(const FormatUnit& unit, const FmtItem& item) -> int {
  if (unit.byte_count > 0) return unit.byte_count;
  // BSD convention: character-shaped conversions consume 1 byte,
  // numeric conversions 4.
  switch (item.kind) {
    case ConvKind::Char:
    case ConvKind::Str:
    case ConvKind::PrintOrDot:
    case ConvKind::Escaped:
      return 1;
    default:
      return 4;
  }
}

auto format_int_conversion(const FmtItem& item, uint64_t bits, int bcnt)
    -> std::string {
  char fmtbuf[64];
  const bool is_signed = item.conv == 'd' || item.conv == 'i';
  std::string spec = "%" + item.flags;
  if (bcnt >= 8) spec += "ll";
  spec.push_back(item.conv);
  std::snprintf(fmtbuf, sizeof(fmtbuf), "%s", spec.c_str());
  char out[128];
  if (bcnt >= 8) {
    if (is_signed) {
      std::snprintf(out, sizeof(out), fmtbuf,
                    static_cast<long long>(static_cast<int64_t>(bits)));
    } else {
      std::snprintf(out, sizeof(out), fmtbuf,
                    static_cast<unsigned long long>(bits));
    }
  } else {
    // Truncate/sign-extend to the consumed width like the C cast the
    // reference implementation performs.
    const int width = bcnt * 8;
    uint32_t truncated =
        width >= 32 ? static_cast<uint32_t>(bits)
                    : static_cast<uint32_t>(bits & ((1u << width) - 1u));
    if (is_signed) {
      int32_t signed_value = static_cast<int32_t>(truncated);
      if (width < 32 && (truncated & (1u << (width - 1))) != 0) {
        signed_value = static_cast<int32_t>(
            static_cast<int64_t>(truncated) - (int64_t{1} << width));
      }
      std::snprintf(out, sizeof(out), fmtbuf, static_cast<int>(signed_value));
    } else {
      std::snprintf(out, sizeof(out), fmtbuf,
                    static_cast<unsigned int>(truncated));
    }
  }
  return out;
}

auto escaped_char(uint8_t byte) -> std::string {
  switch (byte) {
    case 0: return "\\0";
    case 7: return "\\a";
    case 8: return "\\b";
    case 9: return "\\t";
    case 10: return "\\n";
    case 11: return "\\v";
    case 12: return "\\f";
    case 13: return "\\r";
    default: break;
  }
  if (byte >= 32 && byte <= 126) return std::string(1, static_cast<char>(byte));
  return std::format("{:03o}", byte);
}

// [GNU] printf-style field padding for the character/string/escaped
// conversions: the width parsed from the conversion spec (e.g. the 3 in
// "%3_c") pads with spaces, right-aligned unless '-' was given.  The
// integer conversions build their own printf spec instead.
auto pad_field(std::string text, const FmtItem& item) -> std::string {
  const std::string& spec = item.flags;
  size_t i = 0;
  bool left = false;
  while (i < spec.size() &&
         std::string_view("-+ #0'").find(spec[i]) != std::string_view::npos) {
    if (spec[i] == '-') left = true;
    ++i;
  }
  int width = 0;
  while (i < spec.size() &&
         std::isdigit(static_cast<unsigned char>(spec[i])) != 0) {
    width = width * 10 + (spec[i] - '0');
    ++i;
  }
  if (width <= static_cast<int>(text.size())) return text;
  const std::string padding(
      static_cast<size_t>(width) - text.size(), ' ');
  return left ? text + padding : padding + text;
}

// Apply one format unit once at `cursor`; appends rendered text to `out`
// and advances the cursor past the consumed bytes. Returns false when a
// consuming application cannot start (input exhausted).
auto apply_unit(const FormatUnit& unit, const std::vector<uint8_t>& data,
                size_t base_offset, size_t& cursor, std::string& out)
    -> bool {
  size_t local = cursor;
  std::string rendered;
  bool first_consume = true;
  for (const auto& item : unit.items) {
    if (!item.is_conversion) {
      rendered += item.literal;
      continue;
    }
    if (item.kind == ConvKind::Addr) {
      const uint64_t offset = base_offset + local;
      char fmtbuf[64];
      std::snprintf(fmtbuf, sizeof(fmtbuf), "%%%sll%c", item.flags.c_str(),
                    item.conv);
      char buf[64];
      if (item.conv == 'd') {
        std::snprintf(buf, sizeof(buf), fmtbuf,
                      static_cast<long long>(offset));
      } else {
        std::snprintf(buf, sizeof(buf), fmtbuf,
                      static_cast<unsigned long long>(offset));
      }
      rendered += buf;
      continue;
    }
    const int bcnt = unit_byte_count(unit, item);
    // An application fails only when its FIRST consuming conversion has
    // no input left; later conversions in the same application render
    // zero-padded, matching the reference's trailing-partial-block
    // behavior.
    if (local >= data.size() && first_consume) return false;
    first_consume = false;
    const size_t avail =
        local >= data.size()
            ? 0
            : std::min<size_t>(static_cast<size_t>(bcnt), data.size() - local);
    switch (item.kind) {
      case ConvKind::Int: {
        uint64_t bits = 0;
        for (size_t k = 0; k < avail; ++k) {
          bits |= static_cast<uint64_t>(data[local + k]) << (8 * k);
        }
        rendered += format_int_conversion(item, bits, bcnt);
        break;
      }
      case ConvKind::Char: {
        // [GNU] %c writes the byte itself (printf %c semantics);
        // non-printing bytes pass through raw instead of becoming '.'.
        std::string segment;
        for (size_t k = 0; k < avail; ++k) {
          segment.push_back(static_cast<char>(data[local + k]));
        }
        rendered += pad_field(std::move(segment), item);
        break;
      }
      case ConvKind::Str: {
        // [GNU] %s writes the raw bytes up to the first NUL (printf %s
        // semantics over the consumed byte count).
        std::string segment;
        for (size_t k = 0; k < avail; ++k) {
          const uint8_t byte = data[local + k];
          if (byte == 0) break;
          segment.push_back(static_cast<char>(byte));
        }
        rendered += pad_field(std::move(segment), item);
        break;
      }
      case ConvKind::PrintOrDot: {
        if (avail > 0) {
          uint8_t byte = data[local];
          rendered += (byte >= 32 && byte <= 126)
                          ? std::string(1, static_cast<char>(byte))
                          : ".";
        }
        break;
      }
      case ConvKind::Escaped: {
        if (avail > 0) {
          rendered += pad_field(escaped_char(data[local]), item);
        }
        break;
      }
      case ConvKind::Addr:
        break;  // handled above
    }
    local += avail;
  }
  cursor = local;
  out += rendered;
  return true;
}

// Bytes one application of a unit consumes (0 for literal/address-only
// units and for units whose conversions are all %_A).
auto unit_consumption(const FormatUnit& unit) -> size_t {
  size_t total = 0;
  for (const auto& item : unit.items) {
    if (item.is_conversion && item.kind != ConvKind::Addr) {
      total += static_cast<size_t>(unit_byte_count(unit, item));
    }
  }
  return total;
}

// Bytes one application of a whole format string consumes; a starred
// unit makes the string consume the rest of the input (nullopt).
auto string_consumption(const FormatString& format_string)
    -> std::optional<size_t> {
  size_t total = 0;
  for (const auto& unit : format_string.units) {
    if (unit.count < 0) return std::nullopt;
    total += unit_consumption(unit) * static_cast<size_t>(unit.count);
  }
  return total;
}

// Render one format string over one block. `block_offset` is the
// input-stream offset of block[0] (for %_A).
auto render_string(const FormatString& format_string,
                     const std::vector<uint8_t>& block, size_t block_offset,
                     size_t block_size, std::string& out) -> void {
  size_t cursor = 0;
  const size_t total_units = format_string.units.size();
  for (size_t unit_index = 0; unit_index < total_units; ++unit_index) {
    const auto& unit = format_string.units[unit_index];
    const bool is_last = unit_index + 1 == total_units;
    if (unit.count < 0) {
      while (cursor < block.size()) {
        const size_t before = cursor;
        if (!apply_unit(unit, block, block_offset, cursor, out)) break;
        if (cursor == before) break;  // non-consuming unit: apply once
      }
      continue;
    }
    // Reference rule: a string interpreting less than a block whose
    // LAST unit consumes input with no specified iteration count has
    // that count incremented until the block is processed.
    const bool auto_repeat = is_last && unit.consumes_input &&
                             !unit.count_specified &&
                             string_consumption(format_string).value_or(0) <
                                 block_size;
    long reps = unit.count;
    for (long rep = 0; rep < reps || (auto_repeat && cursor < block.size());
         ++rep) {
      const size_t before = cursor;
      if (!apply_unit(unit, block, block_offset, cursor, out)) break;
      if (cursor == before && !unit.consumes_input) break;
    }
  }
}

auto print_formats(const std::vector<uint8_t>& data, size_t base_offset,
                   const Config& cfg) -> void {
  // Block size: the largest amount of data specified by any format
  // string; a starred unit anywhere makes one block span the rest of
  // the input.
  size_t block_size = 0;
  bool star = false;
  for (const auto& format_string : cfg.program.strings) {
    auto consumption = string_consumption(format_string);
    if (!consumption) {
      star = true;
    } else {
      block_size = std::max(block_size, *consumption);
    }
  }
  if (star || block_size == 0) block_size = data.size();
  if (block_size == 0) return;

  std::vector<uint8_t> previous_block;
  bool have_previous = false;
  bool star_printed = false;
  size_t cursor = 0;
  while (cursor < data.size()) {
    const size_t block_len = std::min(block_size, data.size() - cursor);
    std::vector<uint8_t> block(data.begin() +
                                   static_cast<std::ptrdiff_t>(cursor),
                               data.begin() +
                                   static_cast<std::ptrdiff_t>(cursor +
                                                                block_len));
    // Squeeze: a full block identical to the previous full block
    // collapses to a single "*" (#1373).
    if (!cfg.no_squeeze && have_previous && block_len == block_size &&
        block == previous_block) {
      if (!star_printed) {
        safePrintLn("*");
        star_printed = true;
      }
      cursor += block_len;
      continue;
    }
    std::string rendered;
    for (const auto& format_string : cfg.program.strings) {
      render_string(format_string, block, base_offset + cursor, block_size,
                    rendered);
    }
    safePrint(rendered);
    if (block_len == block_size) {
      previous_block = block;
      have_previous = true;
    }
    star_printed = false;
    cursor += block_len;
  }
}

// --- Input ------------------------------------------------------------------

auto read_stream(const Config& cfg, std::vector<uint8_t>& data) -> int {
  int result = 0;
  int files_attempted = 0;
  int files_failed = 0;
  auto read_one = [&](const std::string& filename) {
    if (filename.empty() || filename == "-") {
      file_io::set_stdin_binary();
      char buf[4096];
      while (std::cin.read(buf, sizeof(buf))) {
        data.insert(data.end(), buf, buf + std::cin.gcount());
      }
      if (std::cin.gcount() > 0) {
        data.insert(data.end(), buf, buf + std::cin.gcount());
      }
      return;
    }
    ++files_attempted;
    std::ifstream file(native_path::normalize_api_operand(filename),
                       std::ios::binary);
    if (!file) {
      // [GNU] warn shape is unquoted: "hexdump: <file>: No such file or
      // directory" (hexdump-display.c next()).
      safeErrorPrint("hexdump: ");
      safeErrorPrint(filename);
      safeErrorPrintLn(": No such file or directory");
      result = 1;
      ++files_failed;
      return;
    }
    data.insert(data.end(), std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
  };

  if (cfg.files.empty()) {
    read_one("");
  } else {
    // [GNU] operands form ONE continuous stream (#1373): offsets keep
    // running across file boundaries instead of restarting per file.
    for (const auto& file : cfg.files) read_one(file);
    // [GNU] when every file argument failed to open, get() adds the
    // summary warning "all input file arguments failed"
    // (hexdump-display.c:366).
    if (files_attempted > 0 && files_failed == files_attempted) {
      safeErrorPrintLn("hexdump: all input file arguments failed");
    }
  }
  return result;
}

auto run(const Config& cfg) -> int {
  std::vector<uint8_t> data;
  const int read_result = read_stream(cfg, data);

  size_t base_offset = 0;
  if (cfg.skip > 0) {
    base_offset = cfg.skip;
    if (cfg.skip >= data.size()) {
      data.clear();
    } else {
      data.erase(data.begin(),
                 data.begin() + static_cast<std::ptrdiff_t>(cfg.skip));
    }
  }
  if (cfg.length > 0 && cfg.length < data.size()) {
    data.resize(cfg.length);
  }

  if (cfg.has_formats) {
    print_formats(data, base_offset, cfg);
  } else {
    print_builtin(data, base_offset, cfg);
  }
  return read_result;
}

}  // namespace hexdump_pipeline

REGISTER_COMMAND(
    hexdump, "hexdump", "hexdump [OPTION]... [FILE]...",
    "Display file contents in hexadecimal, decimal, octal, or ascii.\n"
    "\n"
    "The hexdump utility is a filter which displays the specified files,\n"
    "or standard input, in a user-specified format.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -b               one-byte octal display\n"
    "  -c               one-byte character display\n"
    "  -C               canonical hex+ASCII display\n"
    "  -d               two-byte decimal display\n"
    "  -e FORMAT        format string\n"
    "  -f FORMAT_FILE   format file\n"
    "  -n LENGTH        interpret only LENGTH bytes of input\n"
    "  -o               two-byte octal display\n"
    "  -s OFFSET        skip OFFSET bytes from the beginning\n"
    "  -v               display all input, no squeeze\n"
    "  -x               two-byte hex display",
    "  hexdump -C file.bin     canonical hex+ASCII display\n"
    "  hexdump -x file.bin     two-byte hex display\n"
    "  hexdump -c file.bin     one-byte character display\n"
    "  hexdump -n 16 file.bin  display first 16 bytes\n"
    "  hexdump -s 64 file.bin  skip first 64 bytes",
    "od(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", HEXDUMP_OPTIONS) {
  using namespace hexdump_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"hexdump");
    return 1;
  }

  return run(*cfg_result);
}
