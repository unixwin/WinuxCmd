// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for strings.
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

auto constexpr STRINGS_OPTIONS = std::array{
    // [GNU] -a, --all
    OPTION("-a", "--all", "scan each file in its entirety (default)"),
    // [GNU] -d, --data
    OPTION("-d", "--data",
           "scan only object data sections (placeholder/no-op: BFD section "
           "parsing is not linked in this Windows build)"),
    // [GNU] -f, --print-file-name
    OPTION("-f", "--print-file-name", "print the file name before each string"),
    // [GNU] -n, --bytes
    OPTION("-n", "--bytes",
           "print sequences of at least MIN printable characters (default 4)",
           STRING_TYPE),
    // [GNU] -NUM
    OPTION("-NUM", "", "same as --bytes=MIN", INT_TYPE),
    // [GNU] -t, --radix
    OPTION("-t", "--radix",
           "print the offset within the file before each string", STRING_TYPE),
    // [GNU] -w, --include-all-whitespace
    OPTION("-w", "--include-all-whitespace",
           "include all whitespace as valid string characters"),
    // [GNU] -e, --encoding
    OPTION("-e", "--encoding",
           "select character encoding: s=7-bit-ascii, S=8-bit-UTF8, "
           "b=16-bit-big-endian, l=16-bit-little-endian, B=32-bit-big-endian, "
           "L=32-bit-little-endian",
           STRING_TYPE),
    // [GNU] -o
    OPTION("-o", "", "print offset before each string (alias for -t o)"),
    // [GNU] -s, --output-separator
    OPTION("-s", "--output-separator",
           "string used to separate parsed strings in output", STRING_TYPE),
    // [GNU] -T, --target
    OPTION("-T", "--target",
           "specify binary file format (placeholder/no-op: BFD target "
           "selection is not linked in this Windows build)",
           STRING_TYPE),
    // [GNU] -U, --unicode
    OPTION("-U", "--unicode",
           "unicode display mode (placeholder/no-op except non-default modes "
           "select UTF-8 scanning)",
           STRING_TYPE)};

namespace strings_pipeline {
namespace cp = core::pipeline;

enum class Encoding {
  ASCII,
  UTF8,
  BigEndian16,
  LittleEndian16,
  BigEndian32,
  LittleEndian32
};
enum class Radix { None, Octal, Decimal, Hex };

struct Config {
  size_t min_length = 4;
  Radix radix = Radix::None;
  Encoding encoding = Encoding::ASCII;
  bool all = false;
  bool data_only = false;
  bool print_filenames = false;
  bool include_all_whitespace = false;
  std::string output_separator = "\n";
  std::string target;
  std::string unicode_mode = "default";
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<STRINGS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.all = ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false);
  cfg.data_only = ctx.get<bool>("-d", false) || ctx.get<bool>("--data", false);
  cfg.print_filenames =
      ctx.get<bool>("-f", false) || ctx.get<bool>("--print-file-name", false);
  cfg.include_all_whitespace = ctx.get<bool>("-w", false) ||
                               ctx.get<bool>("--include-all-whitespace", false);

  int numeric_min = ctx.get<int>("-NUM", 0);
  if (numeric_min > 0) {
    cfg.min_length = static_cast<size_t>(numeric_min);
  }

  auto bytes_opt = ctx.get<std::string>("--bytes", "");
  if (bytes_opt.empty()) {
    bytes_opt = ctx.get<std::string>("-n", "");
  }
  if (!bytes_opt.empty()) {
    int val = 0;
    auto [ptr, ec] = std::from_chars(bytes_opt.data(),
                                     bytes_opt.data() + bytes_opt.size(), val);
    if (ec != std::errc() || ptr != bytes_opt.data() + bytes_opt.size() ||
        val < 1) {
      return std::unexpected("invalid minimum length");
    }
    cfg.min_length = static_cast<size_t>(val);
  }

  auto radix_opt = ctx.get<std::string>("--radix", "");
  if (radix_opt.empty()) {
    radix_opt = ctx.get<std::string>("-t", "");
  }
  if (!radix_opt.empty()) {
    if (radix_opt == "o" || radix_opt == "octal")
      cfg.radix = Radix::Octal;
    else if (radix_opt == "d" || radix_opt == "decimal")
      cfg.radix = Radix::Decimal;
    else if (radix_opt == "x" || radix_opt == "hex")
      cfg.radix = Radix::Hex;
    else
      return std::unexpected("invalid radix");
  }

  // -o is alias for -t o
  if (ctx.get<bool>("-o", false)) {
    cfg.radix = Radix::Octal;
  }

  auto enc_opt = ctx.get<std::string>("--encoding", "");
  if (enc_opt.empty()) {
    enc_opt = ctx.get<std::string>("-e", "");
  }
  if (!enc_opt.empty()) {
    if (enc_opt == "s" || enc_opt == "ascii")
      cfg.encoding = Encoding::ASCII;
    else if (enc_opt == "S" || enc_opt == "UTF8")
      cfg.encoding = Encoding::UTF8;
    else if (enc_opt == "b")
      cfg.encoding = Encoding::BigEndian16;
    else if (enc_opt == "l")
      cfg.encoding = Encoding::LittleEndian16;
    else if (enc_opt == "B")
      cfg.encoding = Encoding::BigEndian32;
    else if (enc_opt == "L")
      cfg.encoding = Encoding::LittleEndian32;
    else
      return std::unexpected("invalid encoding");
  }

  if (ctx.has("--output-separator") || ctx.has("-s")) {
    auto sep_opt = ctx.get<std::string>("--output-separator", "");
    if (sep_opt.empty()) {
      sep_opt = ctx.get<std::string>("-s", "");
    }
    cfg.output_separator = sep_opt;
  }

  cfg.target = ctx.get<std::string>("--target", "");
  if (cfg.target.empty()) cfg.target = ctx.get<std::string>("-T", "");

  auto unicode_opt = ctx.get<std::string>("--unicode", "");
  if (unicode_opt.empty()) unicode_opt = ctx.get<std::string>("-U", "");
  if (!unicode_opt.empty()) {
    if (unicode_opt == "default" || unicode_opt == "d") {
      cfg.unicode_mode = "default";
    } else if (unicode_opt == "locale" || unicode_opt == "l" ||
               unicode_opt == "escape" || unicode_opt == "e" ||
               unicode_opt == "invalid" || unicode_opt == "i" ||
               unicode_opt == "hex" || unicode_opt == "x" ||
               unicode_opt == "highlight" || unicode_opt == "h") {
      cfg.unicode_mode = unicode_opt;
      cfg.encoding = Encoding::UTF8;
    } else {
      return std::unexpected("invalid unicode mode");
    }
  }

  for (const auto& pos : ctx.positionals) {
    cfg.files.push_back(std::string(pos));
  }

  return cfg;
}

auto print_offset(size_t offset, Radix radix) -> void {
  switch (radix) {
    case Radix::Octal:
      safePrint(std::format("{:7o} ", offset));
      break;
    case Radix::Decimal:
      safePrint(std::format("{:7d} ", offset));
      break;
    case Radix::Hex:
      safePrint(std::format("{:7x} ", offset));
      break;
    case Radix::None:
      break;
  }
}

auto encoding_width(Encoding encoding) -> size_t {
  switch (encoding) {
    case Encoding::ASCII:
    case Encoding::UTF8:
      return 1;
    case Encoding::BigEndian16:
    case Encoding::LittleEndian16:
      return 2;
    case Encoding::BigEndian32:
    case Encoding::LittleEndian32:
      return 4;
  }
  return 1;
}

auto read_encoded_char(const std::vector<uint8_t>& data, size_t offset,
                       Encoding encoding) -> std::optional<uint32_t> {
  size_t width = encoding_width(encoding);
  if (offset + width > data.size()) {
    return std::nullopt;
  }

  uint32_t value = 0;
  switch (encoding) {
    case Encoding::ASCII:
    case Encoding::UTF8:
      return data[offset];
    case Encoding::BigEndian16:
      return (static_cast<uint32_t>(data[offset]) << 8) |
             static_cast<uint32_t>(data[offset + 1]);
    case Encoding::LittleEndian16:
      return (static_cast<uint32_t>(data[offset + 1]) << 8) |
             static_cast<uint32_t>(data[offset]);
    case Encoding::BigEndian32:
      for (size_t i = 0; i < 4; ++i) {
        value = (value << 8) | static_cast<uint32_t>(data[offset + i]);
      }
      return value;
    case Encoding::LittleEndian32:
      for (size_t i = 0; i < 4; ++i) {
        value |= static_cast<uint32_t>(data[offset + i]) << (8 * i);
      }
      return value;
  }
  return std::nullopt;
}

auto is_graphic_char(uint32_t ch, const Config& cfg) -> bool {
  if (ch > 255) return false;
  unsigned char byte = static_cast<unsigned char>(ch);
  if (byte == '\t') return true;
  if (cfg.include_all_whitespace && std::isspace(byte)) return true;
  if (byte >= 32 && byte <= 126) return true;
  return cfg.encoding == Encoding::UTF8 && byte > 127;
}

auto output_char(uint32_t ch) -> char { return static_cast<char>(ch & 0xff); }

auto print_string_record(const Config& cfg, const std::string& filename,
                         size_t offset, std::string_view text) -> void {
  if (cfg.print_filenames) {
    safePrint(filename);
    safePrint(": ");
  }
  print_offset(offset, cfg.radix);
  safePrint(text);
  safePrint(cfg.output_separator);
}

auto extract_strings_buffer(const std::vector<uint8_t>& data,
                            const std::string& filename, const Config& cfg)
    -> void {
  size_t pos = 0;
  const size_t width = encoding_width(cfg.encoding);

  while (pos < data.size()) {
    size_t scan = pos;
    std::string current;
    bool enough = true;

    for (size_t i = 0; i < cfg.min_length; ++i) {
      auto ch = read_encoded_char(data, scan, cfg.encoding);
      if (!ch || !is_graphic_char(*ch, cfg)) {
        enough = false;
        break;
      }
      current.push_back(output_char(*ch));
      scan += width;
    }

    if (!enough) {
      ++pos;
      continue;
    }

    bool stopped_on_graphic_boundary = false;
    while (true) {
      auto ch = read_encoded_char(data, scan, cfg.encoding);
      if (!ch) break;
      if (!is_graphic_char(*ch, cfg)) {
        stopped_on_graphic_boundary = true;
        break;
      }
      current.push_back(output_char(*ch));
      scan += width;
    }

    print_string_record(cfg, filename, pos, current);
    pos = stopped_on_graphic_boundary ? scan + 1 : scan;
  }
}

auto extract_strings_from_file(const std::string& filename, const Config& cfg)
    -> int {
  std::vector<uint8_t> data;

  if (filename.empty() || filename == "-") {
    // Read from stdin
    char buf[4096];
    while (std::cin.read(buf, sizeof(buf))) {
      data.insert(data.end(), buf, buf + std::cin.gcount());
    }
    if (std::cin.gcount() > 0) {
      data.insert(data.end(), buf, buf + std::cin.gcount());
    }
  } else {
    // Read from file
    std::ifstream file(filename, std::ios::binary);
    if (!file) {
      safeErrorPrint("strings: '");
      safeErrorPrint(filename);
      safeErrorPrintLn("': No such file or directory");
      return 1;
    }
    data.assign(std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
  }

  std::string display_name =
      filename.empty() || filename == "-" ? "{standard input}" : filename;
  extract_strings_buffer(data, display_name, cfg);

  return 0;
}

auto run(const Config& cfg) -> int {
  if (cfg.files.empty()) {
    return extract_strings_from_file("", cfg);
  }

  int result = 0;
  for (const auto& file : cfg.files) {
    if (extract_strings_from_file(file, cfg) != 0) {
      result = 1;
    }
  }
  return result;
}

}  // namespace strings_pipeline

REGISTER_COMMAND(
    strings, "strings", "strings [OPTION]... [FILE]...",
    "Print printable strings from FILE(s).\n"
    "\n"
    "For each FILE, write to standard output all printable character "
    "sequences\n"
    "that are at least MIN characters long. With no FILE, read standard "
    "input.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -a, --all                scan each file in its entirety (default)\n"
    "  -n, --bytes=MIN          print sequences of at least MIN characters "
    "(default 4)\n"
    "  -t, --radix=RADIX        print offset before each string: o=octal, "
    "d=decimal, x=hex\n"
    "  -e, --encoding=ENCODING  select character encoding:\n"
    "                             s=7-bit-ascii (default), S=8-bit-UTF8,\n"
    "                             b=16-bit-big-endian, "
    "l=16-bit-little-endian,\n"
    "                             B=32-bit-big-endian, L=32-bit-little-endian\n"
    "  -o                       print offset before each string (alias for -t "
    "o)\n"
    "\n"
    "Exit status:\n"
    "  0  if any string was found\n"
    "  1  if no string was found or error occurred",
    "  strings a.out            print strings from binary\n"
    "  strings -n 10 a.out     print strings of at least 10 chars\n"
    "  strings -t x a.out      print strings with hex offsets\n"
    "  strings -e S a.out      print UTF-8 strings",
    "grep(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", STRINGS_OPTIONS) {
  using namespace strings_pipeline;

  auto cfg_result = build_config(ctx);
  if (!cfg_result) {
    cp::report_error(cfg_result, L"strings");
    return 1;
  }

  return run(*cfg_result);
}
