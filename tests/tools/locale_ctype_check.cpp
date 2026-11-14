// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @Description: Runtime check for the LC_CTYPE classification projection
///   above 0x7F (issue #1549 phase 2, item 5). The ASCII half of
///   `winux::locale_db::classify` is pinned by static_asserts in
///   src/utils/locale_db.cppm; the GetStringTypeW path has no command
///   consumer, so this harness is its first caller. It runs in the top-level
///   CMake tree (not tests/) for the same ABI reason as rust2-stream-driver:
///   a consumer of the module BMIs must be compiled with the producing
///   target's flags (no /EHsc).
///
///   Expected values were measured on Windows 10/11 NLS: CT_CTYPE1 sets
///   C1_DEFINED for every character it knows, plus the actual classes. The
///   projection deliberately drops C1_DEFINED (it is not a POSIX class), so
///   characters that only carry C1_DEFINED - currency symbols such as U+20AC,
///   combining marks such as U+0301 - report no POSIX class. That gap is
///   recorded in DOCS/2.0-locale-database-design.md §10.
/// @Version: 0.1.0
/// @License: MIT

#include <cstdio>

#include "pch/pch.h"

import std;
import utils;

namespace {

struct ClassCase {
  char32_t code_point;
  bool alpha;
  bool lower;
  bool upper;
  bool digit;
  bool space;
  bool blank;
  bool punct;
  const char *label;
};

}  // namespace

int main() {
  using namespace winux::locale_db;

  const ClassCase cases[] = {
      {U'\u00E9', true, true, false, false, false, false, false, "e-acute"},
      {U'\u00C9', true, false, true, false, false, false, false, "E-acute"},
      {U'\u4E2D', true, false, false, false, false, false, false,
       "CJK ideograph"},
      {U'\u0661', false, false, false, true, false, false, false,
       "ARABIC-INDIC ONE"},
      {U'\u3000', false, false, false, false, true, true, false,
       "IDEOGRAPHIC SPACE"},
      {U'\u00A5', false, false, false, false, false, false, true, "YEN SIGN"},
      {U'\uFF10', false, false, false, true, false, false, false,
       "FULLWIDTH DIGIT ZERO"},
      {U'\uFF21', true, false, true, false, false, false, false,
       "FULLWIDTH LATIN A"},
      {U'\u0410', true, false, true, false, false, false, false, "CYRILLIC A"},
      {U'\u0430', true, true, false, false, false, false, false, "CYRILLIC a"},
  };

  int failures = 0;
  for (const auto &test : cases) {
    const unsigned short bits = classify(test.code_point);
    const bool alpha = (bits & ctype_alpha) != 0;
    const bool lower = (bits & ctype_lower) != 0;
    const bool upper = (bits & ctype_upper) != 0;
    const bool digit = (bits & ctype_digit) != 0;
    const bool space = (bits & ctype_space) != 0;
    const bool blank = (bits & ctype_blank) != 0;
    const bool punct = (bits & ctype_punct) != 0;
    if (alpha == test.alpha && lower == test.lower && upper == test.upper &&
        digit == test.digit && space == test.space && blank == test.blank &&
        punct == test.punct) {
      continue;
    }
    ++failures;
    std::printf(
        "locale-ctype-check: U+%04X (%s) projection mismatch: "
        "alpha=%d/%d lower=%d/%d upper=%d/%d digit=%d/%d space=%d/%d "
        "blank=%d/%d punct=%d/%d\n",
        static_cast<unsigned>(test.code_point), test.label, alpha, test.alpha,
        lower, test.lower, upper, test.upper, digit, test.digit, space,
        test.space, blank, test.blank, punct, test.punct);
  }

  // Documented limits of the CT_CTYPE1 source, pinned so a future change is
  // a conscious one: an astral character (UTF-16 surrogate pair) has no single
  // unit to classify, and C1_DEFINED-only characters expose no POSIX class.
  const unsigned short astral = classify(char32_t{0x1D400});
  if (astral != 0) {
    ++failures;
    std::puts(
        "locale-ctype-check: astral U+1D400 must report 0 (UTF-16 units)");
  }
  const unsigned short euro = classify(U'\u20AC');
  if (euro != 0) {
    ++failures;
    std::puts(
        "locale-ctype-check: U+20AC carries C1_DEFINED only, so it must not "
        "gain a POSIX class without a documented projection change");
  }

  if (failures == 0) {
    std::puts("locale-ctype-check: OK (12 code points above 0x7F)");
    return 0;
  }
  std::printf("locale-ctype-check: %d failure(s)\n", failures);
  return 1;
}
