// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
// GNU-compatible 80-bit extended-precision (x87 "long double") float
// parsing and formatting.
//
// GNU coreutils printf(1) parses every floating-point argument with
// strtold() and prints it through the %L conversions; od(1) -t fL/f16
// decodes the same 80-bit type.  On x86-64 glibc/MSYS2 that type is the
// 80-bit IEEE-754 extended format (64-bit significand, 15-bit exponent).
// MSVC's "long double" is only a 64-bit double, so to match GNU output
// (e.g. printf '%.2e' 2.455 -> 2.45e+00, printf '%a' pi -> 16 hex
// fraction digits) we emulate the type here.
//
module;

// GMF: global-namespace C names (size_t/uintN_t, isspace/memcpy/snprintf)
// that `import std` does not provide.
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

export module utils:gnu_float80;
import std;

export namespace gnu_float80 {

// Value = (negative ? -1 : +1) * mant * 2^(exp - 63).
// For normal values mant's bit 63 is set; subnormals have exp == -16382
// with mant < 2^63.
struct Ext80 {
  enum class Kind { zero, normal, inf, nan };
  Kind kind = Kind::zero;
  bool negative = false;
  uint64_t mant = 0;
  int exp = 0;
};

namespace detail {

// ---------- arbitrary-precision unsigned integer (base 2^32) ----------

struct Big {
  std::vector<uint32_t> w;  // little-endian limbs; empty == 0

  void trim() {
    while (!w.empty() && w.back() == 0) w.pop_back();
  }
};

inline Big big_from_u64(uint64_t v) {
  Big b;
  uint32_t lo = static_cast<uint32_t>(v);
  uint32_t hi = static_cast<uint32_t>(v >> 32);
  if (hi != 0) {
    b.w.push_back(lo);  // keep a zero low limb: it is positional
    b.w.push_back(hi);
  } else if (lo != 0) {
    b.w.push_back(lo);
  }
  return b;
}

inline int big_cmp(const Big& a, const Big& b) {
  if (a.w.size() != b.w.size()) return a.w.size() < b.w.size() ? -1 : 1;
  for (size_t i = a.w.size(); i-- > 0;) {
    if (a.w[i] != b.w[i]) return a.w[i] < b.w[i] ? -1 : 1;
  }
  return 0;
}

inline void big_shl(Big& a, int bits) {
  if (a.w.empty() || bits <= 0) return;
  int limbs = bits / 32;
  int rem = bits % 32;
  if (rem != 0) {
    uint64_t carry = 0;
    for (auto& x : a.w) {
      uint64_t t = (static_cast<uint64_t>(x) << rem) | carry;
      x = static_cast<uint32_t>(t);
      carry = t >> 32;
    }
    if (carry) a.w.push_back(static_cast<uint32_t>(carry));
  }
  if (limbs > 0) a.w.insert(a.w.begin(), limbs, 0);
}

inline void big_add_u32(Big& a, uint32_t v) {
  uint64_t carry = v;
  for (size_t i = 0; carry != 0 && i < a.w.size(); ++i) {
    uint64_t t = static_cast<uint64_t>(a.w[i]) + carry;
    a.w[i] = static_cast<uint32_t>(t);
    carry = t >> 32;
  }
  if (carry) a.w.push_back(static_cast<uint32_t>(carry));
}

inline void big_mul_u32(Big& a, uint32_t m) {
  if (m == 0 || a.w.empty()) {
    a.w.clear();
    return;
  }
  uint64_t carry = 0;
  for (auto& x : a.w) {
    uint64_t t = static_cast<uint64_t>(x) * m + carry;
    x = static_cast<uint32_t>(t);
    carry = t >> 32;
  }
  while (carry) {
    a.w.push_back(static_cast<uint32_t>(carry));
    carry >>= 32;
  }
}

inline Big big_sub(const Big& a, const Big& b) {  // a - b, requires a >= b
  Big r;
  r.w.resize(a.w.size(), 0);
  uint64_t borrow = 0;
  for (size_t i = 0; i < a.w.size(); ++i) {
    uint64_t bi = i < b.w.size() ? b.w[i] : 0;
    uint64_t cur = static_cast<uint64_t>(a.w[i]) - bi - borrow;
    r.w[i] = static_cast<uint32_t>(cur);
    borrow = (cur >> 63) & 1;
  }
  r.trim();
  return r;
}

inline int big_bit_length(const Big& a) {
  if (a.w.empty()) return 0;
  return static_cast<int>(a.w.size() - 1) * 32 +
         (32 - std::countl_zero(a.w.back()));
}

inline bool big_test_bit(const Big& a, int i) {
  int limb = i / 32;
  if (limb >= static_cast<int>(a.w.size())) return false;
  return (a.w[limb] >> (i % 32)) & 1u;
}

inline bool big_any_bits_below(const Big& a, int n) {  // any bit in [0, n)
  int limbs = n / 32;
  for (int i = 0; i < limbs && i < static_cast<int>(a.w.size()); ++i) {
    if (a.w[i] != 0) return true;
  }
  int rem = n % 32;
  if (rem && limbs < static_cast<int>(a.w.size()) &&
      (a.w[limbs] & ((1u << rem) - 1)) != 0) {
    return true;
  }
  return false;
}

// q = a / b, r = a % b  (simple binary long division; inputs here are
// at most ~20k bits so this is fast enough for a CLI tool)
inline void big_divmod(const Big& a, const Big& b, Big& q, Big& r) {
  q.w.clear();
  r.w.clear();
  if (b.w.empty() || big_cmp(a, b) < 0) {
    r = a;
    return;
  }
  int n = big_bit_length(a);
  q.w.assign((n + 31) / 32, 0);
  for (int i = n - 1; i >= 0; --i) {
    big_shl(r, 1);
    if (big_test_bit(a, i)) {
      if (r.w.empty()) r.w.push_back(0);
      r.w[0] |= 1;
    }
    if (big_cmp(r, b) >= 0) {
      r = big_sub(r, b);
      q.w[i / 32] |= 1u << (i % 32);
    }
  }
  q.trim();
  r.trim();
}

inline uint32_t big_divmod_u32(Big& a, uint32_t m) {  // a /= m; returns a % m
  uint64_t rem = 0;
  for (size_t i = a.w.size(); i-- > 0;) {
    uint64_t cur = (rem << 32) | a.w[i];
    a.w[i] = static_cast<uint32_t>(cur / m);
    rem = cur % m;
  }
  a.trim();
  return static_cast<uint32_t>(rem);
}

inline std::string big_to_decimal(Big a) {
  if (a.w.empty()) return "0";
  std::string groups;
  while (!a.w.empty()) {
    uint32_t rem = big_divmod_u32(a, 1000000000u);
    char buf[16];
    if (a.w.empty()) {
      std::snprintf(buf, sizeof(buf), "%u", rem);
    } else {
      std::snprintf(buf, sizeof(buf), "%09u", rem);
    }
    groups.insert(0, buf);
  }
  return groups;
}

inline Big big_pow5(int n) {
  Big r = big_from_u64(1);
  for (int i = 0; i < n; ++i) big_mul_u32(r, 5);
  return r;
}

// Round |v| = N * 2^shift to a 64-bit significand (round to nearest,
// ties to even).  sticky_extra marks additional nonzero magnitude below
// N's bit 0 (e.g. a nonzero division remainder).
inline Ext80 normalize(const Big& N, int shift, bool sticky_extra,
                       bool negative, bool& range_err) {
  Ext80 v;
  v.negative = negative;
  int L = big_bit_length(N);
  if (L == 0) {
    v.kind = Ext80::Kind::zero;
    v.exp = -16382;
    return v;
  }

  uint64_t mant;
  int exp2;
  bool sticky = sticky_extra;
  if (L > 64) {
    int drop = L - 64;
    mant = 0;
    for (int i = 0; i < 64; ++i) {
      if (big_test_bit(N, drop + i)) mant |= 1ull << i;
    }
    bool round_bit = big_test_bit(N, drop - 1);
    sticky |= big_any_bits_below(N, drop - 1);
    if (round_bit && (sticky || (mant & 1) != 0)) {
      ++mant;
      if (mant == 0) {
        mant = 1ull << 63;
        ++L;
      }
    }
    exp2 = L - 1 + shift;
  } else {
    mant = 0;
    for (int i = 0; i < L; ++i) {
      if (big_test_bit(N, i)) mant |= 1ull << (i + 64 - L);
    }
    exp2 = L - 1 + shift;
  }

  if (exp2 > 16383) {
    v.kind = Ext80::Kind::inf;
    range_err = true;
    return v;
  }
  if (exp2 < -16382) {
    // Subnormal range: mantissa slides right of the 64-bit window.
    int sh = -16382 - exp2;
    range_err = true;
    uint64_t m = mant;
    bool round_bit = sh <= 64 && ((m >> (sh - 1)) & 1) != 0;
    bool st = sticky;
    if (sh > 1) {
      uint64_t lowmask = sh - 1 >= 64 ? ~0ull : ((1ull << (sh - 1)) - 1);
      st |= (m & lowmask) != 0;
    }
    m = sh >= 64 ? 0 : (m >> sh);
    if (round_bit && (st || (m & 1) != 0)) ++m;
    if (m == 0) {
      v.kind = Ext80::Kind::zero;
      v.exp = -16382;
      return v;
    }
    v.kind = Ext80::Kind::normal;
    v.mant = m;
    v.exp = -16382;
    return v;
  }
  v.kind = Ext80::Kind::normal;
  v.mant = mant;
  v.exp = exp2;
  return v;
}

inline bool ascii_ieq(std::string_view s, size_t pos, std::string_view word) {
  if (pos + word.size() > s.size()) return false;
  for (size_t i = 0; i < word.size(); ++i) {
    char a = s[pos + i];
    char b = word[i];
    if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + 32);
    if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + 32);
    if (a != b) return false;
  }
  return true;
}

}  // namespace detail

// strtold work-alike: parses the longest valid float prefix of `text`.
// On return, `end` points just past the parsed characters (== text.data()
// when nothing parses, like strtold).  `range_err` reports ERANGE
// (overflow to inf, or subnormal/zeroed underflow).
inline Ext80 parse(std::string_view text, const char*& end, bool& range_err) {
  using detail::ascii_ieq;
  using detail::Big;
  using detail::big_add_u32;
  using detail::big_bit_length;
  using detail::big_cmp;
  using detail::big_divmod;
  using detail::big_from_u64;
  using detail::big_mul_u32;
  using detail::big_pow5;
  using detail::big_shl;
  using detail::normalize;

  Ext80 v;
  range_err = false;
  const char* base = text.data();
  size_t n = text.size();
  size_t i = 0;
  while (i < n && std::isspace(static_cast<unsigned char>(text[i])) != 0) {
    ++i;
  }
  bool negative = false;
  if (i < n && (text[i] == '+' || text[i] == '-')) {
    negative = text[i] == '-';
    ++i;
  }

  auto finish = [&](size_t pos) {
    v.negative = negative;
    end = base + pos;
    return v;
  };

  if (ascii_ieq(text, i, "infinity")) {
    v.kind = Ext80::Kind::inf;
    return finish(i + 8);
  }
  if (ascii_ieq(text, i, "inf")) {
    v.kind = Ext80::Kind::inf;
    return finish(i + 3);
  }
  if (ascii_ieq(text, i, "nan")) {
    size_t j = i + 3;
    v.kind = Ext80::Kind::nan;
    if (j < n && text[j] == '(') {
      size_t k = j + 1;
      while (k < n && (std::isalnum(static_cast<unsigned char>(text[k])) != 0 ||
                       text[k] == '_')) {
        ++k;
      }
      if (k < n && text[k] == ')') j = k + 1;
    }
    return finish(j);
  }

  // Hexadecimal float: 0x hhh [. hhh] [p [+-] ddd]
  if (i + 1 < n && text[i] == '0' &&
      (text[i + 1] == 'x' || text[i + 1] == 'X') && i + 2 < n &&
      (std::isxdigit(static_cast<unsigned char>(text[i + 2])) != 0 ||
       (text[i + 2] == '.' && i + 3 < n &&
        std::isxdigit(static_cast<unsigned char>(text[i + 3])) != 0))) {
    size_t j = i + 2;
    Big mant;
    int frac_hex = 0;
    bool seen_digit = false;
    while (j < n && std::isxdigit(static_cast<unsigned char>(text[j])) != 0) {
      char c = text[j++];
      int d = c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
      big_mul_u32(mant, 16);
      big_add_u32(mant, static_cast<uint32_t>(d));
      seen_digit = true;
    }
    if (j < n && text[j] == '.') {
      ++j;
      while (j < n && std::isxdigit(static_cast<unsigned char>(text[j])) != 0) {
        char c = text[j++];
        int d = c <= '9' ? c - '0' : (c | 32) - 'a' + 10;
        big_mul_u32(mant, 16);
        big_add_u32(mant, static_cast<uint32_t>(d));
        ++frac_hex;
      }
    }
    int bexp = 0;
    if (j < n && (text[j] == 'p' || text[j] == 'P')) {
      size_t k = j + 1;
      bool eneg = false;
      if (k < n && (text[k] == '+' || text[k] == '-')) {
        eneg = text[k] == '-';
        ++k;
      }
      long long e = 0;
      bool any = false;
      while (k < n && std::isdigit(static_cast<unsigned char>(text[k])) != 0) {
        if (e < 1000000) e = e * 10 + (text[k] - '0');
        any = true;
        ++k;
      }
      if (any) {
        bexp = static_cast<int>(eneg ? -e : e);
        j = k;
      }
    }
    if (!seen_digit && frac_hex == 0) {
      end = base + i + 1;  // "0x" alone parses as plain "0"
      return v;
    }
    v = normalize(mant, bexp - 4 * frac_hex, false, negative, range_err);
    end = base + j;
    return v;
  }

  // Decimal float: ddd [. ddd] [eE [+-] ddd]
  size_t int_start = i;
  while (i < n && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
    ++i;
  }
  size_t int_end = i;
  size_t frac_start = i, frac_end = i;
  if (i < n && text[i] == '.') {
    frac_start = ++i;
    while (i < n && std::isdigit(static_cast<unsigned char>(text[i])) != 0) {
      ++i;
    }
    frac_end = i;
  }
  if (int_end == int_start && frac_end == frac_start) {
    end = base;  // nothing parsed
    return v;
  }

  long long exp10 = 0;
  if (i < n && (text[i] == 'e' || text[i] == 'E')) {
    size_t k = i + 1;
    bool eneg = false;
    if (k < n && (text[k] == '+' || text[k] == '-')) {
      eneg = text[k] == '-';
      ++k;
    }
    long long e = 0;
    bool any = false;
    while (k < n && std::isdigit(static_cast<unsigned char>(text[k])) != 0) {
      if (e < 100000000) e = e * 10 + (text[k] - '0');
      any = true;
      ++k;
    }
    if (any) {
      exp10 = eneg ? -e : e;
      i = k;
    }
  }
  end = base + i;

  // Significant digit string; value = D * 10^k.
  std::string digits;
  digits.reserve((int_end - int_start) + (frac_end - frac_start));
  for (size_t p = int_start; p < int_end; ++p) digits += text[p];
  size_t n_frac = frac_end - frac_start;
  for (size_t p = frac_start; p < frac_end; ++p) digits += text[p];
  size_t first_sig = digits.find_first_not_of('0');
  if (first_sig == std::string::npos) {
    v.kind = Ext80::Kind::zero;
    v.negative = negative;
    return v;
  }
  digits.erase(0, first_sig);

  long long k = exp10 - static_cast<long long>(n_frac);

  // Keep enough leading significant digits for a correctly-rounded
  // 64-bit significand; the dropped tail only contributes stickiness.
  bool sticky_extra = false;
  constexpr size_t kKeepDigits = 40;
  if (digits.size() > kKeepDigits) {
    for (size_t p = kKeepDigits; p < digits.size(); ++p) {
      if (digits[p] != '0') sticky_extra = true;
    }
    k += static_cast<long long>(digits.size() - kKeepDigits);
    digits.resize(kKeepDigits);
  }

  // Quick magnitude bound: the leading digit sits at 10^(k + len - 1).
  long long lead_exp10 = k + static_cast<long long>(digits.size());
  if (lead_exp10 > 5000) {
    v.kind = Ext80::Kind::inf;
    v.negative = negative;
    range_err = true;
    return v;
  }
  if (lead_exp10 < -5100) {
    v.kind = Ext80::Kind::zero;
    v.negative = negative;
    range_err = true;
    return v;
  }

  Big D;
  for (char c : digits) {
    big_mul_u32(D, 10);
    big_add_u32(D, static_cast<uint32_t>(c - '0'));
  }

  if (k >= 0) {
    // v = D * 5^k * 2^k
    for (long long m = 0; m < k; ++m) big_mul_u32(D, 5);
    big_shl(D, static_cast<int>(k));
    v = normalize(D, 0, sticky_extra, negative, range_err);
  } else {
    int j = static_cast<int>(-k);
    // v = D / (5^j * 2^j) = (D * 2^s / 5^j) * 2^-(s+j); pick s so the
    // quotient carries ~70 significant bits.  5^j contributes ~2.322*j
    // bits to the divisor.
    int s = static_cast<int>((j * 2322 + 999) / 1000) + 70 - big_bit_length(D);
    if (s < 0) s = 0;
    Big num = D;
    big_shl(num, s);
    Big den = big_pow5(j);
    Big q, r;
    big_divmod(num, den, q, r);
    v = normalize(q, -(s + j), sticky_extra || !r.w.empty(), negative,
                  range_err);
  }
  return v;
}

// Decode IEEE-754 binary32/binary64/binary80 (x87 80-bit packed into
// `size` bytes, as used by od -t f4/f8/f16).  Missing tail bytes are
// zero-filled, like GNU's zero-padded final block.
inline Ext80 from_bytes(const unsigned char* data, size_t size,
                        bool big_endian) {
  Ext80 v;
  unsigned char le[16] = {};
  if (big_endian) {
    // first byte is most significant; missing tail bytes are least
    // significant -> fill them with zero
    for (size_t i = 0; i < size; ++i) le[i] = data[i];
    std::reverse(le, le + size);
  } else {
    for (size_t i = 0; i < size; ++i) le[i] = data[i];
  }

  if (size == 4) {
    uint32_t bits;
    std::memcpy(&bits, le, 4);
    uint32_t sign = bits >> 31;
    int e = (bits >> 23) & 0xFF;
    uint32_t frac = bits & 0x7FFFFF;
    v.negative = sign != 0;
    if (e == 0xFF) {
      v.kind = frac == 0 ? Ext80::Kind::inf : Ext80::Kind::nan;
      return v;
    }
    if (e == 0 && frac == 0) {
      v.kind = Ext80::Kind::zero;
      return v;
    }
    uint64_t m;
    int exp2;
    if (e == 0) {
      // denormal: value = frac * 2^-149; normalize the significand
      int l = 32 - std::countl_zero(frac);
      m = static_cast<uint64_t>(frac) << (64 - l);
      exp2 = l - 150;
    } else {
      m = (static_cast<uint64_t>(frac) | (1ull << 23)) << (64 - 24);
      exp2 = e - 127;
    }
    v.kind = Ext80::Kind::normal;
    v.mant = m;
    v.exp = exp2;
    return v;
  }
  if (size == 8) {
    uint64_t bits;
    std::memcpy(&bits, le, 8);
    uint64_t sign = bits >> 63;
    int e = static_cast<int>((bits >> 52) & 0x7FF);
    uint64_t frac = bits & ((1ull << 52) - 1);
    v.negative = sign != 0;
    if (e == 0x7FF) {
      v.kind = frac == 0 ? Ext80::Kind::inf : Ext80::Kind::nan;
      return v;
    }
    if (e == 0 && frac == 0) {
      v.kind = Ext80::Kind::zero;
      return v;
    }
    if (e == 0) {
      // denormal: value = frac * 2^-1074; normalize the significand
      int l = 64 - std::countl_zero(frac);
      v.kind = Ext80::Kind::normal;
      v.mant = frac << (64 - l);
      v.exp = l - 1075;
    } else {
      v.kind = Ext80::Kind::normal;
      v.mant = (frac | (1ull << 52)) << (64 - 53);
      v.exp = e - 1023;
    }
    return v;
  }
  // size == 16: x87 80-bit extended in the low 10 bytes (little-endian):
  //   [0..7] 64-bit significand (explicit integer bit)
  //   [8..9] sign:15 | exponent:15
  //   [10..15] unused
  uint64_t mant;
  std::memcpy(&mant, le, 8);
  uint16_t se;
  std::memcpy(&se, le + 8, 2);
  v.negative = (se & 0x8000) != 0;
  int e = se & 0x7FFF;
  if (e == 0x7FFF) {
    v.kind = ((mant << 1) == 0) ? Ext80::Kind::inf : Ext80::Kind::nan;
    return v;
  }
  if (e == 0) {
    if (mant == 0) {
      v.kind = Ext80::Kind::zero;
      return v;
    }
    // denormal: value = mant * 2^-16445; normalize the significand
    int l = 64 - std::countl_zero(mant);
    v.kind = Ext80::Kind::normal;
    v.mant = mant << (64 - l);
    v.exp = l - 16446;
    return v;
  }
  v.kind = Ext80::Kind::normal;
  v.mant = mant;
  v.exp = e - 16383;
  return v;
}

inline Ext80 from_i64(long long val) {
  Ext80 v;
  v.kind = Ext80::Kind::normal;
  if (val == 0) {
    v.kind = Ext80::Kind::zero;
    return v;
  }
  bool neg = val < 0;
  uint64_t m =
      neg ? static_cast<uint64_t>(-(val + 1)) + 1 : static_cast<uint64_t>(val);
  int l = 64 - std::countl_zero(m);
  v.negative = neg;
  v.mant = m << (64 - l);
  v.exp = l - 1;
  return v;
}

// ---------- formatting ----------

struct FormatOpts {
  char conv = 'f';     // 'e' 'f' 'g' 'a' (any case)
  int precision = -1;  // -1 = default
  int width = 0;
  bool left = false;
  bool plus = false;
  bool space = false;
  bool alt = false;  // '#'
  bool zero_pad = false;
};

namespace detail {

// Exact decimal expansion of |v|: digits has no leading or trailing
// zeros, and |v| = 0.digits * 10^point_exp.
inline void exact_digits(uint64_t mant, int exp2, std::string& digits,
                         int& point_exp) {
  int E = exp2 - 63;
  Big J = big_from_u64(mant);
  if (E >= 0) {
    big_shl(J, E);
    digits = big_to_decimal(J);
    point_exp = static_cast<int>(digits.size());
  } else {
    // J = mant * 5^-E
    for (int i = 0; i < -E; ++i) big_mul_u32(J, 5);
    digits = big_to_decimal(J);
    // strip trailing zeros (exact value; they only hide stickiness that
    // does not exist for a finite expansion)
    size_t last = digits.find_last_not_of('0');
    size_t kept = last == std::string::npos ? 0 : last + 1;
    size_t dropped = digits.size() - kept;
    digits.resize(kept);
    point_exp = static_cast<int>(kept + dropped) + E;  // = len + E
  }
}

// Round digit string S (exact expansion) to n >= 1 significant digits,
// round-half-even.  Returns the n-digit string; sets carry when the
// digits rolled over (999.. -> 1000..; caller bumps the exponent).
inline std::string round_sig(const std::string& S, size_t n, bool& carry) {
  carry = false;
  std::string r = S.substr(0, n);
  if (r.size() < n) r.append(n - r.size(), '0');
  if (S.size() <= n) return r;
  char d = S[n];
  bool tail_nonzero = false;
  for (size_t i = n + 1; i < S.size(); ++i) {
    if (S[i] != '0') tail_nonzero = true;
  }
  bool up = d > '5' || (d == '5' && tail_nonzero) ||
            (d == '5' && !tail_nonzero && ((r.back() - '0') & 1) != 0);
  if (!up) return r;
  for (size_t i = r.size(); i-- > 0;) {
    if (r[i] != '9') {
      ++r[i];
      return r;
    }
    r[i] = '0';
  }
  r.insert(r.begin(), '1');
  carry = true;
  return r;
}

// Rounding to zero significant digits (n == 0 case for %f with tiny
// values): returns "1" with carry if |v| rounds up to a 1 at position
// point_exp, else "0".
inline std::string round_zero(const std::string& S, bool& carry) {
  carry = false;
  if (S[0] > '5' ||
      (S[0] == '5' && S.find_first_not_of('0', 1) != std::string::npos)) {
    carry = true;
    return "1";
  }
  return "0";
}

inline std::string e_form(const std::string& digits, int point_exp,
                          int precision, bool alt, bool upper) {
  // digits: at least 1 char; value = digits[0].digits[1..] e (point_exp-1)
  std::string out;
  out.push_back(digits[0]);
  if (precision > 0 || alt) {
    out.push_back('.');
    for (int i = 1; i <= precision; ++i) {
      out.push_back(i < static_cast<int>(digits.size()) ? digits[i] : '0');
    }
  }
  out.push_back(upper ? 'E' : 'e');
  int e = point_exp - 1;
  out.push_back(e < 0 ? '-' : '+');
  if (e < 0) e = -e;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02d", e);
  out += buf;
  return out;
}

inline std::string f_form(const std::string& digits, int point_exp,
                          int precision, bool alt) {
  // value = 0.digits * 10^point_exp, digits already rounded to
  // point_exp + precision significant digits.
  std::string out;
  size_t L = digits.size();
  if (point_exp <= 0) {
    out = "0";
    if (precision > 0 || alt) out.push_back('.');
    size_t zeros = static_cast<size_t>(-point_exp);
    size_t digit_room = static_cast<size_t>(precision) > zeros
                            ? static_cast<size_t>(precision) - zeros
                            : 0;
    out.append(zeros, '0');
    size_t used = 0;
    for (size_t i = 0; i < L && used < digit_room; ++i, ++used) {
      out.push_back(digits[i]);
    }
    size_t have = zeros + used;
    if (have < static_cast<size_t>(precision)) {
      out.append(static_cast<size_t>(precision) - have, '0');
    }
    return out;
  }
  size_t ip = static_cast<size_t>(point_exp);
  for (size_t i = 0; i < ip; ++i) {
    out.push_back(i < L ? digits[i] : '0');
  }
  if (precision > 0 || alt) {
    out.push_back('.');
    for (size_t i = 0; i < static_cast<size_t>(precision); ++i) {
      size_t idx = ip + i;
      out.push_back(idx < L ? digits[idx] : '0');
    }
  }
  return out;
}

}  // namespace detail

// Format |v| per conv/precision (before sign/width handling).
inline std::string format(const Ext80& v, const FormatOpts& o) {
  using detail::Big;
  using detail::e_form;
  using detail::exact_digits;
  using detail::f_form;
  using detail::round_sig;
  using detail::round_zero;

  bool upper = std::isupper(static_cast<unsigned char>(o.conv)) != 0;
  char conv =
      static_cast<char>(std::tolower(static_cast<unsigned char>(o.conv)));
  int prec = o.precision;

  // ---- inf / nan ----
  if (v.kind == Ext80::Kind::inf || v.kind == Ext80::Kind::nan) {
    std::string body = v.kind == Ext80::Kind::inf ? (upper ? "INF" : "inf")
                                                  : (upper ? "NAN" : "nan");
    std::string sign = v.negative ? "-" : (o.plus ? "+" : (o.space ? " " : ""));
    std::string out = sign + body;
    // '0' flag does not pad inf/nan
    if (!o.left && static_cast<int>(out.size()) < o.width) {
      out.insert(sign.size(), o.width - out.size(), ' ');
    }
    if (o.left && static_cast<int>(out.size()) < o.width) {
      out.append(o.width - out.size(), ' ');
    }
    return out;
  }

  // ---- %a / %A : normalized hex float (MSYS2/newlib GNU style) ----
  if (conv == 'a') {
    std::string out;
    if (v.kind == Ext80::Kind::zero) {
      out = "0x0";
      if (prec > 0) {
        out.push_back('.');
        out.append(prec, '0');
      } else if (o.alt) {
        out.push_back('.');
      }
      out += upper ? "P+0" : "p+0";
    } else {
      // leading hex digit + 63 fraction bits for normals; for
      // subnormals the leading digit is 0 and the un-normalized
      // 64-bit significand (at exp -16382) is the fraction field.
      bool subnormal = v.exp < -16382;
      uint64_t frac;  // 64 fraction bits (normal: frac63 << 1)
      int lead;
      int pe;
      if (subnormal) {
        int shift = -16382 - v.exp;  // 1..63
        lead = 0;
        frac = v.mant >> shift;
        pe = -16382;
      } else {
        lead = 1;
        frac = (v.mant & ~(1ull << 63)) << 1;
        pe = v.exp;
      }
      if (prec >= 0 && prec < 16) {
        // round the 64 fraction bits to 4*prec bits, half-even
        int keep = 4 * prec;
        int drop = 64 - keep;
        uint64_t kept = drop >= 64 ? 0 : (frac >> drop);
        bool rb = ((frac >> (drop - 1)) & 1) != 0;
        bool st = (frac & ((1ull << (drop - 1)) - 1)) != 0;
        // last kept digit is the lead digit when keep == 0
        bool lsb_odd = keep == 0 ? (lead & 1) != 0 : (kept & 1) != 0;
        if (rb && (st || lsb_odd)) {
          ++kept;
          if (kept >> keep) {  // carried into the integer digit
            lead += 1;
            kept = 0;
          }
        }
        frac = kept << drop;
      }
      char d = static_cast<char>('0' + lead);
      out += upper ? "0X" : "0x";
      out.push_back(d);
      std::string nibbles;
      nibbles.reserve(16);
      for (int i = 0; i < 16; ++i) {
        int nib = static_cast<int>((frac >> (60 - 4 * i)) & 0xF);
        nibbles.push_back(static_cast<char>(
            nib < 10 ? '0' + nib : (upper ? 'A' : 'a') + nib - 10));
      }
      if (prec < 0) {
        while (!nibbles.empty() && nibbles.back() == '0') nibbles.pop_back();
        if (!nibbles.empty() || o.alt) {
          out.push_back('.');
          out += nibbles;
        }
      } else {
        if (prec > 0 || o.alt) out.push_back('.');
        for (int i = 0; i < prec; ++i) {
          out.push_back(i < 16 ? nibbles[i] : '0');
        }
      }
      out.push_back(upper ? 'P' : 'p');
      out.push_back(pe < 0 ? '-' : '+');
      out += std::to_string(pe < 0 ? -pe : pe);
    }
    std::string sign = v.negative ? "-" : (o.plus ? "+" : (o.space ? " " : ""));
    std::string full = sign + out;
    if (static_cast<int>(full.size()) >= o.width) return full;
    if (o.left) return full + std::string(o.width - full.size(), ' ');
    if (o.zero_pad) {
      // zeros go after "0x"
      size_t pos = sign.size() + 2;
      full.insert(pos, o.width - full.size(), '0');
      return full;
    }
    return std::string(o.width - full.size(), ' ') + full;
  }

  // ---- decimal conversions: e / f / g ----
  if (prec < 0) prec = 6;
  if (conv == 'g' && prec == 0) prec = 1;

  std::string digits;
  int point_exp = 0;
  if (v.kind == Ext80::Kind::zero) {
    digits = "0";
    point_exp = 1;  // 0.0 * 10^1? we treat zero specially below
  } else {
    exact_digits(v.mant, v.exp, digits, point_exp);
  }

  std::string body;
  if (v.kind == Ext80::Kind::zero) {
    if (conv == 'e') {
      body = e_form("0", 1, prec, o.alt, upper);
    } else if (conv == 'f') {
      body = "0";
      if (prec > 0 || o.alt) {
        body.push_back('.');
        body.append(prec, '0');
      }
    } else {  // g
      body = "0";
      if (o.alt) {
        body.push_back('.');
        body.append(prec - 1, '0');
      }
    }
  } else if (conv == 'e') {
    size_t n = static_cast<size_t>(prec) + 1;
    bool carry;
    std::string r = round_sig(digits, n, carry);
    int P = point_exp + (carry ? 1 : 0);
    if (carry) r = "1" + std::string(n - 1, '0');
    body = e_form(r, P, prec, o.alt, upper);
  } else if (conv == 'f') {
    long long n = static_cast<long long>(point_exp) + prec;
    if (n <= 0) {
      bool carry = false;
      std::string r;
      if (n == 0) r = round_zero(digits, carry);
      int P = point_exp;
      if (carry) {
        P += 1;
        body = f_form("1", P, prec, o.alt);
      } else {
        body = "0";
        if (prec > 0 || o.alt) {
          body.push_back('.');
          body.append(prec, '0');
        }
      }
    } else {
      bool carry;
      std::string r = round_sig(digits, static_cast<size_t>(n), carry);
      int P = point_exp;
      if (carry) {
        P += 1;
        r = "1" + std::string(static_cast<size_t>(n) - 1, '0');
      }
      body = f_form(r, P, prec, o.alt);
    }
  } else {  // g
    size_t n = static_cast<size_t>(prec);
    bool carry;
    std::string r = round_sig(digits, n, carry);
    int P = point_exp;
    if (carry) {
      P += 1;
      r = "1" + std::string(n - 1, '0');
    }
    int X = P - 1;  // decimal exponent of the rounded value
    if (X < -4 || X >= prec) {
      body = e_form(r, P, prec - 1, o.alt, upper);
    } else {
      body = f_form(r, P, prec - 1 - X, o.alt);
    }
    if (!o.alt) {
      // strip trailing zeros and a bare decimal point
      size_t epos = body.find_last_of("eE");
      std::string mant =
          epos == std::string::npos ? body : body.substr(0, epos);
      std::string exp = epos == std::string::npos ? "" : body.substr(epos);
      size_t dot = mant.find('.');
      if (dot != std::string::npos) {
        size_t last = mant.find_last_not_of('0');
        if (last == dot) {
          mant.resize(dot);
        } else {
          mant.resize(last + 1);
        }
      }
      body = mant + exp;
    }
  }

  std::string sign = v.negative ? "-" : (o.plus ? "+" : (o.space ? " " : ""));
  std::string full = sign + body;
  if (static_cast<int>(full.size()) >= o.width) return full;
  if (o.left) return full + std::string(o.width - full.size(), ' ');
  if (o.zero_pad) {
    full.insert(sign.size(), o.width - full.size(), '0');
    return full;
  }
  return std::string(o.width - full.size(), ' ') + full;
}

// Round a parsed value back to a significand of `sig_bits` bits (the
// source type's precision) so a candidate string can be checked against
// the original float/double/long double.
inline Ext80 round_sig_bits(const Ext80& v, int sig_bits) {
  if (v.kind != Ext80::Kind::normal) return v;
  Ext80 r = v;
  int drop = 64 - sig_bits;
  if (drop <= 0) return r;
  uint64_t kept = v.mant >> drop;
  bool rb = ((v.mant >> (drop - 1)) & 1) != 0;
  bool st = (v.mant & ((1ull << (drop - 1)) - 1)) != 0;
  if (rb && (st || (kept & 1) != 0)) {
    ++kept;
    if (kept >> sig_bits) {  // carried out of the significand
      kept >>= 1;
      ++r.exp;
      if (r.exp > 16383) {
        r.kind = Ext80::Kind::inf;
        r.mant = 0;
        return r;
      }
    }
  }
  r.mant = kept << drop;
  return r;
}

// ftoastr/dtoastr/ldtoastr work-alike used by od -t f: print with %.*g
// starting at `dig` significant digits (1 for subnormal magnitudes) and
// raise the precision until the text round-trips to the same value
// at the source type's precision (sig_bits).
inline std::string shortest_g(const Ext80& v, int dig, int prec_bound,
                              int min_normal_exp, int sig_bits) {
  if (v.kind != Ext80::Kind::normal) {
    FormatOpts o;
    o.conv = 'g';
    return format(v, o);
  }
  const Ext80 target = round_sig_bits(v, sig_bits);
  // |v| < T_MIN (smallest normal): start at precision 1 like ftoastr.
  int prec = v.exp < min_normal_exp ? 1 : dig;
  for (;; ++prec) {
    FormatOpts o;
    o.conv = 'g';
    o.precision = prec;
    std::string s = format(v, o);
    const char* end = nullptr;
    bool range_err = false;
    Ext80 back = round_sig_bits(parse(s, end, range_err), sig_bits);
    bool same = back.kind == target.kind && back.negative == target.negative &&
                back.mant == target.mant && back.exp == target.exp;
    if (same || prec >= prec_bound) return s;
  }
}

}  // namespace gnu_float80
