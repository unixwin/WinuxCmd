// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
export module utils:string;

import std;

export auto ascii_lower_copy(std::string_view text) -> std::string {
  std::string out;
  out.reserve(text.size());
  for (unsigned char ch : text) {
    out.push_back(static_cast<char>(std::tolower(ch)));
  }
  return out;
}

export auto ascii_lower_copy(std::wstring_view text) -> std::wstring {
  std::wstring out;
  out.reserve(text.size());
  for (wchar_t ch : text) {
    if (ch >= L'A' && ch <= L'Z') {
      out.push_back(static_cast<wchar_t>(ch - L'A' + L'a'));
    } else {
      out.push_back(ch);
    }
  }
  return out;
}

export auto ascii_iequals(std::string_view lhs, std::string_view rhs) -> bool {
  if (lhs.size() != rhs.size()) return false;
  for (size_t i = 0; i < lhs.size(); ++i) {
    auto l = static_cast<unsigned char>(lhs[i]);
    auto r = static_cast<unsigned char>(rhs[i]);
    if (std::tolower(l) != std::tolower(r)) return false;
  }
  return true;
}

export auto ascii_iequals(std::wstring_view lhs, std::wstring_view rhs)
    -> bool {
  if (lhs.size() != rhs.size()) return false;
  for (size_t i = 0; i < lhs.size(); ++i) {
    wchar_t l = lhs[i];
    wchar_t r = rhs[i];
    if (l >= L'A' && l <= L'Z') l = static_cast<wchar_t>(l - L'A' + L'a');
    if (r >= L'A' && r <= L'Z') r = static_cast<wchar_t>(r - L'A' + L'a');
    if (l != r) return false;
  }
  return true;
}

export auto ascii_starts_with_ci(std::string_view text, std::string_view prefix)
    -> bool {
  return text.size() >= prefix.size() &&
         ascii_iequals(text.substr(0, prefix.size()), prefix);
}

export auto ascii_contains_ci(std::string_view text, std::string_view needle)
    -> bool {
  if (needle.empty()) return true;
  if (needle.size() > text.size()) return false;

  const auto lowered_text = ascii_lower_copy(text);
  const auto lowered_needle = ascii_lower_copy(needle);
  return lowered_text.find(lowered_needle) != std::string::npos;
}

export auto ascii_contains_ci(std::wstring_view text, std::wstring_view needle)
    -> bool {
  if (needle.empty()) return true;
  if (needle.size() > text.size()) return false;

  const auto lowered_text = ascii_lower_copy(text);
  const auto lowered_needle = ascii_lower_copy(needle);
  return lowered_text.find(lowered_needle) != std::wstring::npos;
}
