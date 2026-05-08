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
 *  - File: base32.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for base32 command (RFC 4648).
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

auto constexpr BASE32_OPTIONS = std::array{
    OPTION("-d", "--decode", "decode data"),
    OPTION("-w", "", "wrap encoded lines at COLS (default 76)", INT_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Base32 alphabet (RFC 4648)
constexpr char BASE32_ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

// Base32 decode table
constexpr signed char BASE32_DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11,
    12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1,
    -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17,
    18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1};

// Encode data to base32
std::string base32_encode(const std::vector<unsigned char>& data,
                          int wrap = 76) {
  std::string result;
  size_t i = 0;

  while (i < data.size()) {
    // Process 5 bytes at a time
    unsigned char block[5] = {0};
    size_t block_size = 0;

    for (size_t j = 0; j < 5 && i < data.size(); ++j, ++i) {
      block[j] = data[i];
      block_size++;
    }

    // Encode to 8 characters
    unsigned char b0 = block[0];
    unsigned char b1 = block[1];
    unsigned char b2 = block[2];
    unsigned char b3 = block[3];
    unsigned char b4 = block[4];

    if (block_size >= 1) {
      result += BASE32_ALPHABET[(b0 >> 3) & 0x1F];
      result += BASE32_ALPHABET[((b0 & 0x07) << 2) |
                                (block_size >= 2 ? (b1 >> 6) : 0)];
    }
    if (block_size >= 2) {
      result += BASE32_ALPHABET[(b1 >> 1) & 0x1F];
      result += BASE32_ALPHABET[((b1 & 0x01) << 4) |
                                (block_size >= 3 ? (b2 >> 4) : 0)];
    }
    if (block_size >= 3) {
      result += BASE32_ALPHABET[((b2 & 0x0F) << 1) |
                                (block_size >= 4 ? (b3 >> 7) : 0)];
      result += BASE32_ALPHABET[(block_size >= 4) ? ((b3 >> 2) & 0x1F) : '='];
    }
    if (block_size >= 4) {
      result += BASE32_ALPHABET[((b3 & 0x03) << 3) |
                                (block_size >= 5 ? (b4 >> 5) : 0)];
      result += BASE32_ALPHABET[(block_size >= 5) ? (b4 & 0x1F) : '='];
    }

    // Add padding if needed
    if (block_size == 1) {
      result.append(6, '=');
    } else if (block_size == 2) {
      result.append(4, '=');
    } else if (block_size == 3) {
      result.append(3, '=');
    } else if (block_size == 4) {
      result.append(1, '=');
    }
  }

  // Add line wrapping
  if (wrap > 0) {
    std::string wrapped;
    for (size_t i = 0; i < result.size(); i += wrap) {
      if (!wrapped.empty()) {
        wrapped += '\n';
      }
      wrapped += result.substr(i, wrap);
    }
    return wrapped;
  }

  return result;
}

// Decode base32 to data
std::vector<unsigned char> base32_decode(const std::string& encoded) {
  std::vector<unsigned char> result;
  unsigned char buffer[8] = {0};
  size_t buffer_pos = 0;
  size_t padding_count = 0;

  for (char c : encoded) {
    if (c == '=') {
      padding_count++;
      if (padding_count > 6) break;
      continue;
    }
    if (c == '\n' || c == '\r' || c == ' ' || c == '\t') {
      continue;
    }

    signed char value = BASE32_DECODE_TABLE[static_cast<unsigned char>(c)];
    if (value < 0) continue;

    buffer[buffer_pos++] = static_cast<unsigned char>(value);

    if (buffer_pos == 8) {
      // Decode 8 characters to 5 bytes
      result.push_back((buffer[0] << 3) | (buffer[1] >> 2));
      result.push_back(((buffer[1] & 0x03) << 6) | (buffer[2] << 1) |
                       (buffer[3] >> 4));
      result.push_back(((buffer[3] & 0x0F) << 4) | (buffer[4] >> 1));
      result.push_back(((buffer[4] & 0x01) << 7) | (buffer[5] << 2) |
                       (buffer[6] >> 3));
      result.push_back(((buffer[6] & 0x07) << 5) | buffer[7]);

      buffer_pos = 0;
      padding_count = 0;
    }
  }

  // Handle remaining data with padding
  if (buffer_pos > 0) {
    if (buffer_pos >= 2) {
      result.push_back((buffer[0] << 3) | (buffer[1] >> 2));
    }
    if (buffer_pos >= 4) {
      result.push_back(((buffer[1] & 0x03) << 6) | (buffer[2] << 1) |
                       (buffer[3] >> 4));
    }
    if (buffer_pos >= 5) {
      result.push_back(((buffer[3] & 0x0F) << 4) | (buffer[4] >> 1));
    }
    if (buffer_pos >= 7) {
      result.push_back(((buffer[4] & 0x01) << 7) | (buffer[5] << 2) |
                       (buffer[6] >> 3));
    }
  }

  // Remove extra bytes based on padding
  if (padding_count == 1 && result.size() >= 1) {
    result.resize(result.size() - 1);
  } else if (padding_count == 3 && result.size() >= 2) {
    result.resize(result.size() - 2);
  } else if (padding_count == 4 && result.size() >= 3) {
    result.resize(result.size() - 3);
  } else if (padding_count == 6 && result.size() >= 4) {
    result.resize(result.size() - 4);
  }

  return result;
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    base32, "base32", "base32 [OPTION]... [FILE]",
    "Base32 encode or decode FILE, or standard input, to standard output.\n"
    "Encode or decode data using the Base32 encoding scheme (RFC 4648).\n"
    "With no FILE, or when FILE is -, read standard input.",
    "  base32 file.txt\n"
    "  echo 'Hello' | base32\n"
    "  base32 -d encoded.txt\n"
    "  base32 -w 64 file.bin",
    "base64(1), basenc(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    BASE32_OPTIONS) {
  bool decode = ctx.get<bool>("-d", false) || ctx.get<bool>("--decode", false);
  int wrap = ctx.get<int>("-w", 76);

  // Read input
  std::string input;
  if (ctx.positionals.empty() || ctx.positionals[0] == "-") {
    input.assign(std::istreambuf_iterator<char>(std::cin),
                 std::istreambuf_iterator<char>());
  } else {
    std::wstring wfile = utf8_to_wstring(std::string(ctx.positionals[0]));
    HANDLE hFile =
        CreateFileW(wfile.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                    OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
      safeErrorPrintLn("base32: cannot open '" +
                       std::string(ctx.positionals[0]) + "'");
      return 1;
    }

    LARGE_INTEGER fileSize;
    GetFileSizeEx(hFile, &fileSize);
    input.resize(fileSize.QuadPart);
    DWORD bytesRead;
    ReadFile(hFile, input.data(), static_cast<DWORD>(fileSize.QuadPart),
             &bytesRead, nullptr);
    CloseHandle(hFile);
  }

  // Process
  if (decode) {
    auto decoded = encoding::base32_decode(input);
    std::string output(decoded.begin(), decoded.end());
    safePrint(output);
  } else {
    auto data = std::span<const uint8_t>(
        reinterpret_cast<const uint8_t*>(input.data()), input.size());
    std::string encoded = encoding::base32_encode(data, wrap);
    safePrintLn(encoded);
  }

  return 0;
}
