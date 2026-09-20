// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module utils:textio;

import std;
import :utf8;

namespace {
enum class EncodingHint { Utf8, Utf16Le, Utf16Be };

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
