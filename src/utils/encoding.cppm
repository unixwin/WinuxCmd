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
inline size_t wrapped_encoded_size(size_t encoded_size, int wrap) {
  if (encoded_size == 0 || wrap <= 0) return encoded_size;
  return encoded_size + ((encoded_size - 1) / static_cast<size_t>(wrap));
}

/**
 * @brief Encode data to base64 with a caller-provided alphabet
 * @param data Input data to encode
 * @param alphabet 64-character encoding alphabet
 * @param wrap Line wrap width (0 for no wrapping)
 * @return Base64 encoded string
 */
inline std::string base64_encode(std::span<const uint8_t> data,
                                 std::string_view alphabet, int wrap = 0) {
  const size_t encoded_size = ((data.size() + 2) / 3) * 4;
  const size_t wrap_width = wrap > 0 ? static_cast<size_t>(wrap) : 0;
  const char* alpha = alphabet.data();
  std::string result(wrapped_encoded_size(encoded_size, wrap), '\0');
  size_t out = 0;
  size_t column = 0;

  const size_t full_size = (data.size() / 3) * 3;
  if (wrap_width == 0) {
    for (size_t i = 0; i < full_size; i += 3) {
      const uint8_t b0 = data[i];
      const uint8_t b1 = data[i + 1];
      const uint8_t b2 = data[i + 2];
      result[out++] = alpha[b0 >> 2];
      result[out++] = alpha[((b0 & 0x03) << 4) | (b1 >> 4)];
      result[out++] = alpha[((b1 & 0x0f) << 2) | (b2 >> 6)];
      result[out++] = alpha[b2 & 0x3f];
    }
  } else if ((wrap_width % 4) == 0) {
    for (size_t i = 0; i < full_size; i += 3) {
      if (column == wrap_width) {
        result[out++] = '\n';
        column = 0;
      }
      const uint8_t b0 = data[i];
      const uint8_t b1 = data[i + 1];
      const uint8_t b2 = data[i + 2];
      result[out++] = alpha[b0 >> 2];
      result[out++] = alpha[((b0 & 0x03) << 4) | (b1 >> 4)];
      result[out++] = alpha[((b1 & 0x0f) << 2) | (b2 >> 6)];
      result[out++] = alpha[b2 & 0x3f];
      column += 4;
    }
  } else {
    for (size_t i = 0; i < full_size; i += 3) {
      const uint8_t b0 = data[i];
      const uint8_t b1 = data[i + 1];
      const uint8_t b2 = data[i + 2];
      char quartet[4] = {
          alpha[b0 >> 2],
          alpha[((b0 & 0x03) << 4) | (b1 >> 4)],
          alpha[((b1 & 0x0f) << 2) | (b2 >> 6)],
          alpha[b2 & 0x3f],
      };
      for (char c : quartet) {
        if (column == wrap_width) {
          result[out++] = '\n';
          column = 0;
        }
        result[out++] = c;
        ++column;
      }
    }
  }

  const size_t remaining = data.size() - full_size;
  if (remaining > 0) {
    if (wrap_width > 0 && column == wrap_width) {
      result[out++] = '\n';
      column = 0;
    }
    const uint8_t b0 = data[full_size];
    const uint8_t b1 = remaining == 2 ? data[full_size + 1] : 0;
    result[out++] = alpha[b0 >> 2];
    result[out++] = alpha[((b0 & 0x03) << 4) | (b1 >> 4)];
    result[out++] = remaining == 2 ? alpha[(b1 & 0x0f) << 2] : '=';
    result[out++] = '=';
  }

  result.resize(out);
  return result;
}
inline std::string base64_encode(std::span<const uint8_t> data, int wrap = 0) {
  return base64_encode(data, std::string_view(base64_detail::ENCODE_TABLE, 64),
                       wrap);
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
inline std::string base32_encode(std::span<const uint8_t> data,
                                 std::string_view alphabet, int wrap = 0) {
  const size_t encoded_size = ((data.size() + 4) / 5) * 8;
  const size_t wrap_width = wrap > 0 ? static_cast<size_t>(wrap) : 0;
  const char* alpha = alphabet.data();
  const char pad = char{61};
  const char nl = char{10};
  std::string result(wrapped_encoded_size(encoded_size, wrap), char{0});
  size_t out = 0;
  size_t column = 0;

  const size_t full_size = (data.size() / 5) * 5;
  if (wrap_width == 0) {
    for (size_t i = 0; i < full_size; i += 5) {
      const uint8_t b0 = data[i];
      const uint8_t b1 = data[i + 1];
      const uint8_t b2 = data[i + 2];
      const uint8_t b3 = data[i + 3];
      const uint8_t b4 = data[i + 4];
      result[out++] = alpha[(b0 >> 3) & 0x1f];
      result[out++] = alpha[((b0 & 0x07) << 2) | (b1 >> 6)];
      result[out++] = alpha[(b1 >> 1) & 0x1f];
      result[out++] = alpha[((b1 & 0x01) << 4) | (b2 >> 4)];
      result[out++] = alpha[((b2 & 0x0f) << 1) | (b3 >> 7)];
      result[out++] = alpha[(b3 >> 2) & 0x1f];
      result[out++] = alpha[((b3 & 0x03) << 3) | (b4 >> 5)];
      result[out++] = alpha[b4 & 0x1f];
    }
  } else if ((wrap_width % 8) == 0) {
    for (size_t i = 0; i < full_size; i += 5) {
      if (column == wrap_width) {
        result[out++] = nl;
        column = 0;
      }
      const uint8_t b0 = data[i];
      const uint8_t b1 = data[i + 1];
      const uint8_t b2 = data[i + 2];
      const uint8_t b3 = data[i + 3];
      const uint8_t b4 = data[i + 4];
      result[out++] = alpha[(b0 >> 3) & 0x1f];
      result[out++] = alpha[((b0 & 0x07) << 2) | (b1 >> 6)];
      result[out++] = alpha[(b1 >> 1) & 0x1f];
      result[out++] = alpha[((b1 & 0x01) << 4) | (b2 >> 4)];
      result[out++] = alpha[((b2 & 0x0f) << 1) | (b3 >> 7)];
      result[out++] = alpha[(b3 >> 2) & 0x1f];
      result[out++] = alpha[((b3 & 0x03) << 3) | (b4 >> 5)];
      result[out++] = alpha[b4 & 0x1f];
      column += 8;
    }
  } else {
    for (size_t i = 0; i < full_size; i += 5) {
      const uint8_t b0 = data[i];
      const uint8_t b1 = data[i + 1];
      const uint8_t b2 = data[i + 2];
      const uint8_t b3 = data[i + 3];
      const uint8_t b4 = data[i + 4];
      char octet[8] = {
          alpha[(b0 >> 3) & 0x1f],
          alpha[((b0 & 0x07) << 2) | (b1 >> 6)],
          alpha[(b1 >> 1) & 0x1f],
          alpha[((b1 & 0x01) << 4) | (b2 >> 4)],
          alpha[((b2 & 0x0f) << 1) | (b3 >> 7)],
          alpha[(b3 >> 2) & 0x1f],
          alpha[((b3 & 0x03) << 3) | (b4 >> 5)],
          alpha[b4 & 0x1f],
      };
      for (char c : octet) {
        if (column == wrap_width) {
          result[out++] = nl;
          column = 0;
        }
        result[out++] = c;
        ++column;
      }
    }
  }

  const size_t remaining = data.size() - full_size;
  if (remaining > 0) {
    if (wrap_width > 0 && column == wrap_width) {
      result[out++] = nl;
      column = 0;
    }
    const uint8_t b0 = data[full_size];
    const uint8_t b1 = remaining > 1 ? data[full_size + 1] : 0;
    const uint8_t b2 = remaining > 2 ? data[full_size + 2] : 0;
    const uint8_t b3 = remaining > 3 ? data[full_size + 3] : 0;
    char octet[8] = {
        alpha[(b0 >> 3) & 0x1f],
        alpha[((b0 & 0x07) << 2) | (b1 >> 6)],
        remaining > 1 ? alpha[(b1 >> 1) & 0x1f] : pad,
        remaining > 1 ? alpha[((b1 & 0x01) << 4) | (b2 >> 4)] : pad,
        remaining > 2 ? alpha[((b2 & 0x0f) << 1) | (b3 >> 7)] : pad,
        remaining > 3 ? alpha[(b3 >> 2) & 0x1f] : pad,
        remaining > 3 ? alpha[(b3 & 0x03) << 3] : pad,
        pad,
    };
    if (wrap_width == 0 || (wrap_width % 8) == 0) {
      for (char c : octet) result[out++] = c;
    } else {
      for (char c : octet) {
        if (column == wrap_width) {
          result[out++] = nl;
          column = 0;
        }
        result[out++] = c;
        ++column;
      }
    }
  }

  result.resize(out);
  return result;
}

inline std::string base32_encode(std::span<const uint8_t> data, int wrap = 0) {
  return base32_encode(data, std::string_view(base32_detail::ALPHABET, 32),
                       wrap);
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

// ===== GNU-compatible decoders =====
//
// These mirror GNU coreutils' decoders (lib/base64.c, lib/base32.c and the
// per-encoding decoders in src/basenc.c):
//   * '\n' bytes are skipped anywhere in the stream; every other non-alphabet
//     byte stops decoding (or is dropped up front under --ignore-garbage).
//   * Input is consumed in quantum units (4 characters for base64, 8 for
//     base32). '=' padding is only legal at the tail positions of a unit, and
//     a padded unit does not terminate the stream.
//   * Every byte decoded before the first error is still emitted; the caller
//     prints that prefix and then reports "invalid input" with a failure
//     status.
struct GnuDecodeResult {
  std::string output;
  bool ok = true;
};

namespace gnu_decode_detail {
inline auto make_value_map(std::string_view alphabet) -> std::array<int, 256> {
  std::array<int, 256> map;
  map.fill(-1);
  for (size_t i = 0; i < alphabet.size(); ++i) {
    map[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
  }
  return map;
}
}  // namespace gnu_decode_detail

// GNU decode_4(): a partial unit at end of input emits its decodable prefix
// (2 chars -> 1 byte, 3 chars -> 2 bytes) and then fails.
inline GnuDecodeResult base64_decode_gnu(std::string_view encoded,
                                         std::string_view alphabet,
                                         bool ignore_garbage) {
  const auto map = gnu_decode_detail::make_value_map(alphabet);
  GnuDecodeResult result;

  auto decode_unit = [&](std::string_view unit) -> bool {
    const size_t n = unit.size();
    if (n < 2) return false;
    const int v0 = map[static_cast<unsigned char>(unit[0])];
    const int v1 = map[static_cast<unsigned char>(unit[1])];
    if (v0 < 0 || v1 < 0) return false;
    result.output.push_back(static_cast<char>((v0 << 2) | (v1 >> 4)));
    if (n == 2) return false;
    if (unit[2] == '=') {
      if (n != 4 || unit[3] != '=') return false;
      return true;
    }
    const int v2 = map[static_cast<unsigned char>(unit[2])];
    if (v2 < 0) return false;
    result.output.push_back(static_cast<char>(((v1 << 4) & 0xf0) | (v2 >> 2)));
    if (n == 3) return false;
    if (unit[3] == '=') return true;
    const int v3 = map[static_cast<unsigned char>(unit[3])];
    if (v3 < 0) return false;
    result.output.push_back(static_cast<char>(((v2 << 6) & 0xc0) | v3));
    return true;
  };

  std::array<char, 4> unit{};
  size_t n = 0;
  for (unsigned char c : encoded) {
    if (c == '\n') continue;
    if (ignore_garbage && map[c] < 0 && c != '=') continue;
    unit[n++] = static_cast<char>(c);
    if (n == unit.size()) {
      if (!decode_unit(std::string_view(unit.data(), unit.size()))) {
        result.ok = false;
        return result;
      }
      n = 0;
    }
  }
  if (n != 0) {
    decode_unit(std::string_view(unit.data(), n));
    result.ok = false;
  }
  return result;
}

// GNU decode_8(): only complete units of 8 characters decode; a trailing
// partial unit fails without emitting any bytes.
inline GnuDecodeResult base32_decode_gnu(std::string_view encoded,
                                         std::string_view alphabet,
                                         bool ignore_garbage) {
  const auto map = gnu_decode_detail::make_value_map(alphabet);
  GnuDecodeResult result;

  auto decode_unit = [&](std::string_view unit) -> bool {
    if (unit.size() < 8) return false;
    auto at = [&](size_t i) {
      return map[static_cast<unsigned char>(unit[i])];
    };
    auto pad_tail = [&](size_t first) {
      for (size_t i = first; i < 8; ++i) {
        if (unit[i] != '=') return false;
      }
      return true;
    };

    const int v0 = at(0), v1 = at(1);
    if (v0 < 0 || v1 < 0) return false;
    result.output.push_back(static_cast<char>((v0 << 3) | (v1 >> 2)));
    if (unit[2] == '=') return pad_tail(3);
    const int v2 = at(2), v3 = at(3);
    if (v2 < 0 || v3 < 0) return false;
    result.output.push_back(
        static_cast<char>((v1 << 6) | (v2 << 1) | (v3 >> 4)));
    if (unit[4] == '=') return pad_tail(5);
    const int v4 = at(4);
    if (v4 < 0) return false;
    result.output.push_back(static_cast<char>((v3 << 4) | (v4 >> 1)));
    if (unit[5] == '=') return pad_tail(6);
    const int v5 = at(5), v6 = at(6);
    if (v5 < 0 || v6 < 0) return false;
    result.output.push_back(
        static_cast<char>((v4 << 7) | (v5 << 2) | (v6 >> 3)));
    if (unit[7] == '=') return true;
    const int v7 = at(7);
    if (v7 < 0) return false;
    result.output.push_back(static_cast<char>((v6 << 5) | v7));
    return true;
  };

  std::array<char, 8> unit{};
  size_t n = 0;
  for (unsigned char c : encoded) {
    if (c == '\n') continue;
    if (ignore_garbage && map[c] < 0 && c != '=') continue;
    unit[n++] = static_cast<char>(c);
    if (n == unit.size()) {
      if (!decode_unit(std::string_view(unit.data(), unit.size()))) {
        result.ok = false;
        return result;
      }
      n = 0;
    }
  }
  if (n != 0) result.ok = false;
  return result;
}

// GNU base16 decode: hex digits in either case ('\n' skipped, bytes emitted
// as they decode; a dangling nibble or a garbage byte fails).  GNU's B16
// macro accepts both 'A'-'F' and 'a'-'f' (basenc.c:530-542).
inline GnuDecodeResult base16_decode_gnu(std::string_view encoded,
                                         bool ignore_garbage) {
  auto hex_value = [](unsigned char c) -> int {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
  };

  GnuDecodeResult result;
  int high_nibble = -1;
  for (unsigned char c : encoded) {
    if (c == '\n') continue;
    const int value = hex_value(c);
    if (value < 0) {
      if (ignore_garbage) continue;
      result.ok = false;
      return result;
    }
    if (high_nibble < 0) {
      high_nibble = value;
    } else {
      result.output.push_back(static_cast<char>((high_nibble << 4) | value));
      high_nibble = -1;
    }
  }
  if (high_nibble >= 0) result.ok = false;
  return result;
}

// GNU base2 decode: '\n' skipped, '0'/'1' accumulate into octets; a garbage
// byte or a dangling partial octet fails.
inline GnuDecodeResult base2_decode_gnu(std::string_view encoded,
                                        bool least_significant_first,
                                        bool ignore_garbage) {
  GnuDecodeResult result;
  uint8_t octet = 0;
  int bit_pos = 0;
  for (unsigned char c : encoded) {
    if (c == '\n') continue;
    if (c != '0' && c != '1') {
      if (ignore_garbage) continue;
      result.ok = false;
      return result;
    }
    const auto bit = static_cast<uint8_t>(c == '1' ? 1 : 0);
    if (least_significant_first) {
      octet |= static_cast<uint8_t>(bit << bit_pos);
      if (++bit_pos == 8) {
        result.output.push_back(static_cast<char>(octet));
        octet = 0;
        bit_pos = 0;
      }
    } else {
      if (bit_pos == 0) bit_pos = 8;
      --bit_pos;
      octet |= static_cast<uint8_t>(bit << bit_pos);
      if (bit_pos == 0) {
        result.output.push_back(static_cast<char>(octet));
        octet = 0;
      }
    }
  }
  if (bit_pos != 0) result.ok = false;
  return result;
}

// GNU z85 decode: '\n' skipped; each unit of 5 characters emits 4 bytes;
// garbage, an overflowing unit, or a trailing partial unit fails.
inline GnuDecodeResult z85_decode_gnu(std::string_view encoded,
                                      std::string_view alphabet,
                                      bool ignore_garbage) {
  const auto map = gnu_decode_detail::make_value_map(alphabet);
  GnuDecodeResult result;
  std::array<int, 5> unit{};
  size_t n = 0;
  for (unsigned char c : encoded) {
    if (c == '\n') continue;
    const int value = (c >= 33 && c <= 125) ? map[c] : -1;
    if (value < 0) {
      if (ignore_garbage) continue;
      result.ok = false;
      return result;
    }
    unit[n++] = value;
    if (n == unit.size()) {
      uint64_t value64 = 0;
      for (int v : unit) value64 = value64 * 85 + static_cast<uint32_t>(v);
      if (value64 > std::numeric_limits<uint32_t>::max()) {
        result.ok = false;
        return result;
      }
      for (int shift = 24; shift >= 0; shift -= 8) {
        result.output.push_back(static_cast<char>((value64 >> shift) & 0xff));
      }
      n = 0;
    }
  }
  if (n != 0) result.ok = false;
  return result;
}

}  // namespace encoding
