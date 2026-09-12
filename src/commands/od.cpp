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
 *  - File: od.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for od command.
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

auto constexpr OD_OPTIONS = std::array{
    // [GNU]
    OPTION("-A", "--address-radix", "select output address radix", STRING_TYPE),
    // [GNU]
    OPTION("-a", "", "select named character output"),
    // [GNU]
    OPTION("-b", "", "select octal byte output"),
    // [GNU]
    OPTION("-c", "", "select ASCII output"),
    // [GNU]
    OPTION("-d", "", "select unsigned decimal 2-byte output"),
    // [GNU]
    OPTION("-j", "--skip-bytes", "skip bytes", STRING_TYPE),
    // [GNU]
    OPTION("-N", "--read-bytes", "limit bytes", STRING_TYPE),
    // [GNU]
    OPTION("-o", "", "select octal 2-byte output"),
    // [GNU]
    OPTION("-t", "--format", "select output type", STRING_TYPE),
    // [GNU]
    OPTION("-v", "--output-duplicates", "write all input data"),
    // [GNU]
    OPTION("-w", "--width", "output bytes per line", OPTIONAL_STRING_TYPE),
    // [GNU]
    OPTION("-x", "", "select hexadecimal 2-byte units"),
    // [GNU]
    OPTION("", "--endian", "byte order for multi-byte input units",
           STRING_TYPE),
    // [GNU]
    OPTION("", "--traditional",
           "accept arguments in traditional form (e.g., od -x file)"),
    // [GNU] --strings: output strings of at least BYTES graphic characters
    OPTION("", "--strings",
           "output strings of at least BYTES graphic characters", STRING_TYPE),
    // [GNU] -S: output strings of at least BYTES graphic characters
    OPTION("-S", "", "output strings of at least BYTES graphic characters",
           STRING_TYPE),
    // [GNU] -f: select floating-point output
    OPTION("-f", "", "select floating-point output"),
    // [GNU] -i: select signed decimal 2-byte output
    OPTION("-i", "", "select signed decimal 2-byte output"),
    // [GNU] -l: select signed decimal 4-byte output
    OPTION("-l", "", "select signed decimal 4-byte output"),
    // [GNU] -s: select signed decimal 2-byte output
    OPTION("-s", "", "select signed decimal 2-byte output")};

// ======================================================
// Helper functions
// ======================================================

namespace od_pipeline {
enum class AddressBase { octal, decimal, hex, none };
enum class Endian { little, big };
enum class FormatKind {
  octal,
  hexadecimal,
  unsigned_decimal,
  signed_decimal,
  character,
  named_character,
  floating_point,  // [DIFFERS] Added for -f / -t f
  string_type      // [DIFFERS] Added for -t s / --strings / -S
};

struct FormatSpec {
  FormatKind kind = FormatKind::octal;
  size_t size = 2;
  bool ascii_trailer = false;
};

struct Config {
  AddressBase address_base = AddressBase::octal;
  size_t address_width = 7;
  size_t skip_bytes = 0;
  std::optional<size_t> limit_bytes;
  size_t bytes_per_line = 16;
  bool abbreviate_duplicate_blocks = true;
  Endian endian = Endian::little;
  std::vector<FormatSpec> specs;
  std::vector<std::string> files;
};

auto parse_count(std::string_view text) -> std::optional<size_t> {
  if (text.empty()) return std::nullopt;

  int base = 10;
  std::string_view number = text;
  if (number.size() > 2 && number[0] == '0' &&
      (number[1] == 'x' || number[1] == 'X')) {
    base = 16;
    number.remove_prefix(2);
  }

  size_t digit_end = 0;
  while (digit_end < number.size() &&
         std::isxdigit(static_cast<unsigned char>(number[digit_end]))) {
    if (base == 10 &&
        !std::isdigit(static_cast<unsigned char>(number[digit_end]))) {
      break;
    }
    ++digit_end;
  }
  if (digit_end == 0) return std::nullopt;

  size_t value = 0;
  auto [ptr, ec] =
      std::from_chars(number.data(), number.data() + digit_end, value, base);
  if (ec != std::errc() || ptr != number.data() + digit_end)
    return std::nullopt;

  std::string_view suffix = number.substr(digit_end);
  size_t multiplier = 1;
  if (suffix.empty() || suffix == "c") {
    multiplier = 1;
  } else if (suffix == "w") {
    multiplier = 2;
  } else if (suffix == "b") {
    multiplier = 512;
  } else if (suffix == "kB") {
    multiplier = 1000;
  } else if (suffix == "K" || suffix == "KiB") {
    multiplier = 1024;
  } else if (suffix == "MB") {
    multiplier = 1000ULL * 1000ULL;
  } else if (suffix == "M" || suffix == "MiB") {
    multiplier = 1024ULL * 1024ULL;
  } else if (suffix == "GB") {
    multiplier = 1000ULL * 1000ULL * 1000ULL;
  } else if (suffix == "G" || suffix == "GiB") {
    multiplier = 1024ULL * 1024ULL * 1024ULL;
  } else {
    return std::nullopt;
  }

  if (value > std::numeric_limits<size_t>::max() / multiplier) {
    return std::nullopt;
  }
  return value * multiplier;
}

auto parse_positive_count(std::string_view opt, std::string_view text)
    -> std::optional<size_t> {
  auto value = parse_count(text);
  if (!value || *value == 0) {
    safeErrorPrintLn("od: invalid " + std::string(opt) + " value '" +
                     std::string(text) + "'");
    return std::nullopt;
  }
  return value;
}

auto integral_size(std::string_view digits, size_t fallback)
    -> std::optional<size_t> {
  if (digits.empty()) return fallback;
  if (digits == "C") return 1;
  if (digits == "S") return 2;
  if (digits == "I") return 4;
  if (digits == "L") return sizeof(unsigned long);

  size_t size = 0;
  auto [ptr, ec] =
      std::from_chars(digits.data(), digits.data() + digits.size(), size);
  if (ec != std::errc() || ptr != digits.data() + digits.size() || size == 0 ||
      size > 8) {
    return std::nullopt;
  }
  return size;
}

auto append_format_specs(std::string_view spec_text,
                         std::vector<FormatSpec>& specs) -> bool {
  for (size_t i = 0; i < spec_text.size();) {
    char kind = spec_text[i++];
    bool ascii_trailer = false;
    size_t start = i;
    while (i < spec_text.size() &&
           (std::isdigit(static_cast<unsigned char>(spec_text[i])) ||
            spec_text[i] == 'C' || spec_text[i] == 'S' || spec_text[i] == 'I' ||
            spec_text[i] == 'L')) {
      ++i;
    }
    if (i < spec_text.size() && spec_text[i] == 'z') {
      ascii_trailer = true;
      ++i;
    }
    std::string_view size_text =
        spec_text.substr(start, i - start - (ascii_trailer ? 1 : 0));

    FormatSpec parsed;
    parsed.ascii_trailer = ascii_trailer;
    switch (kind) {
      case 'a':
        parsed.kind = FormatKind::named_character;
        parsed.size = 1;
        break;
      case 'c':
        parsed.kind = FormatKind::character;
        parsed.size = 1;
        break;
      case 'd':
        parsed.kind = FormatKind::signed_decimal;
        if (auto size = integral_size(size_text, 4)) {
          parsed.size = *size;
        } else {
          safeErrorPrintLn("od: invalid type string '" +
                           std::string(spec_text) + "'");
          return false;
        }
        break;
      case 'o':
        parsed.kind = FormatKind::octal;
        if (auto size = integral_size(size_text, 4)) {
          parsed.size = *size;
        } else {
          safeErrorPrintLn("od: invalid type string '" +
                           std::string(spec_text) + "'");
          return false;
        }
        break;
      case 'u':
        parsed.kind = FormatKind::unsigned_decimal;
        if (auto size = integral_size(size_text, 4)) {
          parsed.size = *size;
        } else {
          safeErrorPrintLn("od: invalid type string '" +
                           std::string(spec_text) + "'");
          return false;
        }
        break;
      case 'x':
        parsed.kind = FormatKind::hexadecimal;
        if (auto size = integral_size(size_text, 4)) {
          parsed.size = *size;
        } else {
          safeErrorPrintLn("od: invalid type string '" +
                           std::string(spec_text) + "'");
          return false;
        }
        break;
      case 'f':  // [DIFFERS] Floating-point output for -t f
        parsed.kind = FormatKind::floating_point;
        if (auto size = integral_size(size_text, 4)) {
          parsed.size = *size;
        } else {
          safeErrorPrintLn("od: invalid type string '" +
                           std::string(spec_text) + "'");
          return false;
        }
        break;
      case 's':  // [DIFFERS] String output for -t s
        parsed.kind = FormatKind::string_type;
        if (auto size = integral_size(size_text, 1)) {
          parsed.size = *size;
        } else {
          safeErrorPrintLn("od: invalid type string '" +
                           std::string(spec_text) + "'");
          return false;
        }
        break;
      default:
        safeErrorPrintLn("od: invalid type string '" + std::string(spec_text) +
                         "'");
        return false;
    }
    if (parsed.size == 0 || parsed.size > 8) {
      safeErrorPrintLn("od: invalid type string '" + std::string(spec_text) +
                       "'");
      return false;
    }
    specs.push_back(parsed);
  }
  return true;
}

auto address_to_string(size_t address, AddressBase base, size_t width)
    -> std::string {
  if (base == AddressBase::none) return {};
  std::ostringstream out;
  out << std::setfill('0') << std::setw(static_cast<int>(width));
  switch (base) {
    case AddressBase::decimal:
      out << std::dec << address;
      break;
    case AddressBase::hex:
      out << std::hex << std::nouppercase << address;
      break;
    case AddressBase::octal:
      out << std::oct << address;
      break;
    case AddressBase::none:
      break;
  }
  return out.str();
}

auto load_integer(const std::vector<unsigned char>& data, size_t offset,
                  size_t available, size_t size, Endian endian) -> uint64_t {
  uint64_t value = 0;
  if (endian == Endian::little) {
    for (size_t i = 0; i < size && i < available; ++i) {
      value |= static_cast<uint64_t>(data[offset + i]) << (i * CHAR_BIT);
    }
    return value;
  }

  for (size_t i = 0; i < size; ++i) {
    value <<= CHAR_BIT;
    if (i < available) value |= data[offset + i];
  }
  return value;
}

auto integer_field_width(const FormatSpec& spec) -> int {
  switch (spec.kind) {
    case FormatKind::octal:
      return static_cast<int>((spec.size * CHAR_BIT + 2) / 3);
    case FormatKind::hexadecimal:
      return static_cast<int>(spec.size * 2);
    case FormatKind::unsigned_decimal:
      if (spec.size <= 1) return 3;
      if (spec.size <= 2) return 5;
      if (spec.size <= 4) return 10;
      return 20;
    case FormatKind::signed_decimal:
      if (spec.size <= 1) return 4;
      if (spec.size <= 2) return 6;
      if (spec.size <= 4) return 11;
      return 20;
    case FormatKind::character:
    case FormatKind::named_character:
      return 3;
    case FormatKind::floating_point:  // [DIFFERS]
      if (spec.size <= 4) return 14;
      return 24;
    case FormatKind::string_type:  // [DIFFERS]
      return static_cast<int>(spec.size + 4);
  }
  return 3;
}

auto format_named_character(unsigned char c) -> std::string {
  static constexpr std::array<std::string_view, 33> names = {
      "nul", "soh", "stx", "etx", "eot", "enq", "ack", "bel", "bs",
      "ht",  "nl",  "vt",  "ff",  "cr",  "so",  "si",  "dle", "dc1",
      "dc2", "dc3", "dc4", "nak", "syn", "etb", "can", "em",  "sub",
      "esc", "fs",  "gs",  "rs",  "us",  "sp"};
  if (c < 33) return std::string(names[c]);
  if (c == 127) return "del";
  return std::string(1, static_cast<char>(c));
}

auto format_character(unsigned char c) -> std::string {
  switch (c) {
    case '\0':
      return "\\0";
    case '\a':
      return "\\a";
    case '\b':
      return "\\b";
    case '\f':
      return "\\f";
    case '\n':
      return "\\n";
    case '\r':
      return "\\r";
    case '\t':
      return "\\t";
    case '\v':
      return "\\v";
    default:
      if (c >= 32 && c < 127) return std::string(1, static_cast<char>(c));
      char buf[8];
      sprintf_s(buf, sizeof(buf), "%03o", c);
      // GNU od -c prints non-special non-printables as bare octal (001), not
      // \001.
      return std::string(buf);
  }
}

// [DIFFERS] Added floating-point formatter for -f / -t f
auto format_floating_point(const unsigned char* data, size_t available,
                           size_t size, Endian endian) -> std::string {
  if (size == 4 && available >= 4) {
    unsigned char buf[4];
    if (endian == Endian::little) {
      std::memcpy(buf, data, 4);
    } else {
      for (size_t j = 0; j < 4; ++j) buf[j] = data[3 - j];
    }
    float value;
    std::memcpy(&value, buf, sizeof(float));
    std::ostringstream out;
    out << std::setprecision(7) << value;
    return out.str();
  }
  if (size == 8 && available >= 8) {
    unsigned char buf[8];
    if (endian == Endian::little) {
      std::memcpy(buf, data, 8);
    } else {
      for (size_t j = 0; j < 8; ++j) buf[j] = data[7 - j];
    }
    double value;
    std::memcpy(&value, buf, sizeof(double));
    std::ostringstream out;
    out << std::setprecision(15) << value;
    return out.str();
  }
  return "?";
}

auto append_ascii_trailer(std::string& line,
                          const std::vector<unsigned char>& data, size_t offset,
                          size_t n_bytes, size_t bytes_per_line) -> void {
  if (n_bytes < bytes_per_line) {
    line.append((bytes_per_line - n_bytes) * 3, ' ');
  }
  line.append("  >");
  for (size_t i = 0; i < n_bytes; ++i) {
    unsigned char c = data[offset + i];
    line.push_back((c >= 32 && c < 127) ? static_cast<char>(c) : '.');
  }
  line.push_back('<');
}

auto append_formatted_line(std::string& output, const Config& cfg,
                           const FormatSpec& spec, size_t logical_offset,
                           const std::vector<unsigned char>& data,
                           size_t offset, size_t n_bytes, bool first_spec)
    -> void {
  if (cfg.address_base != AddressBase::none) {
    if (first_spec) {
      output += address_to_string(logical_offset, cfg.address_base,
                                  cfg.address_width);
    } else {
      output.append(cfg.address_width, ' ');
    }
  }

  const int width = integer_field_width(spec);
  for (size_t i = 0; i < n_bytes; i += spec.size) {
    size_t available = std::min(spec.size, n_bytes - i);
    std::ostringstream field;
    field << ' ';
    if (spec.kind == FormatKind::character) {
      field << std::setw(width) << format_character(data[offset + i]);
    } else if (spec.kind == FormatKind::named_character) {
      field << std::setw(width) << format_named_character(data[offset + i]);
    } else if (spec.kind == FormatKind::floating_point) {  // [DIFFERS]
      field << format_floating_point(data.data() + offset + i, available,
                                     spec.size, cfg.endian);
    } else if (spec.kind == FormatKind::string_type) {  // [DIFFERS]
      std::string str;
      bool all_printable = true;
      for (size_t j = 0; j < spec.size; ++j) {
        if (i + j < n_bytes) {
          unsigned char c = data[offset + i + j];
          if (c >= 32 && c < 127) {
            str += static_cast<char>(c);
          } else {
            all_printable = false;
            break;
          }
        }
      }
      if (all_printable && str.size() == spec.size) {
        field << " " << str;
      } else {
        field << " " << std::string(spec.size, ' ');
      }
    } else {
      uint64_t raw =
          load_integer(data, offset + i, available, spec.size, cfg.endian);
      if (spec.kind == FormatKind::hexadecimal) {
        field << std::setfill('0') << std::setw(width) << std::hex
              << std::nouppercase << raw;
      } else if (spec.kind == FormatKind::octal) {
        field << std::setfill('0') << std::setw(width) << std::oct << raw;
      } else if (spec.kind == FormatKind::unsigned_decimal) {
        field << std::setw(width) << std::dec << raw;
      } else {
        int64_t signed_value = static_cast<int64_t>(raw);
        if (available == spec.size && spec.size < sizeof(uint64_t) &&
            (raw & (1ULL << (spec.size * CHAR_BIT - 1))) != 0) {
          signed_value =
              static_cast<int64_t>(raw | (~0ULL << (spec.size * CHAR_BIT)));
        }
        field << std::setw(width) << std::dec << signed_value;
      }
    }
    output += field.str();
  }

  if (spec.ascii_trailer) {
    append_ascii_trailer(output, data, offset, n_bytes, cfg.bytes_per_line);
  }
  output.push_back('\n');
}

auto read_file_bytes(const std::string& filename,
                     std::vector<unsigned char>& data,
                     std::optional<size_t> max_bytes) -> bool {
  if (max_bytes && data.size() >= *max_bytes) return true;

  std::ifstream input(native_path::normalize_api_operand(filename),
                      std::ios::binary);
  if (!input) {
    // [GNU] od reports "<name>: <reason>"; a directory operand reads as
    // "Is a directory" on GNU (uutils #12993)
    std::error_code ec;
    const std::filesystem::path p(
        native_path::normalize_api_operand(filename));
    if (std::filesystem::is_directory(p, ec)) {
      safeErrorPrintLn("od: " + filename + ": Is a directory");
    } else if (std::filesystem::exists(p, ec)) {
      safeErrorPrintLn("od: " + filename + ": Permission denied");
    } else {
      safeErrorPrintLn("od: " + filename + ": No such file or directory");
    }
    return false;
  }

  std::array<char, 64 * 1024> buffer{};
  while (input && (!max_bytes || data.size() < *max_bytes)) {
    size_t wanted = buffer.size();
    if (max_bytes) wanted = std::min(wanted, *max_bytes - data.size());
    input.read(buffer.data(), static_cast<std::streamsize>(wanted));
    std::streamsize got = input.gcount();
    if (got <= 0) break;
    data.insert(data.end(), buffer.begin(), buffer.begin() + got);
  }
  return true;
}

auto read_stream_bytes(std::istream& input, std::vector<unsigned char>& data,
                       std::optional<size_t> max_bytes) -> void {
  std::array<char, 64 * 1024> buffer{};
  while (input && (!max_bytes || data.size() < *max_bytes)) {
    size_t wanted = buffer.size();
    if (max_bytes) wanted = std::min(wanted, *max_bytes - data.size());
    input.read(buffer.data(), static_cast<std::streamsize>(wanted));
    std::streamsize got = input.gcount();
    if (got <= 0) break;
    data.insert(data.end(), buffer.begin(), buffer.begin() + got);
  }
}

auto max_input_bytes_needed(const Config& cfg) -> std::optional<size_t> {
  if (!cfg.limit_bytes) return std::nullopt;
  if (cfg.skip_bytes > std::numeric_limits<size_t>::max() - *cfg.limit_bytes) {
    return std::numeric_limits<size_t>::max();
  }
  return cfg.skip_bytes + *cfg.limit_bytes;
}

auto build_config(const CommandContext<OD_OPTIONS.size()>& ctx)
    -> std::optional<Config> {
  Config cfg;

  std::string base = ctx.get<std::string>("-A", "");
  if (base.empty()) base = ctx.get<std::string>("--address-radix", "o");
  if (base == "d") {
    cfg.address_base = AddressBase::decimal;
    cfg.address_width = 7;
  } else if (base == "x") {
    cfg.address_base = AddressBase::hex;
    cfg.address_width = 6;
  } else if (base == "n") {
    cfg.address_base = AddressBase::none;
    cfg.address_width = 0;
  } else if (base == "o" || base.empty()) {
    cfg.address_base = AddressBase::octal;
    cfg.address_width = 7;
  } else {
    safeErrorPrintLn("od: invalid output address radix '" + base + "'");
    return std::nullopt;
  }

  if (ctx.has("-j") || ctx.has("--skip-bytes")) {
    const std::string jval = ctx.has("-j")
                                 ? ctx.get<std::string>("-j", "")
                                 : ctx.get<std::string>("--skip-bytes", "");
    auto parsed = parse_count(jval);
    if (!parsed) {
      safeErrorPrintLn("od: invalid --skip-bytes/-j value '" + jval + "'");
      return std::nullopt;
    }
    cfg.skip_bytes = *parsed;
  }
  if (ctx.has("-N") || ctx.has("--read-bytes")) {
    const std::string nval = ctx.has("-N")
                                 ? ctx.get<std::string>("-N", "")
                                 : ctx.get<std::string>("--read-bytes", "");
    auto parsed = parse_count(nval);
    if (!parsed) {
      safeErrorPrintLn("od: invalid --read-bytes/-N value '" + nval + "'");
      return std::nullopt;
    }
    cfg.limit_bytes = *parsed;
  }

  std::string width = ctx.get<std::string>("--width", "");
  if (!ctx.has("--width")) width = ctx.get<std::string>("-w", "");
  if (ctx.has("--width") || ctx.has("-w")) {
    if (width.empty()) {
      cfg.bytes_per_line = 32;
    } else {
      auto parsed = parse_positive_count("-w", width);
      if (!parsed) return std::nullopt;
      cfg.bytes_per_line = *parsed;
    }
  }

  if (ctx.has("--endian")) {
    const auto endian = ctx.get<std::string>("--endian", "");
    if (endian == "little") {
      cfg.endian = Endian::little;
    } else if (endian == "big") {
      cfg.endian = Endian::big;
    } else {
      safeErrorPrintLn("od: invalid endian value '" + endian + "'");
      return std::nullopt;
    }
  }

  if (ctx.has("-a"))
    cfg.specs.push_back({FormatKind::named_character, 1, false});
  if (ctx.has("-b")) cfg.specs.push_back({FormatKind::octal, 1, false});
  if (ctx.has("-c")) cfg.specs.push_back({FormatKind::character, 1, false});
  if (ctx.has("-d"))
    cfg.specs.push_back({FormatKind::unsigned_decimal, 2, false});
  if (ctx.has("-f"))
    cfg.specs.push_back({FormatKind::floating_point, 4, false});  // [DIFFERS]
  if (ctx.has("-i"))
    cfg.specs.push_back({FormatKind::signed_decimal, 2, false});  // [DIFFERS]
  if (ctx.has("-l"))
    cfg.specs.push_back({FormatKind::signed_decimal, 4, false});  // [DIFFERS]
  if (ctx.has("-o")) cfg.specs.push_back({FormatKind::octal, 2, false});
  if (ctx.has("-s"))
    cfg.specs.push_back({FormatKind::signed_decimal, 2, false});  // [DIFFERS]
  if (ctx.has("-x")) cfg.specs.push_back({FormatKind::hexadecimal, 2, false});
  for (const auto& occurrence : ctx.get_all<std::string>("-t")) {
    if (!append_format_specs(occurrence, cfg.specs)) return std::nullopt;
  }
  // [DIFFERS] --strings / -S: output strings of at least N bytes
  for (const auto& occurrence : ctx.get_all<std::string>("--strings")) {
    size_t min_len = 3;
    if (!occurrence.empty()) {
      auto parsed = parse_count(occurrence);
      if (!parsed || *parsed == 0) {
        safeErrorPrintLn("od: invalid --strings value '" + occurrence + "'");
        return std::nullopt;
      }
      min_len = *parsed;
    }
    cfg.specs.push_back({FormatKind::string_type, min_len, false});
  }
  for (const auto& occurrence : ctx.get_all<std::string>("-S")) {
    size_t min_len = 3;
    if (!occurrence.empty()) {
      auto parsed = parse_count(occurrence);
      if (!parsed || *parsed == 0) {
        safeErrorPrintLn("od: invalid -S value '" + occurrence + "'");
        return std::nullopt;
      }
      min_len = *parsed;
    }
    cfg.specs.push_back({FormatKind::string_type, min_len, false});
  }
  if (cfg.specs.empty()) {
    cfg.specs.push_back({FormatKind::octal, 2, false});
  }
  cfg.abbreviate_duplicate_blocks =
      !ctx.get<bool>("-v", false) &&
      !ctx.get<bool>("--output-duplicates", false);

  bool first_positional = true;
  for (auto arg : ctx.positionals) {
    std::string file_arg(arg);
    if (ctx.has("-w") && ctx.get<std::string>("-w", "").empty() &&
        parse_count(file_arg).has_value()) {
      cfg.bytes_per_line = *parse_positive_count("-w", file_arg);
      continue;
    }
    // [DIFFERS] --traditional: first positional may be a skip count
    if (first_positional && ctx.has("--traditional")) {
      auto parsed = parse_count(file_arg);
      if (parsed) {
        cfg.skip_bytes = *parsed;
        first_positional = false;
        continue;
      }
    }
    first_positional = false;
    cfg.files.push_back(file_arg);
  }

  return cfg;
}

auto dump_data(const Config& cfg, const std::vector<unsigned char>& full_data)
    -> std::string {
  std::vector<unsigned char> data;
  if (cfg.skip_bytes < full_data.size()) {
    auto begin =
        full_data.begin() + static_cast<std::ptrdiff_t>(cfg.skip_bytes);
    auto end = full_data.end();
    if (cfg.limit_bytes &&
        *cfg.limit_bytes < static_cast<size_t>(end - begin)) {
      end = begin + static_cast<std::ptrdiff_t>(*cfg.limit_bytes);
    }
    data.assign(begin, end);
  }

  std::string output;
  std::vector<unsigned char> previous;
  bool previous_equal = false;

  for (size_t pos = 0; pos < data.size(); pos += cfg.bytes_per_line) {
    size_t n = std::min(cfg.bytes_per_line, data.size() - pos);
    bool is_full = n == cfg.bytes_per_line;
    bool duplicate =
        cfg.abbreviate_duplicate_blocks && is_full && previous.size() == n &&
        std::equal(data.begin() + static_cast<std::ptrdiff_t>(pos),
                   data.begin() + static_cast<std::ptrdiff_t>(pos + n),
                   previous.begin());
    if (duplicate) {
      if (!previous_equal) output += "*\n";
      previous_equal = true;
      continue;
    }
    previous.assign(data.begin() + static_cast<std::ptrdiff_t>(pos),
                    data.begin() + static_cast<std::ptrdiff_t>(pos + n));
    previous_equal = false;

    for (size_t spec_index = 0; spec_index < cfg.specs.size(); ++spec_index) {
      append_formatted_line(output, cfg, cfg.specs[spec_index],
                            cfg.skip_bytes + pos, data, pos, n,
                            spec_index == 0);
    }
  }

  if (cfg.address_base != AddressBase::none) {
    output += address_to_string(cfg.skip_bytes + data.size(), cfg.address_base,
                                cfg.address_width);
    output.push_back('\n');
  }
  return output;
}
}  // namespace od_pipeline

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    od,
    /* cmd_name */ "od",
    /* cmd_synopsis */ "od [OPTION]... [FILE]...",
    /* cmd_desc */
    "Dump files in octal and other formats.\n"
    "Write an unambiguous representation of each FILE to standard output.\n"
    "With no FILE, or when FILE is -, read standard input.",
    /* examples */
    "  od -x file.bin\n"
    "  od -t x1z -v file.bin\n"
    "  echo 'Hello' | od -c\n"
    "  od -A x -t x1z -N 16 file.bin",
    /* see_also */ "hexdump, xxd",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ OD_OPTIONS) {
  auto cfg_result = od_pipeline::build_config(ctx);
  if (!cfg_result) return 1;
  const od_pipeline::Config& cfg = *cfg_result;

  // Read input
  std::vector<unsigned char> data;
  auto max_input_bytes = od_pipeline::max_input_bytes_needed(cfg);

  if (cfg.files.empty() || cfg.files[0] == "-") {
    _setmode(_fileno(stdin), _O_BINARY);
    od_pipeline::read_stream_bytes(std::cin, data, max_input_bytes);
  } else {
    bool ok = true;
    for (const auto& file_arg : cfg.files) {
      // [GNU] Operands that are not well-formed UTF-8 (e.g. a lone 0xFF
      // byte) cannot name a real file through the wide-char Windows API.
      // GNU reports "No such file or directory" and exits 1; routing such
      // bytes into path conversion previously hung the tool (#339,
      // uutils#12794).
      if (!is_valid_utf8(file_arg)) {
        // Build the diagnostic as one message through the shared i18n helper
        // (the idiom date.cpp uses for the same ENOENT case). Emitting it as
        // three separate safeErrorPrint calls made each fragment its own
        // catalog entry — producing a stray "od: " key and leaving the
        // operand and the reason untranslatable.
        safeErrorPrintLn(winux::i18n::format(
            "command.od.error.cannot_open",
            "od: {}: No such file or directory", file_arg));
        ok = false;
        continue;
      }
      if (max_input_bytes && data.size() >= *max_input_bytes) break;
      std::vector<std::string> expanded;
      if (contains_wildcard(file_arg)) {
        auto glob_result = glob_expand(file_arg);
        if (glob_result.expanded) {
          for (const auto& f : glob_result.files) {
            expanded.push_back(wstring_to_utf8(f));
          }
        } else {
          expanded.push_back(file_arg);
        }
      } else {
        expanded.push_back(file_arg);
      }
      for (const auto& exp : expanded) {
        if (max_input_bytes && data.size() >= *max_input_bytes) break;
        ok = od_pipeline::read_file_bytes(exp, data, max_input_bytes) && ok;
      }
    }
    if (!ok && data.empty()) return 1;
  }

  safePrint(od_pipeline::dump_data(cfg, data));
  return 0;
}
