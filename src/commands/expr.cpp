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
 *  - File: expr.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for expr command.
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

auto constexpr EXPR_OPTIONS =
    std::array{OPTION("", "", "evaluate expressions", STRING_TYPE)};

// Simple expression evaluator for basic arithmetic
namespace {
auto evaluate_expr(std::span<const std::string_view> args)
    -> std::expected<long long, std::string> {
  if (args.empty()) {
    return std::unexpected("expr: missing operand");
  }

  if (args.size() == 1) {
    try {
      return std::stoll(std::string(args[0]));
    } catch (...) {
      return std::unexpected("expr: non-numeric argument");
    }
  }

  // Basic support for: a + b, a - b, a * b, a / b, a % b
  if (args.size() != 3) {
    return std::unexpected("expr: syntax error");
  }

  try {
    auto left = std::stoll(std::string(args[0]));
    auto op = args[1];
    auto right = std::stoll(std::string(args[2]));

    if (op == "+") {
      return left + right;
    } else if (op == "-") {
      return left - right;
    } else if (op == "*") {
      return left * right;
    } else if (op == "/") {
      if (right == 0) {
        return std::unexpected("expr: division by zero");
      }
      return left / right;
    } else if (op == "%") {
      if (right == 0) {
        return std::unexpected("expr: division by zero");
      }
      return left % right;
    } else if (op == "<") {
      return left < right ? 1 : 0;
    } else if (op == "<=") {
      return left <= right ? 1 : 0;
    } else if (op == "=") {
      return left == right ? 1 : 0;
    } else if (op == "!=") {
      return left != right ? 1 : 0;
    } else if (op == ">=") {
      return left >= right ? 1 : 0;
    } else if (op == ">") {
      return left > right ? 1 : 0;
    } else if (op == "&") {
      return (left != 0 && right != 0) ? 1 : 0;
    } else if (op == "|") {
      return (left != 0 || right != 0) ? 1 : 0;
    } else {
      return std::unexpected("expr: invalid operator");
    }
  } catch (const std::exception&) {
    return std::unexpected("expr: non-numeric argument");
  }
}

}  // namespace

REGISTER_COMMAND(expr_cmd,
                 /* name */
                 "expr",

                 /* synopsis */
                 "expr EXPRESSION",
                 "Evaluate expressions.\n"
                 "\n"
                 "Print the value of EXPRESSION to standard output.\n"
                 "\n"
                 "Supported operators:\n"
                 "  +, -      Addition, subtraction\n"
                 "  *, /, %   Multiplication, division, modulus\n"
                 "  <, <=, =, !=, >=, >  Comparison\n"
                 "  &, |      Logical AND, OR\n"
                 "\n"
                 "Note: This is a basic implementation supporting simple "
                 "two-operand expressions.",
                 "  expr 2 + 3\n"
                 "  expr 5 \\* 10\n"
                 "  expr 10 / 2\n"
                 "  expr 5 % 3\n"
                 "  expr 2 \\\< 5",

                 /* see also */
                 "test(1), let(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd",
                 EXPR_OPTIONS) {
  auto result = evaluate_expr(ctx.positionals);
  if (!result) {
    safeErrorPrintLn(result.error());
    return 1;
  }

  safePrintLn(std::to_string(*result));

  // Return non-zero if result is 0 (for logical expressions)
  return *result == 0 ? 1 : 0;
}
