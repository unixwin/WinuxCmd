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
 *  - File: mpicalc.cpp
 *  - CopyrightYear: 2026
 */
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for mpicalc command.
/// @Version: 0.1.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"

import std;
import core;
import utils;
import container;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

// ======================================================
// Options (constexpr)
// ======================================================

auto constexpr MPICALC_OPTIONS =
    std::array{OPTION("", "--help", "display this help and exit")};

// ======================================================
// Helper functions
// ======================================================

namespace {
// Simple expression evaluator
double evaluate_expression(const std::string& expr, size_t& pos) {
  double result = 0.0;

  // Handle parentheses
  if (pos < expr.length() && expr[pos] == '(') {
    pos++;  // Skip '('
    result = evaluate_expression(expr, pos);
    if (pos < expr.length() && expr[pos] == ')') {
      pos++;  // Skip ')'
    }
    return result;
  }

  // Handle unary operators
  if (pos < expr.length() && (expr[pos] == '+' || expr[pos] == '-')) {
    double sign = (expr[pos] == '-') ? -1.0 : 1.0;
    pos++;
    return sign * evaluate_expression(expr, pos);
  }

  // Handle numbers
  std::string num_str;
  while (pos < expr.length() && (std::isdigit(expr[pos]) || expr[pos] == '.')) {
    num_str += expr[pos++];
  }

  if (!num_str.empty()) {
    result = std::stod(num_str);
  } else {
    // Handle functions
    std::string func;
    while (pos < expr.length() && std::isalpha(expr[pos])) {
      func += expr[pos++];
    }

    if (func == "sin" || func == "cos" || func == "tan" || func == "sqrt" ||
        func == "log" || func == "ln" || func == "exp" || func == "abs" ||
        func == "floor" || func == "ceil") {
      if (pos < expr.length() && expr[pos] == '(') {
        pos++;  // Skip '('
        double arg = evaluate_expression(expr, pos);
        if (pos < expr.length() && expr[pos] == ')') {
          pos++;  // Skip ')'
        }

        if (func == "sin")
          result = std::sin(arg);
        else if (func == "cos")
          result = std::cos(arg);
        else if (func == "tan")
          result = std::tan(arg);
        else if (func == "sqrt")
          result = std::sqrt(arg);
        else if (func == "log")
          result = std::log10(arg);
        else if (func == "ln")
          result = std::log(arg);
        else if (func == "exp")
          result = std::exp(arg);
        else if (func == "abs")
          result = std::abs(arg);
        else if (func == "floor")
          result = std::floor(arg);
        else if (func == "ceil")
          result = std::ceil(arg);
      }
    }
  }

  // Handle binary operators (left to right)
  while (pos < expr.length()) {
    char op = expr[pos];

    if (op == '+' || op == '-' || op == '*' || op == '/' || op == '%' ||
        op == '^') {
      pos++;
      double right = evaluate_expression(expr, pos);

      switch (op) {
        case '+':
          result += right;
          break;
        case '-':
          result -= right;
          break;
        case '*':
          result *= right;
          break;
        case '/':
          if (right != 0)
            result /= right;
          else
            result = std::numeric_limits<double>::infinity();
          break;
        case '%':
          result = std::fmod(result, right);
          break;
        case '^':
          result = std::pow(result, right);
          break;
      }
    } else if (op == ')') {
      break;
    } else if (!std::isspace(op)) {
      pos++;
    } else {
      pos++;
    }
  }

  return result;
}

// Format result
std::string format_result(double value) {
  if (std::isinf(value)) {
    return (value > 0) ? "Infinity" : "-Infinity";
  }
  if (std::isnan(value)) {
    return "NaN";
  }

  // Check if result is close to an integer
  double int_part;
  if (std::modf(value, &int_part) < 1e-10) {
    return std::to_string(static_cast<long long>(int_part));
  }

  // Format with appropriate precision
  char buffer[128];
  sprintf_s(buffer, sizeof(buffer), "%.10g", value);

  // Remove trailing zeros
  std::string result = buffer;
  size_t dot_pos = result.find('.');
  if (dot_pos != std::string::npos) {
    size_t last_non_zero = result.find_last_not_of('0');
    if (last_non_zero != std::string::npos) {
      result = result.substr(0, last_non_zero + 1);
    }
    if (result.back() == '.') {
      result.pop_back();
    }
  }

  return result;
}
}  // namespace

// ======================================================
// Main command implementation
// ======================================================

REGISTER_COMMAND(
    mpicalc,
    /* cmd_name */ "mpicalc",
    /* cmd_synopsis */ "mpicalc [EXPRESSION]",
    /* cmd_desc */
    "Arbitrary precision calculator.\n"
    "Evaluate mathematical expressions with support for basic arithmetic,\n"
    "trigonometric functions, and other mathematical operations.",
    /* examples */
    "  mpicalc '2+2'\n"
    "  mpicalc '3*4+5'\n"
    "  mpicalc 'sin(pi/2)'\n"
    "  mpicalc 'sqrt(16)+2^3'\n"
    "  mpicalc 'log(100)'\n"
    "  mpicalc '(1+2)*(3+4)'",
    /* see_also */ "bc, expr",
    /* author */ "WinuxCmd",
    /* copyright */ "Copyright © 2026 WinuxCmd",
    /* options */ MPICALC_OPTIONS) {
  if (ctx.positionals.empty()) {
    safeErrorPrintLn("mpicalc: missing expression");
    safePrintLn("Usage: mpicalc [EXPRESSION]");
    safePrintLn("");
    safePrintLn("Supported operators:");
    safePrintLn(
        "  + - * / % ^ (add, subtract, multiply, divide, modulo, power)");
    safePrintLn("");
    safePrintLn("Supported functions:");
    safePrintLn("  sin, cos, tan - trigonometric functions");
    safePrintLn("  sqrt - square root");
    safePrintLn("  log - base-10 logarithm");
    safePrintLn("  ln - natural logarithm");
    safePrintLn("  exp - exponential");
    safePrintLn("  abs - absolute value");
    safePrintLn("  floor, ceil - floor and ceiling");
    safePrintLn("");
    safePrintLn("Examples:");
    safePrintLn("  mpicalc '2+2'");
    safePrintLn("  mpicalc 'sqrt(16)+2^3'");
    return 1;
  }

  std::string expr = std::string(ctx.positionals[0]);

  // Replace common constants
  size_t pos;
  while ((pos = expr.find("pi")) != std::string::npos) {
    expr.replace(pos, 2, std::to_string(3.14159265358979323846));
  }
  while ((pos = expr.find("PI")) != std::string::npos) {
    expr.replace(pos, 2, std::to_string(3.14159265358979323846));
  }
  while ((pos = expr.find("e")) != std::string::npos) {
    expr.replace(pos, 1, std::to_string(2.71828182845904523536));
  }
  while ((pos = expr.find("E")) != std::string::npos) {
    expr.replace(pos, 1, std::to_string(2.71828182845904523536));
  }

  try {
    size_t pos = 0;
    double result = evaluate_expression(expr, pos);
    safePrintLn(format_result(result));
  } catch (const std::exception& e) {
    safeErrorPrintLn("mpicalc: error evaluating expression - " +
                     std::string(e.what()));
    return 1;
  } catch (...) {
    safeErrorPrintLn("mpicalc: error evaluating expression");
    return 1;
  }

  return 0;
}
