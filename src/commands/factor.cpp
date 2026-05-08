/*
 *  Copyright © 2026 WinuxCmd
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
 *  - File: factor.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for factor command.
/// @Version: 0.1.0
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

auto constexpr FACTOR_OPTIONS =
    std::array{OPTION("", "", "factor numbers", STRING_TYPE)};

namespace {
auto factorize(long long n) -> std::vector<long long> {
  std::vector<long long> factors;

  if (n < 2) {
    return {n};
  }

  // Extract 2s
  while (n % 2 == 0) {
    factors.push_back(2);
    n /= 2;
  }

  // Extract odd factors
  for (long long i = 3; i * i <= n; i += 2) {
    while (n % i == 0) {
      factors.push_back(i);
      n /= i;
    }
  }

  // If n is a prime greater than 2
  if (n > 1) {
    factors.push_back(n);
  }

  return factors;
}

}  // namespace

REGISTER_COMMAND(factor_cmd,
                 /* name */
                 "factor",

                 /* synopsis */
                 "factor [NUMBER]...",
                 "Print the prime factors of each specified integer NUMBER.\n"
                 "\n"
                 "If no NUMBER is specified, read from standard input.",
                 "  factor 12\n"
                 "  factor 100\n"
                 "  factor 17\n"
                 "  echo '24' | factor",

                 /* see also */
                 "primes(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 FACTOR_OPTIONS) {
  std::vector<long long> numbers;

  if (ctx.positionals.empty()) {
    // Read from stdin
    std::string line;
    while (std::getline(std::cin, line)) {
      try {
        numbers.push_back(std::stoll(line));
      } catch (...) {
        safeErrorPrintLn("factor: '" + line + "' is not a valid integer");
      }
    }
  } else {
    for (const auto& arg : ctx.positionals) {
      try {
        numbers.push_back(std::stoll(std::string(arg)));
      } catch (...) {
        safeErrorPrintLn("factor: '" + std::string(arg) +
                         "' is not a valid integer");
      }
    }
  }

  for (auto num : numbers) {
    auto factors = factorize(num);
    safePrint(std::to_string(num) + ":");
    for (auto f : factors) {
      safePrint(" " + std::to_string(f));
    }
    safePrintLn("");
  }

  return 0;
}
