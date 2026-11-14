// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
// GNU coreutils human_readable() for the -h / --si size faces.
//
// [GNU] lib/human.c computes the value in *tenths* of the selected unit and
// rounds each step UP (human_ceiling, which ls.c:4437 and du.c pass for
// -h/--si).  That is why 1537 bytes is "1.6K" and 1048575 bytes is "1.0M":
// 1023.999K rounds to 1024.0K and then promotes to the next unit instead of
// printing "1024K".  Beyond the base, the suffix set follows
// human.c's exponent tables: "KMGTPE" for 1024-based units and "kMGTPE" for
// --si (1000-based, lowercase kilo only).
//
module;

#include <cstdint>
#include <cstdio>

export module utils:human_size;

import std;

export namespace winux::human {

enum class UnitBase {
  binary,   // 1024-based, suffixes K M G T P E  (-h, --human-readable)
  decimal,  // 1000-based, suffixes k M G T P E  (--si)
};

// Formats |size| bytes the way GNU's human_readable(size, buf, human_ceiling,
// 1, output_block_size) does for ls/du: at most one decimal digit below 10
// units, no decimal digit at or above 10, always rounding up.
inline auto size(uint64_t bytes, UnitBase base) -> std::string {
  const bool decimal = base == UnitBase::decimal;
  const char *units = decimal ? "BkMGTPE" : "BKMGTP";
  const uint64_t radix = decimal ? 1000 : 1024;

  if (bytes < radix) {
    return std::to_string(bytes);
  }

  // Smallest unit index (1 = K/k) with bytes in [radix^idx, radix^(idx+1)).
  int idx = 0;
  uint64_t unit = radix;
  while (idx < 6 && bytes / unit >= radix) {
    unit *= radix;
    ++idx;
  }
  ++idx;

  // Value in tenths of the selected unit, rounded up.  The remainder form
  // avoids overflow at exabyte scale.
  const uint64_t whole = bytes / unit;
  const uint64_t rest = bytes % unit;
  uint64_t tenths = whole * 10 + (rest * 10 + unit - 1) / unit;

  // Rollover: 1024.0K is printed as 1.0M, never "1024K".
  while (tenths >= radix * 10 && idx < 6) {
    tenths = (tenths + radix - 1) / radix;
    ++idx;
  }

  char buffer[32];
  if (tenths < 100) {  // below 10 units: one decimal digit
    std::snprintf(buffer, sizeof(buffer), "%llu.%llu%c",
                  static_cast<unsigned long long>(tenths / 10),
                  static_cast<unsigned long long>(tenths % 10), units[idx]);
  } else {
    std::snprintf(buffer, sizeof(buffer), "%llu%c",
                  static_cast<unsigned long long>((tenths + 9) / 10),
                  units[idx]);
  }
  return std::string(buffer);
}

}  // namespace winux::human
