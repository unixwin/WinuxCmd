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
 *  - File: strings.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr STRINGS_OPTIONS = std::array{
    OPTION("-a", "--all",
           "scan each file in its entirety (default)"),
    OPTION("-n", "--bytes",
           "print sequences of at least MIN printable characters (default 4)",
           STRING_TYPE),
    OPTION("-t", "--radix",
           "print the offset within the file before each string",
           STRING_TYPE),
    OPTION("-e", "--encoding",
           "select character encoding: s=7-bit-ascii, S=8-bit-UTF8, b=16-bit-big-endian, l=16-bit-little-endian, B=32-bit-big-endian, L=32-bit-little-endian",
           STRING_TYPE),
    OPTION("-o", "",
           "print offset before each string (alias for -t o)")};

namespace strings_pipeline {
namespace cp = core::pipeline;

enum class Encoding { ASCII, UTF8, BigEndian16, LittleEndian16, BigEndian32, LittleEndian32 };
enum class Radix { None, Octal, Decimal, Hex };

struct Config {
  size_t min_length = 4;
  Radix radix = Radix::None;
  Encoding encoding = Encoding::ASCII;
  bool all = false;
  SmallVector<std::string, 64> files;
};

auto build_config(const CommandContext<STRINGS_OPTIONS.size()>& ctx)
    -> cp::Result<Config> {
  Config cfg;

  cfg.all = ctx.get<bool>("-a", false) || ctx.get<bool>("--all", false);

  auto bytes_opt = ctx.get<std::string>("--bytes", "");
  if (bytes_opt.empty()) {
    bytes_opt = ctx.get<std::string>("-n", "");
  }
  if (!bytes_opt.empty()) {
    try {
      int val = std::stoi(bytes_opt);
      if (val < 1) return std::unexpected("invalid minimum length");
      cfg.min_length = static_cast<size_t>(val);
    } catch (...) {
      return std::unexpected("invalid minimum length");
    }
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

  for (const auto& pos : ctx.positionals) {
    cfg.files.push_back(std::string(pos));
  }

  return cfg;
}

auto print_offset(size_t offset, Radix radix) -> void {
  switch (radix) {
    case Radix::Octal:
      safePrint(std::format("{:7o}  ", offset));
      break;
    case Radix::Decimal:
      safePrint(std::format("{:7d}  ", offset));
      break;
    case Radix::Hex:
      safePrint(std::format("{:7x}  ", offset));
      break;
    case Radix::None:
      break;
  }
}

auto is_printable(unsigned char ch) -> bool {
  return ch >= 32 && ch <= 126;
}

auto extract_strings_ascii(const std::vector<uint8_t>& data, size_t min_length,
                           Radix radix) -> void {
  std::string current;
  size_t start_offset = 0;

  for (size_t i = 0; i < data.size(); ++i) {
    if (is_printable(data[i])) {
      if (current.empty()) start_offset = i;
      current.push_back(static_cast<char>(data[i]));
    } else {
      if (current.size() >= min_length) {
        print_offset(start_offset, radix);
        safePrintLn(current);
      }
      current.clear();
    }
  }

  if (current.size() >= min_length) {
    print_offset(start_offset, radix);
    safePrintLn(current);
  }
}

auto extract_strings_utf8(const std::vector<uint8_t>& data, size_t min_length,
                          Radix radix) -> void {
  std::string current;
  size_t start_offset = 0;

  for (size_t i = 0; i < data.size(); ++i) {
    unsigned char ch = data[i];
    if (ch >= 32) {
      if (current.empty()) start_offset = i;
      current.push_back(static_cast<char>(ch));
    } else {
      if (current.size() >= min_length) {
        print_offset(start_offset, radix);
        safePrintLn(current);
      }
      current.clear();
    }
  }

  if (current.size() >= min_length) {
    print_offset(start_offset, radix);
    safePrintLn(current);
  }
}

auto extract_strings_from_file(const std::string& filename,
                               const Config& cfg) -> int {
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

  switch (cfg.encoding) {
    case Encoding::ASCII:
    case Encoding::UTF8:
      extract_strings_ascii(data, cfg.min_length, cfg.radix);
      break;
    default:
      // For other encodings, treat as ASCII
      extract_strings_ascii(data, cfg.min_length, cfg.radix);
      break;
  }

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
    strings, "strings",
    "strings [OPTION]... [FILE]...",
    "Print printable strings from FILE(s).\n"
    "\n"
    "For each FILE, write to standard output all printable character sequences\n"
    "that are at least MIN characters long. With no FILE, read standard input.\n"
    "\n"
    "Mandatory arguments to long options are mandatory for short options too.\n"
    "\n"
    "  -a, --all                scan each file in its entirety (default)\n"
    "  -n, --bytes=MIN          print sequences of at least MIN characters (default 4)\n"
    "  -t, --radix=RADIX        print offset before each string: o=octal, d=decimal, x=hex\n"
    "  -e, --encoding=ENCODING  select character encoding:\n"
    "                             s=7-bit-ascii (default), S=8-bit-UTF8,\n"
    "                             b=16-bit-big-endian, l=16-bit-little-endian,\n"
    "                             B=32-bit-big-endian, L=32-bit-little-endian\n"
    "  -o                       print offset before each string (alias for -t o)\n"
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
