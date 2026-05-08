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
 *  - File: encoding.cppm
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
module;
#include <cstdint>
export module utils:encoding;

import std;

export namespace encoding {

// ===== Base64 =====

namespace base64_detail {
// Base64 encoding table
constexpr char ENCODE_TABLE[64] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'};

// Base64 decoding table (compile-time initialized)
constexpr int8_t DECODE_TABLE[256] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, 62, -1, -1, -1, 63, 52, 53, 54, 55, 56, 57, 58, 59, 60,
    61, -1, -1, -1, -1, -1, -1, -1, 0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  10,
    11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, -1, -1, -1, -1,
    -1, -1, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42,
    43, 44, 45, 46, 47, 48, 49, 50, 51, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
    -1, -1, -1, -1, -1, -1, -1, -1, -1};
}  // namespace base64_detail

/**
 * @brief Encode data to base64
 * @param data Input data to encode
 * @param wrap Line wrap width (0 for no wrapping)
 * @return Base64 encoded string
 */
inline std::string base64_encode(std::span<const uint8_t> data, int wrap = 0) {
  std::string result;
  result.reserve(((data.size() + 2) / 3) * 4);

  for (size_t i = 0; i < data.size(); i += 3) {
    uint32_t triple = 0;
    int padding = 0;

    for (int j = 0; j < 3; ++j) {
      if (i + j < data.size()) {
        triple |= static_cast<uint32_t>(data[i + j]) << (16 - 8 * j);
      } else {
        padding++;
      }
    }

    result += base64_detail::ENCODE_TABLE[(triple >> 18) & 0x3F];
    result += base64_detail::ENCODE_TABLE[(triple >> 12) & 0x3F];

    if (padding >= 2) {
      result += '=';
      result += '=';
    } else {
      result += base64_detail::ENCODE_TABLE[(triple >> 6) & 0x3F];
      if (padding >= 1) {
        result += '=';
      } else {
        result += base64_detail::ENCODE_TABLE[triple & 0x3F];
      }
    }
  }

  // Add line wrapping if requested
  if (wrap > 0) {
    std::string wrapped;
    for (size_t i = 0; i < result.size(); i += static_cast<size_t>(wrap)) {
      if (!wrapped.empty()) {
        wrapped += '\n';
      }
      wrapped += result.substr(i, static_cast<size_t>(wrap));
    }
    return wrapped;
  }

  return result;
}

/**
 * @brief Decode base64 to data
 * @param encoded Base64 encoded string
 * @param ignore_garbage Ignore non-base64 characters
 * @return Decoded data, or empty vector on error
 */
inline std::vector<uint8_t> base64_decode(std::string_view encoded,
                                          bool ignore_garbage = false) {
  std::vector<uint8_t> result;
  result.reserve((encoded.size() / 4) * 3);

  uint32_t triple = 0;
  int bits = 0;

  for (char c : encoded) {
    if (c == '=') break;
    if (c == '\n' || c == '\r') continue;

    int8_t value = base64_detail::DECODE_TABLE[static_cast<uint8_t>(c)];
    if (value < 0) {
      if (!ignore_garbage) {
        return {};  // Return empty vector on error
      }
      continue;
    }

    triple = (triple << 6) | static_cast<uint32_t>(value);
    bits += 6;

    if (bits >= 8) {
      bits -= 8;
      result.push_back(static_cast<uint8_t>((triple >> bits) & 0xFF));
    }
  }

  return result;
}

// ===== Base32 =====

namespace base32_detail {
// Base32 alphabet (RFC 4648)
constexpr char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";

// Base32 decode table
constexpr signed char DECODE_TABLE[256] = {
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
}  // namespace base32_detail

/**
 * @brief Encode data to base32 (RFC 4648)
 * @param data Input data to encode
 * @param wrap Line wrap width (0 for no wrapping)
 * @return Base32 encoded string
 */
inline std::string base32_encode(std::span<const uint8_t> data, int wrap = 0) {
  std::string result;
  size_t i = 0;

  while (i < data.size()) {
    // Process 5 bytes at a time
    uint8_t block[5] = {0};
    size_t block_size = 0;

    for (size_t j = 0; j < 5 && i < data.size(); ++j, ++i) {
      block[j] = data[i];
      block_size++;
    }

    // Encode to 8 characters
    uint8_t b0 = block[0];
    uint8_t b1 = block[1];
    uint8_t b2 = block[2];
    uint8_t b3 = block[3];
    uint8_t b4 = block[4];

    if (block_size >= 1) {
      result += base32_detail::ALPHABET[(b0 >> 3) & 0x1F];
      result += base32_detail::ALPHABET[((b0 & 0x07) << 2) |
                                        (block_size >= 2 ? (b1 >> 6) : 0)];
    }
    if (block_size >= 2) {
      result += base32_detail::ALPHABET[(b1 >> 1) & 0x1F];
      result += base32_detail::ALPHABET[((b1 & 0x01) << 4) |
                                        (block_size >= 3 ? (b2 >> 4) : 0)];
    }
    if (block_size >= 3) {
      result += base32_detail::ALPHABET[((b2 & 0x0F) << 1) |
                                        (block_size >= 4 ? (b3 >> 7) : 0)];
      result +=
          base32_detail::ALPHABET[(block_size >= 4) ? ((b3 >> 2) & 0x1F) : '='];
    }
    if (block_size >= 4) {
      result += base32_detail::ALPHABET[((b3 & 0x03) << 3) |
                                        (block_size >= 5 ? (b4 >> 5) : 0)];
      result += base32_detail::ALPHABET[(block_size >= 5) ? (b4 & 0x1F) : '='];
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

  // Add line wrapping if requested
  if (wrap > 0) {
    std::string wrapped;
    for (size_t j = 0; j < result.size(); j += static_cast<size_t>(wrap)) {
      if (!wrapped.empty()) {
        wrapped += '\n';
      }
      wrapped += result.substr(j, static_cast<size_t>(wrap));
    }
    return wrapped;
  }

  return result;
}

/**
 * @brief Decode base32 to data
 * @param encoded Base32 encoded string
 * @return Decoded data, or empty vector on error
 */
inline std::vector<uint8_t> base32_decode(std::string_view encoded) {
  std::vector<uint8_t> result;
  uint8_t buffer[8] = {0};
  size_t buffer_pos = 0;
  size_t padding_count = 0;

  for (char c : encoded) {
    if (c == '=') {
      padding_count++;
      if (padding_count > 6) break;
      buffer[buffer_pos++] = 0;
    } else if (std::isalnum(static_cast<unsigned char>(c))) {
      signed char value = base32_detail::DECODE_TABLE[static_cast<uint8_t>(
          std::toupper(static_cast<unsigned char>(c)))];
      if (value < 0) {
        return {};  // Invalid character
      }
      buffer[buffer_pos++] = static_cast<uint8_t>(value);
    } else if (c != '\n' && c != '\r' && c != ' ') {
      return {};  // Invalid character
    }

    if (buffer_pos == 8) {
      result.push_back((buffer[0] << 3) | (buffer[1] >> 2));
      result.push_back(((buffer[1] & 0x03) << 6) | (buffer[2] << 1) |
                       (buffer[3] >> 4));
      result.push_back(((buffer[3] & 0x0F) << 4) | (buffer[4] >> 1));
      result.push_back(((buffer[4] & 0x01) << 7) | (buffer[5] << 2) |
                       (buffer[6] >> 3));
      result.push_back(((buffer[6] & 0x07) << 5) | buffer[7]);
      buffer_pos = 0;
    }
  }

  // Handle partial buffer
  if (buffer_pos > 0 && buffer_pos >= 2) {
    result.push_back((buffer[0] << 3) | (buffer[1] >> 2));
  }
  if (buffer_pos > 0 && buffer_pos >= 4) {
    result.push_back(((buffer[1] & 0x03) << 6) | (buffer[2] << 1) |
                     (buffer[3] >> 4));
  }
  if (buffer_pos > 0 && buffer_pos >= 5) {
    result.push_back(((buffer[3] & 0x0F) << 4) | (buffer[4] >> 1));
  }
  if (buffer_pos > 0 && buffer_pos >= 7) {
    result.push_back(((buffer[4] & 0x01) << 7) | (buffer[5] << 2) |
                     (buffer[6] >> 3));
  }

  return result;
}

// ===== Base16 (Hex) =====

namespace base16_detail {
constexpr char ALPHABET_LOWER[] = "0123456789abcdef";
constexpr char ALPHABET_UPPER[] = "0123456789ABCDEF";
}  // namespace base16_detail

/**
 * @brief Encode data to base16 (hexadecimal)
 * @param data Input data to encode
 * @param uppercase Use uppercase letters
 * @return Hex encoded string
 */
inline std::string base16_encode(std::span<const uint8_t> data,
                                 bool uppercase = false) {
  std::string result;
  result.reserve(data.size() * 2);

  const char* alphabet =
      uppercase ? base16_detail::ALPHABET_UPPER : base16_detail::ALPHABET_LOWER;

  for (uint8_t byte : data) {
    result += alphabet[byte >> 4];
    result += alphabet[byte & 0x0F];
  }

  return result;
}

/**
 * @brief Decode base16 (hexadecimal) to data
 * @param encoded Hex encoded string
 * @return Decoded data, or empty vector on error
 */
inline std::vector<uint8_t> base16_decode(std::string_view encoded) {
  std::vector<uint8_t> result;
  result.reserve(encoded.size() / 2);

  uint8_t high_nibble = 0;
  bool has_high_nibble = false;

  for (char c : encoded) {
    uint8_t value = 0;

    if (c >= '0' && c <= '9') {
      value = c - '0';
    } else if (c >= 'A' && c <= 'F') {
      value = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
      value = c - 'a' + 10;
    } else if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
      continue;  // Skip whitespace
    } else {
      return {};  // Invalid character
    }

    if (!has_high_nibble) {
      high_nibble = value;
      has_high_nibble = true;
    } else {
      result.push_back((high_nibble << 4) | value);
      has_high_nibble = false;
    }
  }

  return result;
}

}  // namespace encoding
