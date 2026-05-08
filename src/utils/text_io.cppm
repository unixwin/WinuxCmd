/*
 *  Copyright (c) 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: text_io.cppm
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
export module utils:textio;

import std;
import :utf8;

namespace {
enum class EncodingHint { Utf8, Utf16Le, Utf16Be };

auto guess_utf16_from_nuls(std::string_view bytes)
    -> std::optional<EncodingHint> {
  if (bytes.size() < 2) return std::nullopt;

  size_t even_nul = 0;
  size_t odd_nul = 0;
  for (size_t i = 0; i < bytes.size(); ++i) {
    if (bytes[i] != '\0') continue;
    if ((i & 1U) == 0U)
      ++even_nul;
    else
      ++odd_nul;
  }

  const size_t threshold = bytes.size() / 4;
  if (odd_nul > threshold && odd_nul > even_nul * 2)
    return EncodingHint::Utf16Le;
  if (even_nul > threshold && even_nul > odd_nul * 2)
    return EncodingHint::Utf16Be;
  return std::nullopt;
}

auto decode_utf16(std::string_view bytes, bool little_endian) -> std::string {
  std::wstring wide;
  wide.reserve(bytes.size() / 2);

  size_t i = 0;
  while (i + 1 < bytes.size()) {
    const std::uint8_t b0 = static_cast<std::uint8_t>(bytes[i]);
    const std::uint8_t b1 = static_cast<std::uint8_t>(bytes[i + 1]);
    const std::uint16_t u16 =
        little_endian
            ? static_cast<std::uint16_t>(b0 |
                                         (static_cast<std::uint16_t>(b1) << 8))
            : static_cast<std::uint16_t>((static_cast<std::uint16_t>(b0) << 8) |
                                         b1);
    i += 2;

    if (u16 == 0xFEFF)
      continue;  // Drop BOM markers, including repeated per-line BOMs.
    wide.push_back(static_cast<wchar_t>(u16));
  }

  return wstring_to_utf8(wide);
}
}  // namespace

export auto read_text_stream(std::istream& in) -> std::string {
  std::string bytes{std::istreambuf_iterator<char>{in},
                    std::istreambuf_iterator<char>{}};
  if (bytes.empty()) return bytes;

  EncodingHint encoding = EncodingHint::Utf8;
  if (bytes.size() >= 2 && static_cast<std::uint8_t>(bytes[0]) == 0xFF &&
      static_cast<std::uint8_t>(bytes[1]) == 0xFE) {
    encoding = EncodingHint::Utf16Le;
  } else if (bytes.size() >= 2 && static_cast<std::uint8_t>(bytes[0]) == 0xFE &&
             static_cast<std::uint8_t>(bytes[1]) == 0xFF) {
    encoding = EncodingHint::Utf16Be;
  } else if (auto guessed = guess_utf16_from_nuls(bytes); guessed.has_value()) {
    encoding = *guessed;
  }

  if (encoding == EncodingHint::Utf16Le) return decode_utf16(bytes, true);
  if (encoding == EncodingHint::Utf16Be) return decode_utf16(bytes, false);

  // Strip UTF-8 BOM if present.
  if (bytes.size() >= 3 && static_cast<std::uint8_t>(bytes[0]) == 0xEF &&
      static_cast<std::uint8_t>(bytes[1]) == 0xBB &&
      static_cast<std::uint8_t>(bytes[2]) == 0xBF) {
    bytes.erase(0, 3);
  }
  return bytes;
}
