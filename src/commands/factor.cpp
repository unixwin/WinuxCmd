// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for factor command.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "pch/pch.h"
// include other header after pch.h
#include "core/command_macros.h"
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;
using winux::bignum::UInt;

auto constexpr FACTOR_OPTIONS = std::array{
    // [GNU] -h is the GNU spelling of --exponents; -e is a documented
    // WinuxCmd alias (GNU factor 9.x rejects -e).
    OPTION("-e", "--exponents",
           "print repeated factors in form p^e unless e is 1"),
    OPTION("-h", "--exponents",
           "print repeated factors in form p^e unless e is 1")};

namespace {

// [GNU] factor.c accepts leading whitespace and a leading '+' sign, but no
// trailing characters.  Negative numbers are rejected.
auto parse_factor_operand(std::string_view text) -> std::optional<UInt> {
  size_t pos = 0;
  while (pos < text.size() &&
         std::isspace(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  if (pos < text.size() && text[pos] == '+') {
    ++pos;
  }
  const size_t digits_begin = pos;
  while (pos < text.size() &&
         std::isdigit(static_cast<unsigned char>(text[pos]))) {
    ++pos;
  }
  if (pos == digits_begin || pos != text.size()) {
    return std::nullopt;
  }
  return UInt::from_decimal(text.substr(digits_begin));
}

auto report_invalid_operand(std::string_view text) -> void {
  safeErrorPrintLn(winux::i18n::format(
      "command.factor.error.invalid_positive_integer",
      "factor: '{}' is not a valid positive integer", std::string(text)));
}

auto small_primes() -> const std::vector<std::uint64_t>& {
  static const std::vector<std::uint64_t> primes = [] {
    std::vector<std::uint64_t> result;
    for (std::uint64_t candidate = 2; candidate < 1000; ++candidate) {
      bool prime = true;
      for (std::uint64_t p : result) {
        if (p * p > candidate) break;
        if (candidate % p == 0) {
          prime = false;
          break;
        }
      }
      if (prime) result.push_back(candidate);
    }
    return result;
  }();
  return primes;
}

// Appends all prime factors of n (n >= 2) to out, unordered.
auto factor_into(UInt n, std::vector<UInt>& out) -> void {
  for (const std::uint64_t p : small_primes()) {
    while (true) {
      auto [q, r] = divmod_u64(n, p);
      if (r != 0) break;
      out.emplace_back(p);
      n = std::move(q);
    }
    if (n.is_one()) return;
  }

  std::vector<UInt> stack;
  stack.push_back(std::move(n));
  while (!stack.empty()) {
    UInt x = std::move(stack.back());
    stack.pop_back();
    if (winux::bignum::is_probable_prime(x)) {
      out.push_back(std::move(x));
      continue;
    }
    // pollard_rho retries with fresh seeds until it finds a nontrivial
    // factor, so x is always split into two smaller parts here.
    UInt d = winux::bignum::pollard_rho(x);
    stack.push_back(d);
    stack.push_back(divmod(x, d).first);
  }
}

auto factorize(const UInt& n) -> std::vector<UInt> {
  std::vector<UInt> factors;
  if (n < UInt{2}) {
    return factors;  // [GNU] 0 and 1 print with no factors
  }
  factor_into(n, factors);
  std::ranges::sort(factors,
                    [](const UInt& a, const UInt& b) { return a < b; });
  return factors;
}

auto print_factors(const UInt& n, bool exponents) -> void {
  auto factors = factorize(n);
  safePrint(n.to_decimal() + ":");
  if (exponents) {
    // [GNU] group repeated factors: 2 2 2 3 -> 2^3 3
    for (size_t i = 0; i < factors.size();) {
      size_t count = 1;
      while (i + count < factors.size() && factors[i + count] == factors[i]) {
        ++count;
      }
      safePrint(" " + factors[i].to_decimal());
      if (count > 1) {
        safePrint("^" + std::to_string(count));
      }
      i += count;
    }
  } else {
    for (const auto& f : factors) {
      safePrint(" " + f.to_decimal());
    }
  }
  safePrintLn("");
}

}  // namespace

REGISTER_COMMAND(factor_cmd,
                 /* name */
                 "factor",

                 /* synopsis */
                 "factor [OPTION]... [NUMBER]...",
                 "Print the prime factors of each specified integer NUMBER.\n"
                 "\n"
                 "If no NUMBER is specified, read from standard input.\n"
                 "\n"
                 "  -h, --exponents   print repeated factors in form p^e "
                 "unless e is 1\n"
                 "  -e                same as --exponents\n"
                 "      --help        display this help and exit\n"
                 "      --version     output version information and exit",
                 "  factor 12\n"
                 "  factor 100\n"
                 "  factor -h 72\n"
                 "  factor --exponents 17\n"
                 "  echo '24' | factor",

                 /* see also */
                 "primes(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FACTOR_OPTIONS) {
  bool had_error = false;
  bool exponents = ctx.count({"-e", "-h", "--exponents"}) > 0;

  if (ctx.positionals.empty()) {
    // [GNU] read whitespace-separated tokens from standard input
    std::string line;
    while (std::getline(std::cin, line)) {
      size_t pos = 0;
      while (pos < line.size()) {
        while (pos < line.size() &&
               std::isspace(static_cast<unsigned char>(line[pos]))) {
          ++pos;
        }
        const size_t begin = pos;
        while (pos < line.size() &&
               !std::isspace(static_cast<unsigned char>(line[pos]))) {
          ++pos;
        }
        if (begin == pos) break;
        const std::string_view token(line.data() + begin, pos - begin);
        if (auto value = parse_factor_operand(token)) {
          print_factors(*value, exponents);
        } else {
          report_invalid_operand(token);
          had_error = true;
        }
      }
    }
  } else {
    for (const auto& arg : ctx.positionals) {
      if (auto value = parse_factor_operand(arg)) {
        print_factors(*value, exponents);
      } else {
        report_invalid_operand(arg);
        had_error = true;
      }
    }
  }

  return had_error ? 1 : 0;
}
