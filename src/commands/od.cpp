// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
    // (optional argument: bare --strings means --strings=3)
    OPTION("", "--strings",
           "output strings of at least BYTES graphic characters",
           OPTIONAL_STRING_TYPE),
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
  floating_point  // -f / -t f; sizes follow GNU fp_type_size (4/8/16)
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
  bool strings_mode = false;  // --strings / -S
  size_t string_min = 3;
  std::vector<FormatSpec> specs;
  std::vector<std::string> files;
};

// [GNU] xstrtoumax-style count parsing for -j/-N/-w/-S: leading
// whitespace and sign allowed, base 0 autodetection (0x hex, 0 octal,
// else decimal), trailing 'b' (512) and 'B' (1024) multipliers per the
// "Bb" suffix set od passes to xstrtoumax, entire string must be a number.
struct CountResult {
  enum class Status { ok, invalid, too_large };
  Status status = Status::invalid;
  size_t value = 0;
};

auto parse_count(std::string_view text) -> CountResult {
  CountResult r;
  size_t i = 0;
  while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
    ++i;
  }
  bool negative = false;
  if (i < text.size() && (text[i] == '+' || text[i] == '-')) {
    negative = text[i] == '-';
    ++i;
  }
  std::string_view num = text.substr(i);
  // [GNU] xstrtoumax suffixes "Bb": 'b' scales by 512, 'B' by 1024.
  // Not a suffix when the digits are hex (0x1b parses as 27, suffix absent),
  // mirroring how strtol consumes the whole hex literal.
  uint64_t multiplier = 1;
  bool is_hex =
      num.size() >= 2 && num[0] == '0' && (num[1] == 'x' || num[1] == 'X');
  if (!is_hex && num.size() >= 2 && (num.back() == 'b' || num.back() == 'B')) {
    multiplier = num.back() == 'b' ? 512 : 1024;
    num.remove_suffix(1);
  }
  int base = 10;
  if (num.size() >= 2 && num[0] == '0' && (num[1] == 'x' || num[1] == 'X')) {
    base = 16;
    num.remove_prefix(2);
  } else if (num.size() > 1 && num[0] == '0') {
    base = 8;
  }
  if (num.empty()) return r;

  uint64_t value = 0;
  bool overflow = false;
  for (char c : num) {
    int d = -1;
    if (c >= '0' && c <= '9')
      d = c - '0';
    else if (c >= 'a' && c <= 'f')
      d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      d = c - 'A' + 10;
    if (d < 0 || d >= base) return r;  // invalid
    if (value >
        (std::numeric_limits<uint64_t>::max() - static_cast<uint64_t>(d)) /
            static_cast<uint64_t>(base)) {
      overflow = true;
    } else {
      value = value * static_cast<uint64_t>(base) + static_cast<uint64_t>(d);
    }
  }
  if (overflow) {
    r.status = CountResult::Status::too_large;
    return r;
  }
  if (negative) value = static_cast<uint64_t>(0) - value;
  if (multiplier != 1) {
    if (value > std::numeric_limits<uint64_t>::max() / multiplier) {
      r.status = CountResult::Status::too_large;
      return r;
    }
    value *= multiplier;
    if (negative) value = static_cast<uint64_t>(0) - value;
  }
  r.status = CountResult::Status::ok;
  r.value = static_cast<size_t>(value);
  return r;
}

// GNU xstrtol_fatal wording: "invalid -j argument 'xyz'" /
// "-j argument 'xyz' too large".
auto report_count_error(std::string_view opt_name, std::string_view text,
                        CountResult result) -> void {
  if (result.status == CountResult::Status::too_large) {
    safeErrorPrintLn("od: " + std::string(opt_name) + " argument '" +
                     std::string(text) + "' too large");
  } else {
    safeErrorPrintLn("od: invalid " + std::string(opt_name) + " argument '" +
                     std::string(text) + "'");
  }
}

// Which spelling of an option did the user last type?  GNU diagnostics
// name the exact flag (e.g. "-j" vs "--skip-bytes").
template <size_t N>
auto option_spelling(const CommandContext<N>& ctx, std::string_view short_name,
                     std::string_view long_name) -> std::string {
  std::string used(long_name.empty() ? short_name : long_name);
  for (std::string_view a : ctx.raw_args) {
    if (!long_name.empty() &&
        (a == long_name ||
         (a.size() > long_name.size() && a.starts_with(long_name) &&
          a[long_name.size()] == '='))) {
      used = std::string(long_name);
      continue;
    }
    if (!short_name.empty() && a.size() >= 2 && a[0] == '-' && a[1] != '-' &&
        a.find(short_name[1]) != std::string_view::npos) {
      used = std::string(short_name);
    }
  }
  return used;
}

auto append_format_specs(std::string_view spec_text,
                         std::vector<FormatSpec>& specs) -> bool {
  auto invalid_char = [&](char c) {
    safeErrorPrintLn("od: invalid character '" + std::string(1, c) +
                     "' in type string '" + std::string(spec_text) + "'");
    return false;
  };
  auto invalid_size = [&](std::string_view kind_word, unsigned long long n) {
    safeErrorPrintLn("od: invalid type string '" + std::string(spec_text) +
                     "';\nthis system doesn't provide a " + std::to_string(n) +
                     "-byte " + std::string(kind_word) + " type");
    return false;
  };

  for (size_t i = 0; i < spec_text.size();) {
    char kind = spec_text[i++];
    FormatSpec parsed;

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
      case 'o':
      case 'u':
      case 'x': {
        parsed.kind = kind == 'd'   ? FormatKind::signed_decimal
                      : kind == 'o' ? FormatKind::octal
                      : kind == 'u' ? FormatKind::unsigned_decimal
                                    : FormatKind::hexadecimal;
        // [GNU] default is sizeof(unsigned int) = 4
        size_t size = 4;
        if (i < spec_text.size() &&
            (spec_text[i] == 'C' || spec_text[i] == 'S' ||
             spec_text[i] == 'I' || spec_text[i] == 'L')) {
          char letter = spec_text[i++];
          size = letter == 'C' ? 1 : letter == 'S' ? 2 : letter == 'I' ? 4 : 8;
        } else if (i < spec_text.size() &&
                   std::isdigit(static_cast<unsigned char>(spec_text[i]))) {
          // numeric size; >INT_MAX overflows GNU's int and gets the
          // plain "invalid type string" diagnostic
          unsigned long long n = 0;
          size_t digits_start = i;
          while (i < spec_text.size() &&
                 std::isdigit(static_cast<unsigned char>(spec_text[i]))) {
            n = n * 10 + (spec_text[i++] - '0');
            if (n > std::numeric_limits<int>::max()) {
              safeErrorPrintLn("od: invalid type string '" +
                               std::string(spec_text) + "'");
              return false;
            }
          }
          (void)digits_start;
          if (n != 1 && n != 2 && n != 4 && n != 8) {
            return invalid_size("integral", n);
          }
          size = static_cast<size_t>(n);
        }
        parsed.size = size;
        break;
      }
      case 'f': {
        parsed.kind = FormatKind::floating_point;
        // [GNU] default is sizeof(double) = 8
        size_t size = 8;
        if (i < spec_text.size() &&
            (spec_text[i] == 'F' || spec_text[i] == 'D' ||
             spec_text[i] == 'L')) {
          char letter = spec_text[i++];
          size = letter == 'F' ? 4 : letter == 'D' ? 8 : 16;
        } else if (i < spec_text.size() &&
                   std::isdigit(static_cast<unsigned char>(spec_text[i]))) {
          unsigned long long n = 0;
          while (i < spec_text.size() &&
                 std::isdigit(static_cast<unsigned char>(spec_text[i]))) {
            n = n * 10 + (spec_text[i++] - '0');
            if (n > std::numeric_limits<int>::max()) {
              safeErrorPrintLn("od: invalid type string '" +
                               std::string(spec_text) + "'");
              return false;
            }
          }
          if (n != 4 && n != 8 && n != 16) {
            return invalid_size("floating point", n);
          }
          size = static_cast<size_t>(n);
        }
        parsed.size = size;
        break;
      }
      default:
        return invalid_char(kind);
    }
    if (i < spec_text.size() && spec_text[i] == 'z') {
      parsed.ascii_trailer = true;
      ++i;
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
    case FormatKind::floating_point:
      // [GNU] FLT/DBL/LDBL_STRLEN_BOUND with a one-byte decimal point
      if (spec.size <= 4) return 15;
      if (spec.size <= 8) return 24;
      return 29;
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

// [GNU] -t fN: IEEE-754 binary32/64 or the x87 80-bit extended type
// (binary80 packed into 16 bytes).  Like GNU, a short final block is
// decoded with zero-filled missing tail bytes.  Output uses the
// ftoastr/dtoastr/ldtoastr convention: %.*g starting at T_DIG and
// raising precision until the text round-trips.
auto format_floating_point(const unsigned char* data, size_t available,
                           size_t size, Endian endian) -> std::string {
  unsigned char buf[16] = {};
  const size_t n = std::min(available, size);
  std::memcpy(buf, data, n);
  const gnu_float80::Ext80 value =
      gnu_float80::from_bytes(buf, size, endian == Endian::big);
  const int dig = size == 4 ? 6 : size == 8 ? 15 : 18;    // FLT/DBL/LDBL_DIG
  const int bound = size == 4 ? 9 : size == 8 ? 17 : 21;  // *_PREC_BOUND
  const int min_normal_exp = size == 4   ? -126
                             : size == 8 ? -1022
                                         : -16382;  // min normal exp
  const int sig_bits = size == 4 ? 24 : size == 8 ? 53 : 64;
  return gnu_float80::shortest_g(value, dig, bound, min_normal_exp, sig_bits);
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
    } else if (spec.kind == FormatKind::floating_point) {
      field << std::setw(width)
            << format_floating_point(data.data() + offset + i, available,
                                     spec.size, cfg.endian);
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
    // [GNU] operands in diagnostics go through quotef() — the same
    // shell_escape quoting printf %q uses ("od: ''$'\377': ...").
    const std::string shown = gnu_quotearg::shell_escape_quote(filename);
    std::error_code ec;
    const std::filesystem::path p(native_path::normalize_api_operand(filename));
    if (std::filesystem::is_directory(p, ec)) {
      safeErrorPrintLn("od: " + shown + ": Is a directory");
    } else if (std::filesystem::exists(p, ec)) {
      safeErrorPrintLn("od: " + shown + ": Permission denied");
    } else {
      safeErrorPrintLn("od: " + shown + ": No such file or directory");
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
    // [GNU] od: invalid output address radix 'z'; it must be one
    // character from [doxn]
    safeErrorPrintLn("od: invalid output address radix '" + base +
                     "'; it must be one character from [doxn]");
    return std::nullopt;
  }

  if (ctx.has("-j") || ctx.has("--skip-bytes")) {
    // GNU: the last -j/--skip-bytes occurrence wins
    const auto values = ctx.get_all<std::string>("--skip-bytes");
    const std::string& jval = values.back();
    auto parsed = parse_count(jval);
    if (parsed.status != CountResult::Status::ok) {
      report_count_error(option_spelling(ctx, "-j", "--skip-bytes"), jval,
                         parsed);
      return std::nullopt;
    }
    cfg.skip_bytes = parsed.value;
  }
  if (ctx.has("-N") || ctx.has("--read-bytes")) {
    const auto values = ctx.get_all<std::string>("--read-bytes");
    const std::string& nval = values.back();
    auto parsed = parse_count(nval);
    if (parsed.status != CountResult::Status::ok) {
      report_count_error(option_spelling(ctx, "-N", "--read-bytes"), nval,
                         parsed);
      return std::nullopt;
    }
    cfg.limit_bytes = parsed.value;
  }

  if (ctx.has("--width") || ctx.has("-w")) {
    const auto values = ctx.get_all<std::string>("--width");
    const std::string& width = values.back();
    if (width.empty()) {
      cfg.bytes_per_line = 32;  // [GNU] bare -w means -w32
    } else {
      auto parsed = parse_count(width);
      if (parsed.status != CountResult::Status::ok || parsed.value == 0) {
        report_count_error(option_spelling(ctx, "-w", "--width"), width,
                           parsed);
        return std::nullopt;
      }
      cfg.bytes_per_line = parsed.value;
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
    cfg.specs.push_back(
        {FormatKind::floating_point, 4, false});  // [GNU] -f == -t fF
  if (ctx.has("-i"))
    cfg.specs.push_back({FormatKind::signed_decimal, 4, false});  // [GNU] dI
  if (ctx.has("-l"))
    cfg.specs.push_back({FormatKind::signed_decimal, 8, false});  // [GNU] dL
  if (ctx.has("-o")) cfg.specs.push_back({FormatKind::octal, 2, false});
  if (ctx.has("-s"))
    cfg.specs.push_back({FormatKind::signed_decimal, 2, false});  // [DIFFERS]
  if (ctx.has("-x")) cfg.specs.push_back({FormatKind::hexadecimal, 2, false});
  for (const auto& occurrence : ctx.get_all<std::string>("-t")) {
    if (!append_format_specs(occurrence, cfg.specs)) return std::nullopt;
  }
  // [GNU] --strings/-S: dump NUL-terminated strings of >=N graphic
  // characters.  --strings takes an optional argument (default 3),
  // -S a required one; the last occurrence wins.
  for (const auto& occ : ctx.string_occurrences({"-S", "--strings"})) {
    cfg.strings_mode = true;
    if (occ.value.empty()) {
      cfg.string_min = 3;
      continue;
    }
    auto parsed = parse_count(occ.value);
    if (parsed.status != CountResult::Status::ok) {
      std::string_view opt_name =
          occ.long_name.empty() ? occ.short_name : occ.long_name;
      report_count_error(opt_name, occ.value, parsed);
      return std::nullopt;
    }
    cfg.string_min = parsed.value;
  }
  if (cfg.strings_mode && !cfg.specs.empty()) {
    // [GNU] od: no type may be specified when dumping strings
    safeErrorPrintLn("od: no type may be specified when dumping strings");
    return std::nullopt;
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
    if (ctx.has("-w") && ctx.get<std::string>("-w", "").empty()) {
      auto parsed = parse_count(file_arg);
      if (parsed.status == CountResult::Status::ok && parsed.value > 0) {
        cfg.bytes_per_line = parsed.value;
        continue;
      }
    }
    // [DIFFERS] --traditional: first positional may be a skip count
    if (first_positional && ctx.has("--traditional")) {
      auto parsed = parse_count(file_arg);
      if (parsed.status == CountResult::Status::ok) {
        cfg.skip_bytes = parsed.value;
        first_positional = false;
        continue;
      }
    }
    first_positional = false;
    cfg.files.push_back(file_arg);
  }

  return cfg;
}

// The region of input od operates on: everything after --skip-bytes,
// truncated to --read-bytes.
auto sliced_input(const Config& cfg,
                  const std::vector<unsigned char>& full_data)
    -> std::vector<unsigned char> {
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
  return data;
}

// [GNU] dump_strings: scan for runs of >= string_min printable (C locale)
// bytes, print only NUL-terminated runs, honoring -N as a hard read
// boundary — including GNU's odd absolute-address computation which can
// wrap around for a string ending exactly at the -N limit.
auto dump_strings(const Config& cfg,
                  const std::vector<unsigned char>& full_data) -> std::string {
  const std::vector<unsigned char> data = sliced_input(cfg, full_data);
  const bool limited = cfg.limit_bytes.has_value();
  const size_t end_offset = limited ? cfg.skip_bytes + *cfg.limit_bytes
                                    : std::numeric_limits<size_t>::max();
  const size_t min = cfg.string_min;

  auto is_print = [](unsigned char c) { return c >= 0x20 && c <= 0x7E; };

  std::string output;
  std::string buf;
  size_t pos = 0;                   // index into data
  size_t address = cfg.skip_bytes;  // absolute input offset

  while (true) {
    // tryline:
    if (limited && (end_offset < min || end_offset - min <= address)) {
      break;
    }
    buf.clear();
    size_t i = 0;
    for (; i < min; ++i) {
      if (pos >= data.size()) return output;  // EOF mid-scan
      unsigned char c = data[pos++];
      ++address;
      if (!is_print(c)) goto tryline;
      buf.push_back(static_cast<char>(c));
    }
    while (!limited || address < end_offset) {
      if (pos >= data.size()) return output;  // EOF, no NUL
      unsigned char c = data[pos++];
      ++address;
      if (c == 0) break;
      if (!is_print(c)) goto tryline;
      buf.push_back(static_cast<char>(c));
    }
    // print the string: GNU prints format_address(address - i - 1, ' ')
    {
      size_t start = address - buf.size() - 1;  // unsigned wrap is intended
      if (cfg.address_base != AddressBase::none) {
        output += address_to_string(start, cfg.address_base, cfg.address_width);
        output.push_back(' ');
      }
      output += buf;
      output.push_back('\n');
    }
  tryline:;
  }
  return output;
}

auto dump_data(const Config& cfg, const std::vector<unsigned char>& full_data)
    -> std::string {
  std::vector<unsigned char> data = sliced_input(cfg, full_data);

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
    // [GNU] closed stdin (<&-) errors "od: -: Bad file descriptor".
    if (file_io::stdin_is_bad()) {
      safeErrorPrintLn("od: -: Bad file descriptor");
      return 1;
    }
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
            "command.od.error.cannot_open", "od: {}: No such file or directory",
            gnu_quotearg::shell_escape_quote(file_arg)));
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

  safePrint(cfg.strings_mode ? od_pipeline::dump_strings(cfg, data)
                             : od_pipeline::dump_data(cfg, data));
  return 0;
}
