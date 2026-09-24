/*
 *  Copyright © 2026 WinuxCmd
 *
 *  - File: mpicalc.cpp
 *  - CopyrightYear: 2026
 */
#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr MPICALC_OPTIONS =
    std::array{// [EXT]
               OPTION("", "--help", "display help and exit"),
               // [EXT]
               OPTION("", "--version", "display version and exit"),
               // [EXT]
               OPTION("", "--print-config", "print local configuration"),
               // [EXT]
               OPTION("", "--disable-hwf",
                      "accepted for libgcrypt compatibility", STRING_TYPE)};

namespace {
constexpr uint32_t kBaseBits = 32;
constexpr uint64_t kBase = 0x100000000ULL;
constexpr int kPlus = 43;
constexpr int kMinus = 45;
constexpr int kStar = 42;
constexpr int kSlash = 47;
constexpr int kPercent = 37;
constexpr int kLt = 60;
constexpr int kGt = 62;

struct BigInt {
  int sign = 0;
  std::vector<uint32_t> limbs;

  static auto from_uint64(uint64_t value) -> BigInt {
    BigInt out;
    if (value == 0) return out;
    out.sign = 1;
    out.limbs.push_back(static_cast<uint32_t>(value & 0xffffffffULL));
    uint32_t high = static_cast<uint32_t>(value >> 32);
    if (high) out.limbs.push_back(high);
    return out;
  }

  auto normalize() -> void {
    while (!limbs.empty() && limbs.back() == 0) limbs.pop_back();
    if (limbs.empty()) sign = 0;
  }

  [[nodiscard]] auto is_zero() const -> bool { return sign == 0; }

  auto abs_cmp(const BigInt& other) const -> int {
    if (limbs.size() != other.limbs.size()) {
      return limbs.size() < other.limbs.size() ? -1 : 1;
    }
    for (size_t i = limbs.size(); i > 0; --i) {
      uint32_t a = limbs[i - 1];
      uint32_t b = other.limbs[i - 1];
      if (a != b) return a < b ? -1 : 1;
    }
    return 0;
  }

  static auto abs_add(const BigInt& a, const BigInt& b) -> BigInt {
    BigInt out;
    out.sign = 1;
    size_t n = std::max(a.limbs.size(), b.limbs.size());
    out.limbs.resize(n);
    uint64_t carry = 0;
    for (size_t i = 0; i < n; ++i) {
      uint64_t av = i < a.limbs.size() ? a.limbs[i] : 0;
      uint64_t bv = i < b.limbs.size() ? b.limbs[i] : 0;
      uint64_t sum = av + bv + carry;
      out.limbs[i] = static_cast<uint32_t>(sum & 0xffffffffULL);
      carry = sum >> 32;
    }
    if (carry) out.limbs.push_back(static_cast<uint32_t>(carry));
    out.normalize();
    return out;
  }

  static auto abs_sub(const BigInt& a, const BigInt& b) -> BigInt {
    BigInt out;
    out.sign = 1;
    out.limbs.resize(a.limbs.size());
    uint64_t borrow = 0;
    for (size_t i = 0; i < a.limbs.size(); ++i) {
      uint64_t av = a.limbs[i];
      uint64_t bv = i < b.limbs.size() ? b.limbs[i] : 0;
      uint64_t need = bv + borrow;
      if (av >= need) {
        out.limbs[i] = static_cast<uint32_t>(av - need);
        borrow = 0;
      } else {
        out.limbs[i] = static_cast<uint32_t>(kBase + av - need);
        borrow = 1;
      }
    }
    out.normalize();
    return out;
  }

  auto operator-() const -> BigInt {
    BigInt out = *this;
    out.sign = -out.sign;
    return out;
  }

  friend auto operator+(const BigInt& a, const BigInt& b) -> BigInt {
    if (a.sign == 0) return b;
    if (b.sign == 0) return a;
    if (a.sign == b.sign) {
      BigInt out = abs_add(a, b);
      out.sign = a.sign;
      return out;
    }
    int cmp = a.abs_cmp(b);
    if (cmp == 0) return {};
    if (cmp > 0) {
      BigInt out = abs_sub(a, b);
      out.sign = a.sign;
      return out;
    }
    BigInt out = abs_sub(b, a);
    out.sign = b.sign;
    return out;
  }

  friend auto operator-(const BigInt& a, const BigInt& b) -> BigInt {
    return a + (-b);
  }

  friend auto operator*(const BigInt& a, const BigInt& b) -> BigInt {
    if (a.sign == 0 || b.sign == 0) return {};
    BigInt out;
    out.sign = a.sign * b.sign;
    out.limbs.assign(a.limbs.size() + b.limbs.size(), 0);
    for (size_t i = 0; i < a.limbs.size(); ++i) {
      uint64_t carry = 0;
      for (size_t j = 0; j < b.limbs.size(); ++j) {
        uint64_t cur = out.limbs[i + j] + carry +
                       static_cast<uint64_t>(a.limbs[i]) * b.limbs[j];
        out.limbs[i + j] = static_cast<uint32_t>(cur & 0xffffffffULL);
        carry = cur >> 32;
      }
      size_t pos = i + b.limbs.size();
      while (carry) {
        if (pos >= out.limbs.size()) out.limbs.push_back(0);
        uint64_t cur = static_cast<uint64_t>(out.limbs[pos]) + carry;
        out.limbs[pos] = static_cast<uint32_t>(cur & 0xffffffffULL);
        carry = cur >> 32;
        ++pos;
      }
    }
    out.normalize();
    return out;
  }

  auto abs_value() const -> BigInt {
    BigInt out = *this;
    if (out.sign < 0) out.sign = 1;
    return out;
  }

  auto shift_left_one() -> void {
    if (sign == 0) return;
    uint64_t carry = 0;
    for (auto& limb : limbs) {
      uint64_t cur = (static_cast<uint64_t>(limb) << 1) | carry;
      limb = static_cast<uint32_t>(cur & 0xffffffffULL);
      carry = cur >> 32;
    }
    if (carry) limbs.push_back(static_cast<uint32_t>(carry));
  }

  auto shift_right_one() -> void {
    if (sign == 0) return;
    uint32_t carry = 0;
    for (size_t i = limbs.size(); i > 0; --i) {
      uint32_t next_carry = limbs[i - 1] & 1U;
      limbs[i - 1] = (limbs[i - 1] >> 1) | (carry << 31);
      carry = next_carry;
    }
    normalize();
  }

  [[nodiscard]] auto bit_length() const -> size_t {
    if (sign == 0) return 0;
    uint32_t high = limbs.back();
    size_t bits = (limbs.size() - 1) * kBaseBits;
    while (high) {
      ++bits;
      high >>= 1;
    }
    return bits;
  }

  [[nodiscard]] auto get_bit(size_t bit) const -> bool {
    size_t limb = bit / kBaseBits;
    size_t offset = bit % kBaseBits;
    if (limb >= limbs.size()) return false;
    return ((limbs[limb] >> offset) & 1U) != 0;
  }

  auto set_bit(size_t bit) -> void {
    size_t limb = bit / kBaseBits;
    size_t offset = bit % kBaseBits;
    if (limbs.size() <= limb) limbs.resize(limb + 1, 0);
    limbs[limb] |= (1U << offset);
    if (sign == 0) sign = 1;
  }

  auto add_small(uint32_t value) -> void {
    if (value == 0) return;
    if (sign == 0) {
      sign = 1;
      limbs.push_back(value);
      return;
    }
    uint64_t carry = value;
    for (auto& limb : limbs) {
      uint64_t cur = static_cast<uint64_t>(limb) + carry;
      limb = static_cast<uint32_t>(cur & 0xffffffffULL);
      carry = cur >> 32;
      if (!carry) break;
    }
    if (carry) limbs.push_back(static_cast<uint32_t>(carry));
  }

  auto mul_small(uint32_t value) -> void {
    if (sign == 0 || value == 1) return;
    if (value == 0) {
      sign = 0;
      limbs.clear();
      return;
    }
    uint64_t carry = 0;
    for (auto& limb : limbs) {
      uint64_t cur = static_cast<uint64_t>(limb) * value + carry;
      limb = static_cast<uint32_t>(cur & 0xffffffffULL);
      carry = cur >> 32;
    }
    if (carry) limbs.push_back(static_cast<uint32_t>(carry));
  }

  static auto divmod_abs(BigInt a, const BigInt& b)
      -> std::pair<BigInt, BigInt> {
    BigInt divisor = b.abs_value();
    BigInt quotient;
    BigInt rem;
    if (divisor.sign == 0) return {quotient, rem};
    a = a.abs_value();
    if (a.abs_cmp(divisor) < 0) return {quotient, a};
    size_t bits = a.bit_length();
    for (size_t i = bits; i > 0; --i) {
      rem.shift_left_one();
      if (a.get_bit(i - 1)) rem.add_small(1);
      if (rem.abs_cmp(divisor) >= 0) {
        rem = abs_sub(rem, divisor);
        quotient.set_bit(i - 1);
      }
    }
    quotient.normalize();
    rem.normalize();
    return {quotient, rem};
  }

  friend auto operator/(const BigInt& a, const BigInt& b) -> BigInt {
    if (b.sign == 0) return {};
    auto [q, r] = divmod_abs(a, b);
    if (!q.is_zero()) q.sign = a.sign * b.sign;
    return q;
  }

  friend auto operator%(const BigInt& a, const BigInt& b) -> BigInt {
    if (b.sign == 0) return {};
    auto [q, r] = divmod_abs(a, b);
    if (!r.is_zero()) r.sign = a.sign;
    return r;
  }

  [[nodiscard]] auto to_uint64(uint64_t& out) const -> bool {
    if (sign < 0 || limbs.size() > 2) return false;
    out = 0;
    if (!limbs.empty()) out = limbs[0];
    if (limbs.size() > 1) out |= static_cast<uint64_t>(limbs[1]) << 32;
    return true;
  }
};

auto is_hex_digit(int c) -> bool {
  return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

auto hex_value(int c) -> int {
  if (c >= 48 && c <= 57) return c - 48;
  if (c >= 65 && c <= 70) return c - 65 + 10;
  if (c >= 97 && c <= 102) return c - 97 + 10;
  return -1;
}

auto is_number_token(const std::string& token) -> bool {
  if (token.empty()) return false;
  unsigned char first = static_cast<unsigned char>(token[0]);
  if (std::isdigit(first)) return true;
  return token.size() > 1 && token[0] == kMinus && is_hex_digit(token[1]);
}

auto parse_hex_number(const std::string& token) -> std::optional<BigInt> {
  bool neg = false;
  size_t pos = 0;
  if (!token.empty() && token[0] == kMinus) {
    neg = true;
    pos = 1;
  }
  if (pos >= token.size()) return std::nullopt;
  BigInt out;
  for (; pos < token.size(); ++pos) {
    int digit = hex_value(static_cast<unsigned char>(token[pos]));
    if (digit < 0) return std::nullopt;
    out.mul_small(16);
    out.add_small(static_cast<uint32_t>(digit));
  }
  if (!out.is_zero()) out.sign = neg ? -1 : 1;
  return out;
}

auto format_hex(const BigInt& value) -> std::string {
  static const std::string kHex = "0123456789ABCDEF";
  if (value.sign == 0) return "00";
  std::string out;
  for (size_t i = value.limbs.size(); i > 0; --i) {
    uint32_t limb = value.limbs[i - 1];
    for (int shift = 28; shift >= 0; shift -= 4) {
      uint32_t nibble = (limb >> shift) & 0xFU;
      if (!out.empty() || nibble != 0 || i != value.limbs.size() ||
          shift == 0) {
        out.push_back(kHex[nibble]);
      }
    }
  }
  if (out.size() % 2 != 0) out.insert(out.begin(), "0"[0]);
  if (value.sign < 0) out.insert(out.begin(), kMinus);
  return out;
}

auto require_stack(std::vector<BigInt>& stack, size_t need) -> bool {
  if (stack.size() < need) {
    safeErrorPrintLn("stack underflow");
    return false;
  }
  return true;
}

auto mod_positive(BigInt value, const BigInt& mod) -> BigInt {
  if (mod.sign == 0) return {};
  BigInt r = value % mod;
  if (r.sign < 0) r = r + mod.abs_value();
  return r;
}

auto pow_mod(BigInt base, BigInt exp, const BigInt& mod) -> BigInt {
  if (mod.sign == 0) return {};
  base = mod_positive(base, mod);
  BigInt result = BigInt::from_uint64(1);
  BigInt zero;
  BigInt two = BigInt::from_uint64(2);
  while (exp.sign > 0) {
    if (!(exp % two).is_zero()) result = mod_positive(result * base, mod);
    exp = exp / two;
    if (exp.sign > 0) base = mod_positive(base * base, mod);
  }
  return result;
}

auto gcd_value(BigInt a, BigInt b) -> BigInt {
  a = a.abs_value();
  b = b.abs_value();
  while (!b.is_zero()) {
    BigInt r = a % b;
    a = b;
    b = r.abs_value();
  }
  return a;
}

auto prime_check(const BigInt& value) -> BigInt {
  uint64_t n = 0;
  if (!value.to_uint64(n) || n < 2) return {};
  if (n == 2 || n == 3) return BigInt::from_uint64(1);
  if (n % 2 == 0) return {};
  for (uint64_t d = 3; d <= n / d; d += 2) {
    if (n % d == 0) return {};
  }
  return BigInt::from_uint64(1);
}

auto print_help_text() -> void {
  constexpr std::string_view help =
      R"MPICALC(+   add           [0] := [1] + [0]          {-1}
-   subtract      [0] := [1] - [0]          {-1}
*   multiply      [0] := [1] * [0]          {-1}
/   divide        [0] := [1] / [0]          {-1}
%   modulo        [0] := [1] % [0]          {-1}
<   left shift    [0] := [0] << 1           {0}
>   right shift   [0] := [0] >> 1           {0}
++  increment     [0] := [0]++              {0}
--  decrement     [0] := [0]--              {0}
m   multiply mod  [0] := [2] * [1] mod [0]  {-2}
^   power mod     [0] := [2] ^ [1] mod [0]  {-2}
G   gcd           [0] := gcd([1],[0])       {-1}
i   remove item   [0] := [1]                {-1}
d   dup item      [-1] := [0]               {+1}
r   reverse       [0] := [1], [1] := [0]    {0}
b   # of bits     [0] := nbits([0])         {0}
P   prime check   [0] := is_prime([0])?1:0  {0}
c   clear stack
p   print top item
f   print the stack
#   ignore until end of line
?   print this help
)MPICALC";
  safePrint(cmd::meta::format_custom_help(
      "mpicalc", winux::i18n::translate("command.mpicalc.custom_help", help)));
}
auto process_token(const std::string& token, std::vector<BigInt>& stack)
    -> void {
  if (token.empty()) return;
  if (is_number_token(token)) {
    auto value = parse_hex_number(token);
    if (value) {
      if (stack.size() < 500)
        stack.push_back(*value);
      else
        safeErrorPrintLn("stack overflow");
    } else {
      safeErrorPrintLn("invalid number");
    }
    return;
  }
  if (token == "+") {
    if (!require_stack(stack, 2)) return;
    BigInt right = stack.back();
    stack.pop_back();
    stack.back() = stack.back() + right;
  } else if (token == "-") {
    if (!require_stack(stack, 2)) return;
    BigInt right = stack.back();
    stack.pop_back();
    stack.back() = stack.back() - right;
  } else if (token == "*") {
    if (!require_stack(stack, 2)) return;
    BigInt right = stack.back();
    stack.pop_back();
    stack.back() = stack.back() * right;
  } else if (token == "/") {
    if (!require_stack(stack, 2)) return;
    BigInt right = stack.back();
    stack.pop_back();
    stack.back() = stack.back() / right;
  } else if (token == "%") {
    if (!require_stack(stack, 2)) return;
    BigInt right = stack.back();
    stack.pop_back();
    stack.back() = stack.back() % right;
  } else if (token == "++") {
    if (!require_stack(stack, 1)) return;
    stack.back() = stack.back() + BigInt::from_uint64(1);
  } else if (token == "--") {
    if (!require_stack(stack, 1)) return;
    stack.back() = stack.back() - BigInt::from_uint64(1);
  } else if (token == "<") {
    if (!require_stack(stack, 1)) return;
    stack.back().shift_left_one();
  } else if (token == ">") {
    if (!require_stack(stack, 1)) return;
    stack.back().shift_right_one();
  } else if (token == "m") {
    if (!require_stack(stack, 3)) return;
    BigInt mod = stack.back();
    stack.pop_back();
    BigInt rhs = stack.back();
    stack.pop_back();
    stack.back() = mod_positive(stack.back() * rhs, mod);
  } else if (token == "^") {
    if (!require_stack(stack, 3)) return;
    BigInt mod = stack.back();
    stack.pop_back();
    BigInt exp = stack.back();
    stack.pop_back();
    stack.back() = pow_mod(stack.back(), exp, mod);
  } else if (token == "G") {
    if (!require_stack(stack, 2)) return;
    BigInt right = stack.back();
    stack.pop_back();
    stack.back() = gcd_value(stack.back(), right);
  } else if (token == "i") {
    if (!require_stack(stack, 1)) return;
    stack.pop_back();
  } else if (token == "d") {
    if (!require_stack(stack, 1)) return;
    if (stack.size() < 500)
      stack.push_back(stack.back());
    else
      safeErrorPrintLn("stack overflow");
  } else if (token == "r") {
    if (!require_stack(stack, 2)) return;
    std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
  } else if (token == "b") {
    if (!require_stack(stack, 1)) return;
    stack.back() = BigInt::from_uint64(stack.back().abs_value().bit_length());
  } else if (token == "P") {
    if (!require_stack(stack, 1)) return;
    stack.back() = prime_check(stack.back());
  } else if (token == "c") {
    stack.clear();
  } else if (token == "p") {
    if (stack.empty())
      safePrintLn("stack is empty");
    else {
      safePrintLn(format_hex(stack.back()));
      stack.pop_back();
    }
  } else if (token == "f") {
    for (size_t i = stack.size(); i > 0; --i) {
      char index_buf[32];
      sprintf_s(index_buf, sizeof(index_buf), "[%2zu]: ", i - 1);
      safePrint(index_buf);
      safePrintLn(format_hex(stack[i - 1]));
    }
  } else if (token == "?") {
    print_help_text();
  } else {
    safeErrorPrintLn("invalid operator");
  }
}

auto run_calculator() -> int {
  std::vector<BigInt> stack;
  std::string line;
  while (std::getline(std::cin, line)) {
    size_t comment = line.find("#");
    if (comment != std::string::npos) line.erase(comment);
    std::istringstream iss(line);
    std::string token;
    while (iss >> token) process_token(token, stack);
  }
  return 0;
}
}  // namespace

REGISTER_COMMAND(mpicalc, "mpicalc", "mpicalc [options]",
                 "Simple interactive big integer RPN calculator. Values are "
                 "hexadecimal and input is read from standard input.",
                 "  mpicalc < input.txt", "dc, hmac256", "WinuxCmd",
                 "Copyright © 2026 WinuxCmd", MPICALC_OPTIONS) {
  if (ctx.get<bool>("--help", false) || ctx.get<bool>("--version", false)) {
    safePrintLn("mpicalc 2.0");
    safePrintLn("libgcrypt-compatible WinuxCmd implementation");
    safePrintLn("Syntax: mpicalc [options]");
    safePrintLn("Simple interactive big integer RPN calculator");
    return 0;
  }
  // --disable-hwf: accepted for libgcrypt compatibility, ignored on Windows
  (void)ctx.has("--disable-hwf");
  if (ctx.get<bool>("--print-config", false)) {
    safePrintLn("mpi-implementation: winuxcmd-portable");
    return 0;
  }
  if (!ctx.positionals.empty()) {
    safeErrorPrintLn("usage: mpicalc [options]  (--help for help)");
    return 1;
  }
  return run_calculator();
}
