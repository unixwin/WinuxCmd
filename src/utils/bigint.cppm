// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
// Small self-contained arbitrary-precision integers used by `factor` and
// `expr` where GNU coreutils uses GMP.  Deliberately compact: unsigned
// values live in a little-endian array of 64-bit limbs (bignum::UInt) and
// bignum::Int adds a sign on top.  Division uses Knuth Algorithm D on
// x64 (via _udiv128) with a bit-wise long-division fallback elsewhere.
// The header also provides Miller-Rabin primality testing and
// Pollard-Brent rho factorization for `factor`.

module;

// GMF: global-namespace C names (size_t/uint64_t) and intrinsics that
// `import std` does not provide.
#include <cstddef>
#include <cstdint>
#if defined(_M_X64)
#include <intrin.h>
#endif

export module utils:bigint;
import std;

export namespace winux::bignum {

// Unsigned arbitrary-precision integer: little-endian 64-bit limbs,
// normalized so that zero is the empty vector.
class UInt {
 public:
  UInt() = default;
  UInt(std::uint64_t value) {  // NOLINT(google-explicit-constructor)
    if (value != 0) limbs_.push_back(value);
  }

  // Parses a run of decimal digits.  Signs/whitespace are NOT accepted here;
  // callers normalize their input first.
  [[nodiscard]] static auto from_decimal(std::string_view digits)
      -> std::optional<UInt> {
    if (digits.empty()) return std::nullopt;
    UInt value;
    size_t pos = 0;
    while (pos < digits.size()) {
      const size_t chunk_len = std::min<size_t>(19, digits.size() - pos);
      std::uint64_t chunk = 0;
      for (size_t i = 0; i < chunk_len; ++i) {
        const char ch = digits[pos + i];
        if (ch < '0' || ch > '9') return std::nullopt;
        chunk = chunk * 10 + static_cast<std::uint64_t>(ch - '0');
      }
      std::uint64_t scale = 1;
      for (size_t i = 0; i < chunk_len; ++i) scale *= 10;
      value.mul_add_small(scale, chunk);
      pos += chunk_len;
    }
    return value;
  }

  [[nodiscard]] auto to_decimal() const -> std::string {
    if (is_zero()) return "0";
    UInt tmp = *this;
    std::string out;
    while (!tmp.is_zero()) {
      auto [q, r] = divmod_u64(tmp, 10000000000000000000ull);
      tmp = std::move(q);
      std::string chunk = std::to_string(r);
      if (!tmp.is_zero() && chunk.size() < 19) {
        chunk.insert(0, 19 - chunk.size(), '0');
      }
      out.insert(0, chunk);
    }
    return out;
  }

  [[nodiscard]] auto is_zero() const -> bool { return limbs_.empty(); }
  [[nodiscard]] auto is_one() const -> bool {
    return limbs_.size() == 1 && limbs_[0] == 1;
  }
  [[nodiscard]] auto is_odd() const -> bool {
    return !limbs_.empty() && (limbs_[0] & 1) != 0;
  }
  [[nodiscard]] auto limbs() const -> const std::vector<std::uint64_t>& {
    return limbs_;
  }

  [[nodiscard]] auto compare(const UInt& other) const -> int {
    if (limbs_.size() != other.limbs_.size()) {
      return limbs_.size() < other.limbs_.size() ? -1 : 1;
    }
    for (size_t i = limbs_.size(); i-- > 0;) {
      if (limbs_[i] != other.limbs_[i]) {
        return limbs_[i] < other.limbs_[i] ? -1 : 1;
      }
    }
    return 0;
  }

  friend auto operator==(const UInt& a, const UInt& b) -> bool {
    return a.compare(b) == 0;
  }
  friend auto operator!=(const UInt& a, const UInt& b) -> bool {
    return a.compare(b) != 0;
  }
  friend auto operator<(const UInt& a, const UInt& b) -> bool {
    return a.compare(b) < 0;
  }
  friend auto operator<=(const UInt& a, const UInt& b) -> bool {
    return a.compare(b) <= 0;
  }
  friend auto operator>(const UInt& a, const UInt& b) -> bool {
    return a.compare(b) > 0;
  }
  friend auto operator>=(const UInt& a, const UInt& b) -> bool {
    return a.compare(b) >= 0;
  }

  auto operator+=(const UInt& other) -> UInt& {
    if (limbs_.size() < other.limbs_.size()) {
      limbs_.resize(other.limbs_.size(), 0);
    }
    std::uint64_t carry = 0;
    for (size_t i = 0; i < other.limbs_.size(); ++i) {
      const std::uint64_t s1 = limbs_[i] + other.limbs_[i];
      const bool c1 = s1 < limbs_[i];
      const std::uint64_t s2 = s1 + carry;
      const bool c2 = s2 < s1;
      limbs_[i] = s2;
      carry = (c1 || c2) ? 1 : 0;
    }
    for (size_t i = other.limbs_.size(); carry != 0 && i < limbs_.size(); ++i) {
      limbs_[i] += 1;
      carry = (limbs_[i] == 0) ? 1 : 0;
    }
    if (carry != 0) limbs_.push_back(1);
    return *this;
  }

  // Precondition: *this >= other.
  auto operator-=(const UInt& other) -> UInt& {
    std::uint64_t borrow = 0;
    for (size_t i = 0; i < other.limbs_.size(); ++i) {
      const std::uint64_t sub = other.limbs_[i] + borrow;
      const bool b1 = sub < other.limbs_[i];
      const bool b2 = limbs_[i] < sub;
      limbs_[i] -= sub;
      borrow = (b1 || b2) ? 1 : 0;
    }
    for (size_t i = other.limbs_.size(); borrow != 0 && i < limbs_.size();
         ++i) {
      borrow = (limbs_[i] == 0) ? 1 : 0;
      limbs_[i] -= 1;
    }
    normalize();
    return *this;
  }

  auto operator*=(const UInt& other) -> UInt& {
    *this = *this * other;
    return *this;
  }

  friend auto operator+(const UInt& a, const UInt& b) -> UInt {
    UInt r = a;
    r += b;
    return r;
  }
  friend auto operator-(const UInt& a, const UInt& b) -> UInt {
    UInt r = a;
    r -= b;
    return r;
  }
  friend auto operator*(const UInt& a, const UInt& b) -> UInt {
    if (a.is_zero() || b.is_zero()) return UInt{};
    UInt r;
    r.limbs_.assign(a.limbs_.size() + b.limbs_.size() + 1, 0);
    for (size_t i = 0; i < a.limbs_.size(); ++i) {
      for (size_t j = 0; j < b.limbs_.size(); ++j) {
        auto [hi, lo] = mul64(a.limbs_[i], b.limbs_[j]);
        r.add128_at(i + j, lo, hi);
      }
    }
    r.normalize();
    return r;
  }

  // Full division: returns {quotient, remainder}.  den must be non-zero.
  friend auto divmod(const UInt& num, const UInt& den)
      -> std::pair<UInt, UInt> {
    if (num < den) return {UInt{}, num};
#if defined(_M_X64)
    if (den.limbs_.size() == 1) {
      auto [q, r] = divmod_u64(num, den.limbs_[0]);
      return {q, UInt{r}};
    }
    return divmod_knuth(num, den);
#else
    // Portable fallback: bit-wise long division.
    UInt q;
    q.limbs_.assign(num.limbs_.size(), 0);
    UInt r;
    for (size_t i = num.bit_length(); i-- > 0;) {
      r.shl1_inplace();
      if (num.test_bit(i)) {
        if (r.limbs_.empty()) {
          r.limbs_.push_back(1);
        } else {
          r.limbs_[0] |= 1;
        }
      }
      if (r >= den) {
        r -= den;
        q.limbs_[i >> 6] |= (std::uint64_t{1} << (i & 63));
      }
    }
    q.normalize();
    return {q, r};
#endif
  }

  friend auto operator/(const UInt& a, const UInt& b) -> UInt {
    return divmod(a, b).first;
  }
  friend auto operator%(const UInt& a, const UInt& b) -> UInt {
    return divmod(a, b).second;
  }

  // Divide by a single-limb divisor; den must be non-zero.
  friend auto divmod_u64(const UInt& num, std::uint64_t den)
      -> std::pair<UInt, std::uint64_t> {
    UInt q;
    q.limbs_.assign(num.limbs_.size(), 0);
    std::uint64_t rem = 0;
    for (size_t i = num.limbs_.size(); i-- > 0;) {
      const std::uint64_t limb = num.limbs_[i];
#if defined(_M_X64)
      // rem < den, so the quotient always fits in 64 bits.
      q.limbs_[i] = _udiv128(rem, limb, den, &rem);
#else
      std::uint64_t qd = 0;
      for (int b = 63; b >= 0; --b) {
        // rem < den <= 2^64-1, so rem*2+bit < 2^65 and at most one
        // subtraction is needed.  When the shift overflows, the true
        // remainder is 2^64 + rem which is >= den for sure.
        const std::uint64_t carry = rem >> 63;
        rem = (rem << 1) | ((limb >> b) & 1);
        if (carry != 0 || rem >= den) {
          rem -= den;  // wraps correctly in the carry case
          qd |= (std::uint64_t{1} << b);
        }
      }
      q.limbs_[i] = qd;
#endif
    }
    q.normalize();
    return {q, rem};
  }

  friend auto mod_u64(const UInt& num, std::uint64_t den) -> std::uint64_t {
    return divmod_u64(num, den).second;
  }

  auto operator<<=(size_t bits) -> UInt& {
    if (is_zero()) return *this;
    const size_t limb_shift = bits / 64;
    const unsigned bit_shift = static_cast<unsigned>(bits % 64);
    if (bit_shift != 0) {
      limbs_.push_back(0);
      for (size_t i = limbs_.size(); i-- > 0;) {
        const std::uint64_t carry =
            i > 0 ? (limbs_[i - 1] >> (64 - bit_shift)) : 0;
        limbs_[i] = (limbs_[i] << bit_shift) | carry;
      }
    }
    if (limb_shift != 0) {
      limbs_.insert(limbs_.begin(), limb_shift, 0);
    }
    normalize();
    return *this;
  }

  auto operator>>=(size_t bits) -> UInt& {
    const size_t limb_shift = bits / 64;
    const unsigned bit_shift = static_cast<unsigned>(bits % 64);
    if (limb_shift >= limbs_.size()) {
      limbs_.clear();
      return *this;
    }
    if (limb_shift != 0) {
      limbs_.erase(limbs_.begin(), limbs_.begin() + limb_shift);
    }
    if (bit_shift != 0) {
      for (size_t i = 0; i < limbs_.size(); ++i) {
        const std::uint64_t carry =
            (i + 1 < limbs_.size()) ? (limbs_[i + 1] << (64 - bit_shift)) : 0;
        limbs_[i] = (limbs_[i] >> bit_shift) | carry;
      }
    }
    normalize();
    return *this;
  }

  friend auto operator<<(UInt a, size_t bits) -> UInt {
    a <<= bits;
    return a;
  }
  friend auto operator>>(UInt a, size_t bits) -> UInt {
    a >>= bits;
    return a;
  }

  [[nodiscard]] auto bit_length() const -> size_t {
    if (limbs_.empty()) return 0;
    return (limbs_.size() - 1) * 64 +
           static_cast<size_t>(std::bit_width(limbs_.back()));
  }

  [[nodiscard]] auto test_bit(size_t i) const -> bool {
    const size_t limb = i >> 6;
    if (limb >= limbs_.size()) return false;
    return ((limbs_[limb] >> (i & 63)) & 1) != 0;
  }

  // Number of trailing zero bits; 0 when the value is zero.
  [[nodiscard]] auto trailing_zeros() const -> size_t {
    for (size_t i = 0; i < limbs_.size(); ++i) {
      if (limbs_[i] != 0) {
        return i * 64 + static_cast<size_t>(std::countr_zero(limbs_[i]));
      }
    }
    return 0;
  }

 private:
  std::vector<std::uint64_t> limbs_;  // little-endian, normalized

  void normalize() {
    while (!limbs_.empty() && limbs_.back() == 0) limbs_.pop_back();
  }

  // 64x64 -> 128 multiply using 32-bit halves (portable; MSVC has no
  // __int128).
  static auto mul64(std::uint64_t a, std::uint64_t b)
      -> std::pair<std::uint64_t, std::uint64_t> {
    constexpr std::uint64_t kMask = 0xffffffffull;
    const std::uint64_t a0 = a & kMask, a1 = a >> 32;
    const std::uint64_t b0 = b & kMask, b1 = b >> 32;
    const std::uint64_t p0 = a0 * b0, p1 = a0 * b1;
    const std::uint64_t p2 = a1 * b0, p3 = a1 * b1;
    const std::uint64_t mid = p1 + (p0 >> 32);  // cannot overflow
    const std::uint64_t mid2 = mid + p2;
    const std::uint64_t carry_hi = mid2 < mid ? 1 : 0;
    const std::uint64_t hi = p3 + (mid2 >> 32) + (carry_hi << 32);
    const std::uint64_t lo = (mid2 << 32) | (p0 & kMask);
    return {hi, lo};
  }

#if defined(_M_X64)
  // Knuth TAOCP 4.3.1 Algorithm D for a multi-limb divisor (den must have
  // at least two limbs; caller guarantees num >= den).  Uses _udiv128 for
  // the two-limb-by-one-limb quotient estimates.
  static auto divmod_knuth(const UInt& num, const UInt& den)
      -> std::pair<UInt, UInt> {
    const size_t n = den.limbs_.size();
    const size_t m = num.limbs_.size() - n;
    const auto shift =
        static_cast<unsigned>(std::countl_zero(den.limbs_.back()));
    UInt v = den << shift;
    UInt u = num << shift;
    v.limbs_.resize(n, 0);
    u.limbs_.resize(m + n + 1, 0);
    UInt q;
    q.limbs_.assign(m + 1, 0);
    const std::uint64_t vtop = v.limbs_[n - 1];
    const std::uint64_t vnext = v.limbs_[n - 2];
    for (size_t jj = m + 1; jj-- > 0;) {
      const size_t j = jj;
      // Estimate qhat = floor((u[j+n]*B + u[j+n-1]) / vtop).
      std::uint64_t qhat;
      std::uint64_t rhat;
      bool rhat_big = false;
      if (u.limbs_[j + n] == vtop) {
        qhat = std::numeric_limits<std::uint64_t>::max();
        rhat = u.limbs_[j + n - 1] + vtop;
        rhat_big = rhat < u.limbs_[j + n - 1];
      } else {
        qhat = _udiv128(u.limbs_[j + n], u.limbs_[j + n - 1], vtop, &rhat);
      }
      // Refine: qhat is too large while qhat*vnext > rhat*B + u[j+n-2].
      while (!rhat_big) {
        const auto [hi, lo] = mul64(qhat, vnext);
        if (hi < rhat || (hi == rhat && lo <= u.limbs_[j + n - 2])) {
          break;
        }
        --qhat;
        rhat += vtop;
        rhat_big = rhat < vtop;
      }
      // u[j..j+n] -= qhat * v.
      std::uint64_t carry_p = 0;
      std::uint64_t borrow = 0;
      for (size_t i = 0; i < n; ++i) {
        const auto [phi, plo] = mul64(qhat, v.limbs_[i]);
        const std::uint64_t s1 = plo + carry_p;
        const bool c1 = s1 < plo;
        const std::uint64_t sub = s1 + borrow;
        const bool c2 = sub < s1;
        carry_p = phi + ((c1 || c2) ? 1 : 0);
        const bool nb = u.limbs_[j + i] < sub;
        u.limbs_[j + i] -= sub;
        borrow = nb ? 1 : 0;
      }
      const bool neg1 = u.limbs_[j + n] < carry_p;
      u.limbs_[j + n] -= carry_p;
      const bool neg2 = u.limbs_[j + n] < borrow;
      u.limbs_[j + n] -= borrow;
      q.limbs_[j] = qhat;
      if (neg1 || neg2) {
        // qhat was one too large: add the divisor back.
        --q.limbs_[j];
        std::uint64_t c = 0;
        for (size_t i = 0; i < n; ++i) {
          const std::uint64_t s3 = u.limbs_[j + i] + v.limbs_[i];
          const bool a1 = s3 < u.limbs_[j + i];
          const std::uint64_t s4 = s3 + c;
          const bool a2 = s4 < s3;
          u.limbs_[j + i] = s4;
          c = (a1 || a2) ? 1 : 0;
        }
        u.limbs_[j + n] += c;
      }
    }
    q.normalize();
    u >>= shift;
    u.normalize();
    return {q, u};
  }
#endif

  // Adds the 128-bit value (hi:lo) into limbs_ starting at index pos.
  void add128_at(size_t pos, std::uint64_t lo, std::uint64_t hi) {
    std::uint64_t s = limbs_[pos] + lo;
    std::uint64_t carry = (s < limbs_[pos]) ? 1 : 0;
    limbs_[pos] = s;
    s = limbs_[pos + 1] + hi;
    const bool c2 = s < limbs_[pos + 1];
    s += carry;  // carry-in from the low-limb add
    const bool c3 = s < carry;
    limbs_[pos + 1] = s;
    carry = (c2 ? 1 : 0) + (c3 ? 1 : 0);
    pos += 2;
    while (carry != 0) {
      if (pos >= limbs_.size()) {
        limbs_.push_back(carry);
        return;
      }
      s = limbs_[pos] + carry;
      carry = (s < limbs_[pos]) ? 1 : 0;
      limbs_[pos] = s;
      ++pos;
    }
  }

  void shl1_inplace() {
    if (limbs_.empty()) return;
    std::uint64_t carry = 0;
    for (auto& limb : limbs_) {
      const std::uint64_t next_carry = limb >> 63;
      limb = (limb << 1) | carry;
      carry = next_carry;
    }
    if (carry != 0) limbs_.push_back(carry);
  }

  // *this = *this * m + a for small m, a.
  void mul_add_small(std::uint64_t m, std::uint64_t a) {
    std::uint64_t carry = a;
    for (auto& limb : limbs_) {
      auto [hi, lo] = mul64(limb, m);
      const std::uint64_t sum = lo + carry;
      carry = hi + (sum < lo ? 1 : 0);
      limb = sum;
    }
    if (carry != 0) limbs_.push_back(carry);
  }
};

// Binary GCD (Stein's algorithm) — avoids a division dependency in the
// hot loop of Pollard-Brent rho.
inline auto gcd(UInt a, UInt b) -> UInt {
  if (a.is_zero()) return b;
  if (b.is_zero()) return a;
  const size_t sa = a.trailing_zeros();
  const size_t sb = b.trailing_zeros();
  const size_t s = std::min(sa, sb);
  a >>= sa;
  b >>= sb;
  while (a != b) {
    if (a > b) {
      a -= b;
      a >>= a.trailing_zeros();
    } else {
      b -= a;
      b >>= b.trailing_zeros();
    }
  }
  a <<= s;
  return a;
}

inline auto powmod(UInt base, const UInt& exp, const UInt& mod) -> UInt {
  UInt result{1};
  base = divmod(base, mod).second;
  for (size_t i = 0; i < exp.bit_length(); ++i) {
    if (exp.test_bit(i)) result = divmod(result * base, mod).second;
    base = divmod(base * base, mod).second;
  }
  return result;
}

// Miller-Rabin against the first 25 prime bases: deterministic below
// 3.3e24 and a strong probable-prime test beyond (the same approach GMP's
// mpz_probab_prime_p uses for GNU factor).
inline auto is_probable_prime(const UInt& n) -> bool {
  static constexpr std::uint64_t kBases[] = {2,  3,  5,  7,  11, 13, 17, 19, 23,
                                             29, 31, 37, 41, 43, 47, 53, 59, 61,
                                             67, 71, 73, 79, 83, 89, 97};
  if (!n.is_odd()) return n == UInt{2};
  for (const std::uint64_t p : kBases) {
    if (n == UInt{p}) return true;
    if (mod_u64(n, p) == 0) return false;
  }
  UInt d = n - UInt{1};
  const size_t s = d.trailing_zeros();
  d >>= s;
  const UInt nm1 = n - UInt{1};
  for (const std::uint64_t base : kBases) {
    UInt x = powmod(UInt{base}, d, n);
    if (x == UInt{1} || x == nm1) continue;
    bool composite = true;
    for (size_t i = 1; i < s; ++i) {
      x = divmod(x * x, n).second;
      if (x == nm1) {
        composite = false;
        break;
      }
    }
    if (composite) return false;
  }
  return true;
}

namespace detail {

class XorShift64 {
 public:
  explicit XorShift64(std::uint64_t seed) : state_(seed) {
    if (state_ == 0) state_ = 0x9e3779b97f4a7c15ull;
  }
  auto next() -> std::uint64_t {
    state_ ^= state_ << 13;
    state_ ^= state_ >> 7;
    state_ ^= state_ << 17;
    return state_;
  }

 private:
  std::uint64_t state_;
};

inline auto random_below(const UInt& n, XorShift64& rng) -> UInt {
  if (n.limbs().size() == 1) {
    return UInt{rng.next() % n.limbs()[0]};
  }
  // Fill a same-width random value then reduce mod n.
  UInt tmp;
  for (size_t i = n.limbs().size(); i-- > 0;) {
    tmp = (tmp << 64) + UInt{rng.next()};
  }
  return divmod(tmp, n).second;
}

}  // namespace detail

// Pollard-Brent rho: returns a nontrivial factor of the composite n.
// Like GNU factor this keeps retrying with fresh polynomials/seeds until a
// factor is found — a composite cofactor is never emitted as a "factor",
// so pathological inputs take a long time rather than printing wrong
// output.
inline auto pollard_rho(const UInt& n) -> UInt {
  if (!n.is_odd()) return UInt{2};
  constexpr std::uint64_t kBatch = 64;
  for (std::uint64_t attempt = 1;; ++attempt) {
    detail::XorShift64 rng(n.limbs()[0] ^ (attempt * 0x9e3779b97f4a7c15ull) ^
                           (n.bit_length() << 1));
    UInt y = detail::random_below(n, rng);
    const UInt c = detail::random_below(n, rng);
    UInt g{1}, q{1}, x, ys;
    std::uint64_t r = 1;
    auto f = [&n, &c](const UInt& v) -> UInt {
      return divmod(v * v + c, n).second;
    };
    while (g == UInt{1}) {
      x = y;
      for (std::uint64_t i = 0; i < r; ++i) {
        y = f(y);
      }
      std::uint64_t k = 0;
      while (k < r && g == UInt{1}) {
        ys = y;
        const std::uint64_t lim = std::min<std::uint64_t>(kBatch, r - k);
        for (std::uint64_t i = 0; i < lim; ++i) {
          y = f(y);
          const UInt diff = x > y ? x - y : y - x;
          q = divmod(q * diff, n).second;
        }
        g = gcd(q, n);
        k += lim;
      }
      r <<= 1;
    }
    if (g == n) {
      do {
        ys = f(ys);
        const UInt diff = x > ys ? x - ys : ys - x;
        g = gcd(diff, n);
      } while (g == UInt{1});
    }
    if (g != n) return g;
    // g == n: this polynomial/seed combination failed — try again.
  }
}

// Signed arbitrary-precision integer for `expr` arithmetic.
class Int {
 public:
  Int() = default;
  Int(long long value) {  // NOLINT(google-explicit-constructor)
    neg_ = value < 0;
    // 0 - (u64)value wraps to |value| even for LLONG_MIN.
    mag_ = UInt{neg_ ? (std::uint64_t{0} - static_cast<std::uint64_t>(value))
                     : static_cast<std::uint64_t>(value)};
    fix_sign();
  }

  [[nodiscard]] static auto from_decimal(std::string_view text)
      -> std::optional<Int> {
    if (text.empty()) return std::nullopt;
    bool neg = false;
    if (text.front() == '-') {
      neg = true;
      text.remove_prefix(1);
    }
    auto mag = UInt::from_decimal(text);
    if (!mag) return std::nullopt;
    Int r;
    r.neg_ = neg;
    r.mag_ = std::move(*mag);
    r.fix_sign();
    return r;
  }

  [[nodiscard]] auto to_decimal() const -> std::string {
    if (is_zero()) return "0";
    return (neg_ ? "-" : "") + mag_.to_decimal();
  }

  [[nodiscard]] auto is_zero() const -> bool { return mag_.is_zero(); }
  [[nodiscard]] auto negative() const -> bool { return neg_; }
  [[nodiscard]] auto magnitude() const -> const UInt& { return mag_; }

  [[nodiscard]] auto compare(const Int& other) const -> int {
    if (neg_ != other.neg_) return neg_ ? -1 : 1;
    const int c = mag_.compare(other.mag_);
    return neg_ ? -c : c;
  }

  friend auto operator==(const Int& a, const Int& b) -> bool {
    return a.compare(b) == 0;
  }
  friend auto operator!=(const Int& a, const Int& b) -> bool {
    return a.compare(b) != 0;
  }
  friend auto operator<(const Int& a, const Int& b) -> bool {
    return a.compare(b) < 0;
  }
  friend auto operator<=(const Int& a, const Int& b) -> bool {
    return a.compare(b) <= 0;
  }
  friend auto operator>(const Int& a, const Int& b) -> bool {
    return a.compare(b) > 0;
  }
  friend auto operator>=(const Int& a, const Int& b) -> bool {
    return a.compare(b) >= 0;
  }

  friend auto operator-(const Int& a) -> Int {
    Int r = a;
    r.neg_ = !r.neg_;
    r.fix_sign();
    return r;
  }

  friend auto operator+(const Int& a, const Int& b) -> Int {
    if (a.neg_ == b.neg_) {
      Int r;
      r.neg_ = a.neg_;
      r.mag_ = a.mag_ + b.mag_;
      r.fix_sign();
      return r;
    }
    const int c = a.mag_.compare(b.mag_);
    if (c == 0) return Int{0};
    Int r;
    if (c > 0) {
      r.neg_ = a.neg_;
      r.mag_ = a.mag_ - b.mag_;
    } else {
      r.neg_ = b.neg_;
      r.mag_ = b.mag_ - a.mag_;
    }
    return r;
  }

  friend auto operator-(const Int& a, const Int& b) -> Int { return a + (-b); }

  friend auto operator*(const Int& a, const Int& b) -> Int {
    Int r;
    r.neg_ = a.neg_ != b.neg_;
    r.mag_ = a.mag_ * b.mag_;
    r.fix_sign();
    return r;
  }

  // Truncating division/remainder (C/GNU expr semantics: remainder takes
  // the dividend's sign).  Callers must check for a zero divisor first.
  friend auto operator/(const Int& a, const Int& b) -> Int {
    Int r;
    r.neg_ = a.neg_ != b.neg_;
    r.mag_ = a.mag_ / b.mag_;
    r.fix_sign();
    return r;
  }
  friend auto operator%(const Int& a, const Int& b) -> Int {
    Int r;
    r.neg_ = a.neg_;
    r.mag_ = a.mag_ % b.mag_;
    r.fix_sign();
    return r;
  }

  // Saturating conversion for APIs that still take machine integers
  // (expr substr positions, etc.).
  [[nodiscard]] auto to_i64() const -> long long {
    if (mag_.bit_length() > 63) {
      return neg_ ? std::numeric_limits<long long>::min()
                  : std::numeric_limits<long long>::max();
    }
    const std::uint64_t v = mag_.is_zero() ? 0 : mag_.limbs()[0];
    return neg_ ? -static_cast<long long>(v) : static_cast<long long>(v);
  }

 private:
  bool neg_ = false;
  UInt mag_;

  void fix_sign() {
    if (mag_.is_zero()) neg_ = false;
  }
};

}  // namespace winux::bignum
