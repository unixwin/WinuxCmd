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

auto constexpr OD_OPTIONS =
    std::array{OPTION("-A", "", "select input base", STRING_TYPE),
               OPTION("-c", "", "select ASCII output"),
               OPTION("-j", "", "skip bytes", STRING_TYPE),
               OPTION("-N", "", "limit bytes", STRING_TYPE),
               OPTION("-t", "", "select output type", STRING_TYPE),
               OPTION("-v", "", "write all input data"),
               OPTION("-x", "", "select hexadecimal 2-byte units")};

// ======================================================
// Helper functions
// ======================================================

// Output format types
enum class OutputType {
  BYTE_OCTAL,
  BYTE_HEX,
  SHORT_HEX,
  INT_HEX,
  INT_DECIMAL,
  FLOAT_DOUBLE,
  ASCII
};

namespace {
// Parse output type
std::vector<OutputType> parse_output_type(const std::string& type_str) {
  std::vector<OutputType> types;

  if (type_str == "a" || type_str.empty()) {
    types.push_back(OutputType::ASCII);
  } else if (type_str == "x") {
    types.push_back(OutputType::SHORT_HEX);
  } else if (type_str == "d") {
    types.push_back(OutputType::INT_DECIMAL);
  } else if (type_str == "f") {
    types.push_back(OutputType::FLOAT_DOUBLE);
  }

  // Parse format specification (e.g., "x1", "x2", "d4")
  if (!type_str.empty()) {
    char format = type_str[0];
    std::string size_str = type_str.substr(1);

    int size = 1;
    if (!size_str.empty()) {
      size = std::stoi(size_str);
    }

    switch (format) {
      case 'o':
        if (size == 1)
          types.push_back(OutputType::BYTE_OCTAL);
        else if (size == 2)
          types.push_back(OutputType::SHORT_HEX);
        else
          types.push_back(OutputType::INT_HEX);
        break;
      case 'x':
        if (size == 1)
          types.push_back(OutputType::BYTE_HEX);
        else if (size == 2)
          types.push_back(OutputType::SHORT_HEX);
        else
          types.push_back(OutputType::INT_HEX);
        break;
      case 'd':
        types.push_back(OutputType::INT_DECIMAL);
        break;
      case 'f':
        types.push_back(OutputType::FLOAT_DOUBLE);
        break;
      case 'a':
        types.push_back(OutputType::ASCII);
        break;
    }
  }

  return types;
}

// Format byte as octal
std::string format_byte_octal(unsigned char b) {
  char buf[8];
  sprintf_s(buf, sizeof(buf), "%03o", b);
  return buf;
}

// Format byte as hex
std::string format_byte_hex(unsigned char b) {
  char buf[8];
  sprintf_s(buf, sizeof(buf), "%02x", b);
  return buf;
}

// Format short as hex
std::string format_short_hex(const unsigned char* data) {
  unsigned short value = static_cast<unsigned short>(data[0] | (data[1] << 8));
  char buf[8];
  sprintf_s(buf, sizeof(buf), "%04x", value);
  return buf;
}

// Format int as hex
std::string format_int_hex(const unsigned char* data) {
  unsigned int value =
      data[0] | (data[1] << 8) | (data[2] << 16) | (data[3] << 24);
  char buf[16];
  sprintf_s(buf, sizeof(buf), "%08x", value);
  return buf;
}

// Format int as decimal
std::string format_int_decimal(const unsigned char* data) {
  int value = static_cast<int>(data[0] | (data[1] << 8) | (data[2] << 16) |
                               (data[3] << 24));
  return std::to_string(value);
}

// Format ASCII character
std::string format_ascii(unsigned char c) {
  if (c >= 32 && c < 127) {
    return std::string(1, c);
  } else if (c == 0) {
    return "\\0";
  } else if (c == '\n') {
    return "\\n";
  } else if (c == '\r') {
    return "\\r";
  } else if (c == '\t') {
    return "\\t";
  } else {
    char buf[8];
    sprintf_s(buf, sizeof(buf), "\\%03o", c);
    return buf;
  }
}

// Format double as float
std::string format_double(const unsigned char* data) {
  double value;
  memcpy(&value, data, sizeof(double));
  char buf[32];
  sprintf_s(buf, sizeof(buf), "%.6g", value);
  return buf;
}

// Dump data using specified format
void dump_data(const std::vector<unsigned char>& data, size_t offset,
               const std::vector<OutputType>& types, bool show_all) {
  const size_t bytes_per_line = 16;
  size_t pos = 0;

  while (pos < data.size()) {
    // Print offset
    char offset_buf[16];
    sprintf_s(offset_buf, sizeof(offset_buf), "%07zx", offset + pos);
    safePrint(offset_buf);

    // Print bytes in hex
    for (size_t i = 0; i < bytes_per_line; ++i) {
      if (pos + i < data.size()) {
        safePrint(" " + format_byte_hex(data[pos + i]));
      } else {
        safePrint("   ");
      }
    }

    safePrint("  ");

    // Print ASCII representation
    for (size_t i = 0; i < bytes_per_line && pos + i < data.size(); ++i) {
      unsigned char c = data[pos + i];
      safePrint((c >= 32 && c < 127) ? std::string(1, c) : ".");
    }

    safePrintLn("\n");
    pos += bytes_per_line;

    // Skip identical lines if not showing all
    if (!show_all && pos < data.size()) {
      size_t next_pos = pos;
      while (next_pos + bytes_per_line <= data.size()) {
        bool same = true;
        for (size_t i = 0; i < bytes_per_line; ++i) {
          if (pos + i >= data.size() || next_pos + i >= data.size() ||
              data[pos + i] != data[next_pos + i]) {
            same = false;
            break;
          }
        }
        if (!same) break;
        next_pos += bytes_per_line;
      }

      if (next_pos > pos) {
        char buf[32];
        sprintf_s(buf, sizeof(buf), "*\n%07zx\n", offset + next_pos);
        safePrint(buf);
        pos = next_pos;
      }
    }
  }

  // Print final offset
  char final_buf[16];
  sprintf_s(final_buf, sizeof(final_buf), "%07zx\n", offset + data.size());
  safePrint(final_buf);
}

// Parse offset string
size_t parse_offset(const std::string& str) {
  size_t offset = 0;
  if (str.substr(0, 2) == "0x") {
    offset = std::stoull(str, nullptr, 16);
  } else if (str[0] == '0') {
    offset = std::stoull(str, nullptr, 8);
  } else {
    offset = std::stoull(str);
  }
  return offset;
}
}  // namespace

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
  // Parse options
  std::string input_base = ctx.get<std::string>("-A", "o");
  size_t skip_bytes = ctx.get<bool>("-j", false)
                          ? parse_offset(ctx.get<std::string>("-j", ""))
                          : 0;
  size_t limit_bytes = ctx.get<bool>("-N", false)
                           ? parse_offset(ctx.get<std::string>("-N", ""))
                           : 0;
  std::string output_type = ctx.get<std::string>("-t", "o2");
  bool show_all = ctx.get<bool>("-v", false);
  bool hex_short = ctx.get<bool>("-x", false);

  // Default output type
  std::vector<OutputType> types = parse_output_type(output_type);
  if (hex_short) {
    types = {OutputType::SHORT_HEX};
  }

  // Read input
  std::vector<unsigned char> data;

  if (ctx.positionals.empty() || std::string(ctx.positionals[0]) == "-") {
    data.assign(std::istreambuf_iterator<char>(std::cin),
                std::istreambuf_iterator<char>());
  } else {
    for (const auto& filename : ctx.positionals) {
      std::string file_arg(filename);
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
        std::wstring wfile = utf8_to_wstring(exp);
        HANDLE hFile =
            CreateFileW(wfile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);

        if (hFile == INVALID_HANDLE_VALUE) {
          safeErrorPrintLn("od: cannot open '" + exp + "'");
          continue;
        }

        LARGE_INTEGER fileSize;
        GetFileSizeEx(hFile, &fileSize);

        size_t old_size = data.size();
        data.resize(old_size + fileSize.QuadPart);
        DWORD bytesRead;
        ReadFile(hFile, data.data() + old_size,
                 static_cast<DWORD>(fileSize.QuadPart), &bytesRead, nullptr);
        data.resize(old_size + bytesRead);
        CloseHandle(hFile);
      }
    }
  }

  // Apply skip and limit
  if (skip_bytes > 0) {
    if (skip_bytes < data.size()) {
      data.erase(data.begin(), data.begin() + skip_bytes);
    } else {
      data.clear();
    }
  }

  if (limit_bytes > 0 && limit_bytes < data.size()) {
    data.resize(limit_bytes);
  }

  // Dump data
  if (data.empty()) {
    char buf[16];
    sprintf_s(buf, sizeof(buf), "%07zx\n", skip_bytes);
    safePrint(buf);
  } else {
    dump_data(data, skip_bytes, types, show_all);
  }

  return 0;
}
