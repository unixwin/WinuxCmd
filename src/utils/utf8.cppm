/// @Author: caomengxuan666
/// @contributors:
///   - contributor1 <email1@example.com>
///   - contributor2 <email2@example.com>
///   - contributor3 <email3@example.com>
///   - description
/// @Description: UTF-8 utilities
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd
module;

#include "pch/pch.h"
export module utils:utf8;

import std;

/**
 * @brief Convert UTF-8 string to wide string
 * @param utf8 UTF-8 string
 * @return Wide string
 */
export std::wstring utf8_to_wstring(const std::string_view& utf8) {
  if (utf8.empty()) return {};
  int size_needed = MultiByteToWideChar(
      CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()), nullptr, 0);
  std::wstring wide(size_needed, 0);
  MultiByteToWideChar(CP_UTF8, 0, utf8.data(), static_cast<int>(utf8.size()),
                      wide.data(), size_needed);
  return wide;
}

/**
 * @brief Convert wide string to UTF-8 string
 * @param wide Wide string
 * @return UTF-8 string
 */
export std::string wstring_to_utf8(const std::wstring_view& wide) {
  if (wide.empty()) return {};
  int size_needed = WideCharToMultiByte(CP_UTF8, 0, wide.data(),
                                        static_cast<int>(wide.size()), nullptr,
                                        0, nullptr, nullptr);
  std::string utf8(size_needed, 0);
  WideCharToMultiByte(CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
                      utf8.data(), size_needed, nullptr, nullptr);
  return utf8;
}

/**
 * @brief Strict UTF-8 validity check (RFC 3629, no surrogates, no
 *        overlongs). Operands that fail this check cannot name a real file
 *        through the wide-char Windows API, so tools can reject them up
 *        front with GNU's "No such file or directory" instead of routing
 *        malformed bytes into path conversion (#339, #350, #353, #362).
 * @param text byte string to validate
 * @return true when every byte sequence is a well-formed UTF-8 encoding
 */
export auto is_valid_utf8(const std::string_view& text) -> bool {
  const auto* p = text.data();
  const auto* const end = text.data() + text.size();

  auto continuation = [&](unsigned char byte) -> bool {
    return (byte & 0xC0) == 0x80;
  };

  while (p < end) {
    const unsigned char lead = static_cast<unsigned char>(*p);
    if (lead < 0x80) {
      ++p;
      continue;
    }
    size_t length = 0;
    unsigned int codepoint = 0;
    if ((lead & 0xE0) == 0xC0) {
      length = 2;
      codepoint = lead & 0x1F;
    } else if ((lead & 0xF0) == 0xE0) {
      length = 3;
      codepoint = lead & 0x0F;
    } else if ((lead & 0xF8) == 0xF0) {
      length = 4;
      codepoint = lead & 0x07;
    } else {
      return false;
    }
    if (static_cast<size_t>(end - p) < length) return false;
    for (size_t i = 1; i < length; ++i) {
      const unsigned char cont = static_cast<unsigned char>(p[i]);
      if (!continuation(cont)) return false;
      codepoint = (codepoint << 6) | (cont & 0x3F);
    }
    if (length == 2 && codepoint < 0x80) return false;      // overlong
    if (length == 3 && codepoint < 0x800) return false;     // overlong
    if (length == 4 && codepoint < 0x10000) return false;   // overlong
    if (codepoint > 0x10FFFF) return false;
    if (codepoint >= 0xD800 && codepoint <= 0xDFFF) return false;  // surrogate
    p += length;
  }
  return true;
}
