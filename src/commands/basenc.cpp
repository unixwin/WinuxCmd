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
 *  - File: basenc.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for basenc command (multiple encodings).
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

auto constexpr BASENC_OPTIONS =
    std::array{OPTION("-d", "--decode", "decode data"),
               OPTION("-b", "", "baseN", INT_TYPE),
               OPTION("-w", "", "wrap at COLS", INT_TYPE)};

// ======================================================
// Helper functions
// ======================================================

namespace {
enum class Encoding { BASE64, BASE32, BASE16, BASE2URL };

// Base64 encoding
std::string base64_encode(const std::string& data, int wrap = 76) {
  const char* alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string result;

  for (size_t i = 0; i < data.size(); i += 3) {
    unsigned char b0 = data[i];
    unsigned char b1 = (i + 1 < data.size()) ? data[i + 1] : 0;
    unsigned char b2 = (i + 2 < data.size()) ? data[i + 2] : 0;

    result += alphabet[b0 >> 2];
    result += alphabet[((b0 & 0x03) << 4) | (b1 >> 4)];
    result += alphabet[((b1 & 0x0F) << 2) | (b2 >> 6)];
    result += alphabet[b2 & 0x3F];
  }

  // Add padding
  size_t padding = (3 - data.size() % 3) % 3;
  result.resize(result.size() - padding);
  result.append(padding, '=');

  // Wrap lines
  if (wrap > 0) {
    std::string wrapped;
    for (size_t i = 0; i < result.size(); i += wrap) {
      if (!wrapped.empty()) wrapped += '\n';
      wrapped += result.substr(i, wrap);
    }
    return wrapped;
  }

  return result;
}

// Base64 decoding
std::string base64_decode(const std::string& encoded) {
  const signed char decode_table[] = {
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57,
      58, 59, 60, 61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,
      7,  8,  9,  10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
      25, -1, -1, -1, -1, -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36,
      37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
      -1, -1, -1, -1};

  std::string result;
  unsigned char buffer[4] = {0};
  size_t buffer_pos = 0;

  for (char c : encoded) {
    if (c == '=' || c == '\n' || c == '\r' || c == ' ' || c == '\t') continue;
    signed char value = decode_table[static_cast<unsigned char>(c)];
    if (value < 0) continue;

    buffer[buffer_pos++] = static_cast<unsigned char>(value);

    if (buffer_pos == 4) {
      result.push_back((buffer[0] << 2) | (buffer[1] >> 4));
      result.push_back(((buffer[1] & 0x0F) << 4) | (buffer[2] >> 2));
      result.push_back(((buffer[2] & 0x03) << 6) | buffer[3]);
      buffer_pos = 0;
    }
  }

  if (buffer_pos >= 2) {
    result.push_back((buffer[0] << 2) | (buffer[1] >> 4));
  }
  if (buffer_pos >= 3) {
    result.push_back(((buffer[1] & 0x0F) << 4) | (buffer[2] >> 2));
  }

  return result;
}

// Base32 encoding (RFC 4648)
std::string base32_encode(const std::string& data, int wrap = 76) {
  const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
  std::string result;

  for (size_t i = 0; i < data.size(); i += 5) {
    unsigned char b[5] = {0};
    size_t block_size = 0;
    for (size_t j = 0; j < 5 && i + j < data.size(); ++j) {
      b[j] = data[i + j];
      block_size++;
    }

    if (block_size >= 1) {
      result += alphabet[b[0] >> 3];
      result +=
          alphabet[((b[0] & 0x07) << 2) | (block_size >= 2 ? b[1] >> 6 : 0)];
    }
    if (block_size >= 2) {
      result += alphabet[(b[1] >> 1) & 0x1F];
      result +=
          alphabet[((b[1] & 0x01) << 4) | (block_size >= 3 ? b[2] >> 4 : 0)];
    }
    if (block_size >= 3) {
      result +=
          alphabet[((b[2] & 0x0F) << 1) | (block_size >= 4 ? b[3] >> 7 : 0)];
      result += alphabet[(block_size >= 4) ? ((b[3] >> 2) & 0x1F) : '='];
    }
    if (block_size >= 4) {
      result +=
          alphabet[((b[3] & 0x03) << 3) | (block_size >= 5 ? b[4] >> 5 : 0)];
      result += alphabet[(block_size >= 5) ? (b[4] & 0x1F) : '='];
    }

    if (block_size == 1)
      result.append(6, '=');
    else if (block_size == 2)
      result.append(4, '=');
    else if (block_size == 3)
      result.append(3, '=');
    else if (block_size == 4)
      result.append(1, '=');
  }

  if (wrap > 0) {
    std::string wrapped;
    for (size_t i = 0; i < result.size(); i += wrap) {
      if (!wrapped.empty()) wrapped += '\n';
      wrapped += result.substr(i, wrap);
    }
    return wrapped;
  }

  return result;
}

// Base16 (hex) encoding
std::string base16_encode(const std::string& data, int wrap = 76) {
  const char* alphabet = "0123456789ABCDEF";
  std::string result;

  for (unsigned char c : data) {
    result += alphabet[c >> 4];
    result += alphabet[c & 0x0F];
  }

  if (wrap > 0) {
    std::string wrapped;
    for (size_t i = 0; i < result.size(); i += wrap) {
      if (!wrapped.empty()) wrapped += '\n';
      wrapped += result.substr(i, wrap);
    }
    return wrapped;
  }

  return result;
}

// Base16 (hex) decoding
std::string base16_decode(const std::string& encoded) {
  std::string result;

  for (size_t i = 0; i + 1 < encoded.size(); i += 2) {
    char c1 = encoded[i];
    char c2 = encoded[i + 1];

    if (c1 == '\n' || c1 == '\r' || c1 == ' ' || c1 == '\t') {
      i--;
      continue;
    }
    if (c2 == '\n' || c2 == '\r' || c2 == ' ' || c2 == '\t') {
      i++;
      continue;
    }

    unsigned char value = 0;
    if (c1 >= '0' && c1 <= '9')
      value = (c1 - '0') << 4;
    else if (c1 >= 'A' && c1 <= 'F')
      value = (c1 - 'A' + 10) << 4;
    else if (c1 >= 'a' && c1 <= 'f')
      value = (c1 - 'a' + 10) << 4;

    if (c2 >= '0' && c2 <= '9')
      value |= (c2 - '0');
    else if (c2 >= 'A' && c2 <= 'F')
      value |= (c2 - 'A' + 10);
    else if (c2 >= 'a' && c2 <= 'f')
      value |= (c2 - 'a' + 10);

    result.push_back(value);
  }

  return result;
}

// Base2 (binary) encoding
std::string base2_encode(const std::string& data, int wrap = 76) {
  std::string result;

  for (unsigned char c : data) {
    for (int i = 7; i >= 0; --i) {
      result += ((c >> i) & 1) ? '1' : '0';
    }
  }

  if (wrap > 0) {
    std::string wrapped;
    for (size_t i = 0; i < result.size(); i += wrap) {
      if (!wrapped.empty()) wrapped += '\n';
      wrapped += result.substr(i, wrap);
    }
    return wrapped;
  }

  return result;
}

// Determine encoding from argument
Encoding parse_encoding(const std::string& str) {
  if (str == "64" || str == "base64") return Encoding::BASE64;
  if (str == "32" || str == "base32") return Encoding::BASE32;
  if (str == "16" || str == "base16" || str == "hex") return Encoding::BASE16;
  if (str == "2" || str == "base2" || str == "bin") return Encoding::BASE2URL;
  return Encoding::BASE64;
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    basenc, "basenc", "basenc [OPTION]... [FILE]",
    "Encode or decode FILE, or standard input, using various encodings.\n"
    "Encode or decode data using multiple encoding schemes: base64, base32,\n"
    "base16 (hex), or base2 (binary). Default is base64.",
    "  basenc file.txt\n"
    "  echo 'Hello' | basenc --base64\n"
    "  basenc -d --base32 encoded.txt\n"
    "  basenc -b16 file.bin",
    "base64(1), base32(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
    BASENC_OPTIONS) {
  bool decode = ctx.get<bool>("-d", false) || ctx.get<bool>("--decode", false);
  int wrap = 76;
  Encoding encoding = Encoding::BASE64;

  if (ctx.get<bool>("-b", false)) {
    encoding = parse_encoding(ctx.get<std::string>("-b", ""));
  }
  if (ctx.get<bool>("--base64", false)) encoding = Encoding::BASE64;
  if (ctx.get<bool>("--base32", false)) encoding = Encoding::BASE32;
  if (ctx.get<bool>("--base16", false) || ctx.get<bool>("--hex", false))
    encoding = Encoding::BASE16;
  if (ctx.get<bool>("--base2", false) || ctx.get<bool>("--bin", false))
    encoding = Encoding::BASE2URL;
  if (ctx.get<bool>("-w", false)) {
    try {
      wrap = std::stoi(ctx.get<std::string>("-w", "76"));
    } catch (...) {
    }
  }

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
      safeErrorPrintLn("basenc: cannot open '" +
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

  // Process based on encoding
  std::string output;
  switch (encoding) {
    case Encoding::BASE64:
      if (decode) {
        output = base64_decode(input);
      } else {
        output = base64_encode(input, wrap);
      }
      break;
    case Encoding::BASE32:
      if (decode) {
        output = "basenc: base32 decode not implemented\n";
        return 1;
      } else {
        output = base32_encode(input, wrap);
      }
      break;
    case Encoding::BASE16:
      if (decode) {
        output = base16_decode(input);
      } else {
        output = base16_encode(input, wrap);
      }
      break;
    case Encoding::BASE2URL:
      if (decode) {
        output = "basenc: base2 decode not implemented\n";
        return 1;
      } else {
        output = base2_encode(input, wrap);
      }
      break;
  }

  safePrintLn(output);

  return 0;
}
