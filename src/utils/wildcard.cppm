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
 *  - File: wildcard.cppm
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
export module utils:wildcard;

import std;
import :utf8;
import :path;

#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE HANDLE(-1)
#endif

namespace wildcard_impl {

/**
 * @brief Check if a character matches a character class pattern
 * @param char_class The character class string (e.g., "[abc]", "[a-z]",
 * "[^0-9]")
 * @param c The character to check
 * @return true if the character matches the class, false otherwise
 */
static bool match_char_class(std::wstring_view char_class, wchar_t c) {
  if (static_cast<size_t>(char_class.size()) < 2) return false;

  // Check for negation [^...]
  bool negate = (char_class[1] == L'^');
  size_t start = negate ? 2 : 1;
  size_t end =
      static_cast<size_t>(char_class.size()) - 1;  // Exclude closing ']'

  if (start >= end) return false;

  // Check for range [a-z]
  for (size_t i = start; i < end; ++i) {
    if (i + 2 < end && char_class[i + 1] == L'-') {
      // Range pattern: a-z
      wchar_t range_start = char_class[i];
      wchar_t range_end = char_class[i + 2];
      if (c >= range_start && c <= range_end) {
        return !negate;
      }
      i += 2;  // Skip the '-' and next character
    } else {
      // Single character: a
      if (char_class[i] == c) {
        return !negate;
      }
    }
  }

  return negate;  // If negate is true, return true (no match)
}

/**
 * @brief Core wildcard matching implementation with support for *, ?, and []
 * @param pattern The wildcard pattern
 * @param text The text to match against
 * @return true if text matches pattern, false otherwise
 */
static bool wildcard_match_impl(std::wstring_view pattern,
                                std::wstring_view text) {
  size_t pi = 0, ti = 0;
  while (pi < static_cast<size_t>(pattern.size())) {
    if (pattern[pi] == L'*') {
      while (pi < static_cast<size_t>(pattern.size()) && pattern[pi] == L'*')
        ++pi;
      if (pi == static_cast<size_t>(pattern.size())) return true;
      while (ti <= static_cast<size_t>(text.size())) {
        if (wildcard_match_impl(pattern.substr(pi), text.substr(ti)))
          return true;
        if (ti == static_cast<size_t>(text.size())) break;
        ++ti;
      }
      return false;
    }

    if (ti >= static_cast<size_t>(text.size())) return false;

    if (pattern[pi] == L'?') {
      ++pi;
      ++ti;
    } else if (pattern[pi] == L'[') {
      // Find matching ']'
      size_t bracket_end = pi + 1;
      while (bracket_end < static_cast<size_t>(pattern.size()) &&
             pattern[bracket_end] != L']') {
        bracket_end++;
      }

      if (bracket_end >= static_cast<size_t>(pattern.size())) {
        // No closing bracket, treat '[' as literal
        if (pattern[pi] != text[ti]) return false;
        ++pi;
        ++ti;
      } else {
        // Extract character class: [abc] or [a-z] or [^0-9]
        std::wstring_view char_class = pattern.substr(pi, bracket_end - pi + 1);
        if (!match_char_class(char_class, text[ti])) {
          return false;
        }
        pi = bracket_end + 1;
        ++ti;
      }
    } else if (pattern[pi] == text[ti]) {
      ++pi;
      ++ti;
    } else {
      return false;
    }
  }

  return ti == static_cast<size_t>(text.size());
}

}  // namespace wildcard_impl

/**
 * @brief Enhanced wildcard matching with support for *, ?, and []
 * @param pattern The wildcard pattern
 * @param text The text to match against
 * @param case_sensitive Whether to perform case-sensitive matching (default:
 * false)
 * @return true if text matches pattern, false otherwise
 */
export bool wildcard_match(const std::wstring &pattern,
                           const std::wstring &text,
                           bool case_sensitive = false) {
  if (case_sensitive) {
    return wildcard_impl::wildcard_match_impl(pattern, text);
  }

  // Case-insensitive matching
  auto to_lower = [](std::wstring_view s) {
    std::wstring result;
    result.reserve(static_cast<size_t>(s.size()));
    for (wchar_t c : s) {
      result.push_back(std::towlower(c));
    }
    return result;
  };

  auto p = to_lower(pattern);
  auto t = to_lower(text);
  return wildcard_impl::wildcard_match_impl(p, t);
}

/**
 * @brief Check if a string contains wildcard characters (*, ?, [)
 * @param str The string to check
 * @return true if the string contains wildcard characters, false otherwise
 *
 * Note: If the string starts with \x01, it was quoted in the shell
 * and should not be treated as a wildcard pattern.
 */
export bool contains_wildcard(std::wstring_view str) {
  // Check for quote protection marker
  if (!str.empty() && str[0] == L'\x01') {
    return false;
  }
  return str.find(L'*') != std::wstring_view::npos ||
         str.find(L'?') != std::wstring_view::npos ||
         str.find(L'[') != std::wstring_view::npos;
}

/**
 * @brief Result of glob expansion
 */
export struct GlobResult {
  std::vector<std::wstring> files;  // Matched file list
  bool expanded;                    // Whether expansion was performed
};

/**
 * @brief Smart glob expansion that handles all environments
 * @param pattern The wildcard pattern or literal file path
 * @return GlobResult containing matched files and expansion status
 *
 * This function implements smart wildcard expansion:
 * 1. First tries the pattern as a literal file path
 * 2. If that fails, tries wildcard expansion using FindFirstFileW
 * 3. If expansion fails, returns the original pattern as a literal value
 *
 * This approach works correctly in all environments:
 * - PowerShell: expanded parameters don't contain wildcards, used as literal
 * paths
 * - cmd.exe/REPL: parameters with wildcards are expanded
 * - Supports literal filenames like "*.txt" (a file actually named "*.txt")
 */
export GlobResult glob_expand(std::wstring_view pattern) {
  GlobResult result;

  // 1. First try as literal file path
  std::error_code ec;
  if (std::filesystem::exists(pattern, ec)) {
    result.files.push_back(std::wstring(pattern));
    result.expanded = false;  // Not expanded, literal path
    return result;
  }

  // Check if pattern contains [...] character class (not supported by
  // FindFirstFileW)
  bool has_char_class = (pattern.find(L'[') != std::wstring_view::npos);

  if (has_char_class) {
    // Use custom wildcard matching for [...] patterns
    // Extract directory part from pattern
    std::wstring pattern_str(pattern);
    size_t last_sep = pattern_str.find_last_of(L"\\/");
    std::wstring dir;
    std::wstring file_pattern;
    if (last_sep != std::wstring::npos) {
      dir = pattern_str.substr(0, last_sep + 1);
      file_pattern = pattern_str.substr(last_sep + 1);
    } else {
      dir = L".";
      file_pattern = pattern_str;
    }

    // Enumerate directory and match with wildcard_match
    std::error_code iter_ec;
    for (const auto &entry :
         std::filesystem::directory_iterator(dir, iter_ec)) {
      std::wstring filename = entry.path().filename().wstring();
      if (wildcard_match(file_pattern, filename)) {
        if (!dir.empty() && dir != L".") {
          result.files.push_back(dir + filename);
        } else {
          result.files.push_back(filename);
        }
      }
    }
    result.expanded = !result.files.empty();

    // If no files matched, return original pattern as literal
    if (result.files.empty()) {
      result.files.push_back(std::wstring(pattern));
      result.expanded = false;
    }
  } else {
    // Use FindFirstFileW for * and ? patterns (faster)
    WIN32_FIND_DATAW find_data;
    HANDLE hFind = FindFirstFileW(pattern.data(), &find_data);

    if (hFind != INVALID_HANDLE_VALUE) {
      // Extract directory part from pattern
      std::wstring pattern_str(pattern);
      size_t last_sep = pattern_str.find_last_of(L"\\/");
      std::wstring dir;
      if (last_sep != std::wstring::npos) {
        dir = pattern_str.substr(0, last_sep + 1);
      }

      do {
        std::wstring filename = find_data.cFileName;
        // Skip . and .. entries
        if (filename != L"." && filename != L"..") {
          if (!dir.empty()) {
            result.files.push_back(dir + filename);
          } else {
            result.files.push_back(filename);
          }
        }
      } while (FindNextFileW(hFind, &find_data) != 0);
      FindClose(hFind);
      result.expanded = true;

      // If no files matched, return original pattern as literal
      if (result.files.empty()) {
        result.files.push_back(std::wstring(pattern));
        result.expanded = false;
      }
    } else {
      // Expansion failed, return original literal value
      result.files.push_back(std::wstring(pattern));
      result.expanded = false;
    }
  }

  return result;
}

/**
 * @brief Smart glob expansion for narrow strings (UTF-8)
 * @param pattern The wildcard pattern or literal file path (UTF-8)
 * @return GlobResult containing matched files and expansion status
 */
export GlobResult glob_expand(std::string_view pattern) {
  std::wstring wpattern = utf8_to_wstring(pattern);
  return glob_expand(wpattern);
}

/**
 * @brief Enhanced wildcard matching for narrow strings (UTF-8)
 * @param pattern The wildcard pattern
 * @param text The text to match against
 * @param case_sensitive Whether to perform case-sensitive matching (default:
 * false)
 * @return true if text matches pattern, false otherwise
 */
export bool wildcard_match(const std::string &pattern, const std::string &text,
                           bool case_sensitive = false) {
  std::wstring wpattern = utf8_to_wstring(pattern);
  std::wstring wtext = utf8_to_wstring(text);
  return wildcard_match(wpattern, wtext, case_sensitive);
}

/**
 * @brief Check if a narrow string contains wildcard characters (*, ?, [)
 * @param str The string to check
 * @return true if the string contains wildcard characters, false otherwise
 *
 * Note: If the string starts with \x01, it was quoted in the shell
 * and should not be treated as a wildcard pattern.
 */
export bool contains_wildcard(std::string_view str) {
  // Check for quote protection marker
  if (!str.empty() && str[0] == '\x01') {
    return false;
  }
  return str.find('*') != std::string_view::npos ||
         str.find('?') != std::string_view::npos ||
         str.find('[') != std::string_view::npos;
}
