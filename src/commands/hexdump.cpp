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

struct Config {
  DisplayMode mode = DisplayMode::DefaultHex2;
  size_t length = 0;  // 0 = all
  size_t skip = 0;
  bool no_squeeze = false;
  std::string format_string;
  std::string format_file;
  SmallVector<std::string, 64> files;
};

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
  cfg.format_string = ctx.get<std::string>("-e", "");
  if (!cfg.format_string.empty()) {
    return std::unexpected("-e format strings are not yet supported");
  }
  cfg.format_file = ctx.get<std::string>("-f", "");

  auto length_opt = ctx.get<std::string>("-n", "");
  if (!length_opt.empty()) {
    auto [ptr, ec] = std::from_chars(
        length_opt.data(), length_opt.data() + length_opt.size(), cfg.length);
    if (ec != std::errc() || ptr != length_opt.data() + length_opt.size()) {
      return std::unexpected("invalid length");
    }
  }

  auto skip_opt = ctx.get<std::string>("-s", "");
  if (!skip_opt.empty()) {
    auto [ptr, ec] = std::from_chars(
        skip_opt.data(), skip_opt.data() + skip_opt.size(), cfg.skip);
    if (ec != std::errc() || ptr != skip_opt.data() + skip_opt.size()) {
      return std::unexpected("invalid skip");
    }
  }

  for (const auto& pos : ctx.positionals) {
    cfg.files.push_back(std::string(pos));
  }

  return cfg;
}

auto print_canonical(const std::vector<uint8_t>& data, size_t base_offset)
    -> void {
  for (size_t i = 0; i < data.size(); i += 16) {
    safePrint(std::format("{:08x}  ", base_offset + i));

    for (size_t j = 0; j < 16; ++j) {
      if (i + j < data.size()) {
        safePrint(std::format("{:02x}", data[i + j]));
      } else {
        safePrint("  ");
      }
      if (j == 7) safePrint(" ");
      if (j < 15) safePrint(" ");
    }

    safePrint("  |");
    for (size_t j = 0; j < 16 && i + j < data.size(); ++j) {
      unsigned char ch = data[i + j];
      if (ch >= 32 && ch <= 126) {
        safePrint(std::string(1, static_cast<char>(ch)));
      } else {
        safePrint(".");
      }
    }
    safePrintLn("|");
  }

  if (!data.empty()) {
    safePrintLn(std::format("{:08x}", base_offset + data.size()));
  }
}

auto print_hex2(const std::vector<uint8_t>& data, size_t base_offset,
                bool padded_words) -> void {
  for (size_t i = 0; i < data.size(); i += 16) {
    safePrint(std::format("{:07x} ", base_offset + i));
    for (size_t j = 0; j < 16 && i + j + 1 < data.size(); j += 2) {
      uint16_t val = static_cast<uint16_t>(data[i + j]) |
                     (static_cast<uint16_t>(data[i + j + 1]) << 8);
      if (padded_words) {
        safePrint(std::format("   {:04x} ", val));
      } else {
        if (j > 0) safePrint(" ");
        safePrint(std::format("{:04x}", val));
      }
    }
    safePrintLn("");
  }
  if (!data.empty()) {
    safePrintLn(std::format("{:07x}", base_offset + data.size()));
  }
}

auto print_octal2(const std::vector<uint8_t>& data, size_t base_offset)
    -> void {
  for (size_t i = 0; i < data.size(); i += 16) {
    safePrint(std::format("{:07x} ", base_offset + i));
    for (size_t j = 0; j < 16 && i + j + 1 < data.size(); j += 2) {
      uint16_t val = static_cast<uint16_t>(data[i + j]) |
                     (static_cast<uint16_t>(data[i + j + 1]) << 8);
      safePrint(std::format("  {:06o} ", val));
    }
    safePrintLn("");
  }
  if (!data.empty()) {
    safePrintLn(std::format("{:07x}", base_offset + data.size()));
  }
}

auto print_decimal2(const std::vector<uint8_t>& data, size_t base_offset)
    -> void {
  for (size_t i = 0; i < data.size(); i += 16) {
    safePrint(std::format("{:07x} ", base_offset + i));
    for (size_t j = 0; j < 16 && i + j + 1 < data.size(); j += 2) {
      uint16_t val = static_cast<uint16_t>(data[i + j]) |
                     (static_cast<uint16_t>(data[i + j + 1]) << 8);
      safePrint(std::format("  {:05d} ", val));
    }
    safePrintLn("");
  }
  if (!data.empty()) {
    safePrintLn(std::format("{:07x}", base_offset + data.size()));
  }
}

auto print_octal1(const std::vector<uint8_t>& data, size_t base_offset)
    -> void {
  for (size_t i = 0; i < data.size(); i += 16) {
    safePrint(std::format("{:07x} ", base_offset + i));
    for (size_t j = 0; j < 16; ++j) {
      if (i + j < data.size()) {
        safePrint(std::format("{:03o} ", data[i + j]));
      } else {
        safePrint("    ");
      }
    }
    safePrintLn("");
  }
  if (!data.empty()) {
    safePrintLn(std::format("{:07x}", base_offset + data.size()));
  }
}

auto print_char(const std::vector<uint8_t>& data, size_t base_offset) -> void {
  for (size_t i = 0; i < data.size(); i += 16) {
    safePrint(std::format("{:07x} ", base_offset + i));
    for (size_t j = 0; j < 16; ++j) {
      if (i + j >= data.size()) {
        safePrint("    ");
        continue;
      }
      unsigned char ch = data[i + j];
      if (ch == 0)
        safePrint(" \\0 ");
      else if (ch == 7)
        safePrint(" \\a ");
      else if (ch == 8)
        safePrint(" \\b ");
      else if (ch == 9)
        safePrint(" \\t ");
      else if (ch == 10)
        safePrint(" \\n ");
      else if (ch == 11)
        safePrint(" \\v ");
      else if (ch == 12)
        safePrint(" \\f ");
      else if (ch == 13)
        safePrint(" \\r ");
      else if (ch >= 32 && ch <= 126)
        safePrint(std::format("  {} ", static_cast<char>(ch)));
      else
        safePrint(std::format("{:03o} ", ch));
    }
    safePrintLn("");
  }
  if (!data.empty()) {
    safePrintLn(std::format("{:07x}", base_offset + data.size()));
  }
}

auto dump_file(const std::string& filename, const Config& cfg) -> int {
  std::vector<uint8_t> data;

  if (filename.empty() || filename == "-") {
    char buf[4096];
    while (std::cin.read(buf, sizeof(buf))) {
      data.insert(data.end(), buf, buf + std::cin.gcount());
    }
    if (std::cin.gcount() > 0) {
      data.insert(data.end(), buf, buf + std::cin.gcount());
    }
  } else {
    std::ifstream file(native_path::normalize_api_operand(filename),
                       std::ios::binary);
    if (!file) {
      safeErrorPrint("hexdump: '");
      safeErrorPrint(filename);
      safeErrorPrintLn("': No such file or directory");
      return 1;
    }
    data.assign(std::istreambuf_iterator<char>(file),
                std::istreambuf_iterator<char>());
  }

  size_t base_offset = 0;
  if (cfg.skip > 0 && cfg.skip < data.size()) {
    base_offset = cfg.skip;
    data.erase(data.begin(),
               data.begin() + static_cast<std::ptrdiff_t>(cfg.skip));
  } else if (cfg.skip >= data.size()) {
    return 0;
  }

  if (cfg.length > 0 && cfg.length < data.size()) {
    data.resize(cfg.length);
  }

  switch (cfg.mode) {
    case DisplayMode::Canonical:
      print_canonical(data, base_offset);
      break;
    case DisplayMode::DefaultHex2:
      print_hex2(data, base_offset, false);
      break;
    case DisplayMode::Hex2:
      print_hex2(data, base_offset, true);
      break;
    case DisplayMode::Octal2:
      print_octal2(data, base_offset);
      break;
    case DisplayMode::Decimal2:
      print_decimal2(data, base_offset);
      break;
    case DisplayMode::Octal1:
      print_octal1(data, base_offset);
      break;
    case DisplayMode::Char:
      print_char(data, base_offset);
      break;
  }

  return 0;
}

auto run(const Config& cfg) -> int {
  if (cfg.files.empty()) {
    return dump_file("", cfg);
  }

  int result = 0;
  for (const auto& file : cfg.files) {
    if (dump_file(file, cfg) != 0) {
      result = 1;
    }
  }
  return result;
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
