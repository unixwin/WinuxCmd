// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
/// @contributors:
///   - caomengxuan666 <2507560089@qq.com>
/// @Description: Implementation for expr command.
/// @Version: 0.2.0
/// @License: MIT
/// @Copyright: Copyright © 2026 WinuxCmd

#include "core/command_macros.h"
#include "pch/pch.h"
import std;
import core;
import utils;

using cmd::meta::OptionMeta;
using cmd::meta::OptionType;

auto constexpr EXPR_OPTIONS =
    // [GNU]
    std::array{OPTION("", "", "evaluate expressions", STRING_TYPE)};

namespace {

struct ExprError {
  std::string message;
};

struct Value {
  enum class Kind { Integer, String };

  Kind kind = Kind::String;
  winux::bignum::Int integer{};
  std::string text;

  static auto make_integer(winux::bignum::Int value) -> Value {
    Value v;
    v.kind = Kind::Integer;
    v.integer = std::move(value);
    return v;
  }

  static auto make_integer(long long value) -> Value {
    return make_integer(winux::bignum::Int{value});
  }

  static auto make_string(std::string_view value) -> Value {
    Value v;
    v.kind = Kind::String;
    v.text.assign(value.begin(), value.end());
    return v;
  }

  [[nodiscard]] auto as_string() const -> std::string {
    if (kind == Kind::Integer) return integer.to_decimal();
    return text;
  }
};

[[nodiscard]] auto looks_like_integer(std::string_view text) -> bool {
  if (text.empty()) return false;
  size_t pos = text[0] == '-' ? 1 : 0;
  if (pos == text.size()) return false;
  for (; pos < text.size(); ++pos) {
    if (!std::isdigit(static_cast<unsigned char>(text[pos]))) return false;
  }
  return true;
}

[[nodiscard]] auto parse_integer(std::string_view text)
    -> std::optional<winux::bignum::Int> {
  if (!looks_like_integer(text)) return std::nullopt;
  return winux::bignum::Int::from_decimal(text);
}

auto coerce_integer(const Value& value) -> winux::bignum::Int {
  if (value.kind == Value::Kind::Integer) return value.integer;
  auto parsed = parse_integer(value.text);
  if (!parsed) {
    throw ExprError{winux::i18n::translate(
        "command.expr.error.non_integer_argument", "non-integer argument")};
  }
  return *parsed;
}

// Saturating conversion for keyword operands that still need a machine
// integer (substr POS/LENGTH).
auto coerce_i64(const Value& value) -> std::optional<long long> {
  if (value.kind == Value::Kind::Integer) return value.integer.to_i64();
  auto parsed = parse_integer(value.text);
  if (!parsed) return std::nullopt;
  return parsed->to_i64();
}

[[nodiscard]] auto is_null(const Value& value) -> bool {
  if (value.kind == Value::Kind::Integer) return value.integer.is_zero();

  std::string_view s = value.text;
  return s.empty();
}

[[nodiscard]] auto trim_integer_zeros(std::string_view digits)
    -> std::string_view {
  size_t first_non_zero = 0;
  while (first_non_zero < digits.size() && digits[first_non_zero] == '0') {
    ++first_non_zero;
  }
  if (first_non_zero == digits.size()) return std::string_view{"0", 1};
  return digits.substr(first_non_zero);
}

[[nodiscard]] auto compare_integer_text(std::string_view lhs,
                                        std::string_view rhs) -> int {
  bool lhs_neg = !lhs.empty() && lhs[0] == '-';
  bool rhs_neg = !rhs.empty() && rhs[0] == '-';
  lhs.remove_prefix(lhs_neg ? 1 : 0);
  rhs.remove_prefix(rhs_neg ? 1 : 0);
  lhs = trim_integer_zeros(lhs);
  rhs = trim_integer_zeros(rhs);
  if (lhs == "0") lhs_neg = false;
  if (rhs == "0") rhs_neg = false;
  if (lhs_neg != rhs_neg) return lhs_neg ? -1 : 1;

  int magnitude = 0;
  if (lhs.size() != rhs.size()) {
    magnitude = lhs.size() < rhs.size() ? -1 : 1;
  } else {
    int cmp = lhs.compare(rhs);
    magnitude = cmp < 0 ? -1 : (cmp > 0 ? 1 : 0);
  }
  return lhs_neg ? -magnitude : magnitude;
}

[[nodiscard]] auto utf8_logical_length(std::string_view text) -> size_t {
  if (text.empty()) return 0;
  auto wide = utf8_to_wstring(text);
  return wide.empty() ? text.size() : wide.size();
}

[[nodiscard]] auto utf8_logical_substr(std::string_view text, long long pos,
                                       long long length) -> std::string {
  if (pos <= 0 || length <= 0) return {};

  auto wide = utf8_to_wstring(text);
  if (wide.empty() && !text.empty()) {
    size_t begin = static_cast<size_t>(pos - 1);
    if (begin >= text.size()) return {};
    size_t count = std::min(static_cast<size_t>(length), text.size() - begin);
    return std::string(text.substr(begin, count));
  }

  size_t begin = static_cast<size_t>(pos - 1);
  if (begin >= wide.size()) return {};
  size_t count = std::min(static_cast<size_t>(length), wide.size() - begin);
  return wstring_to_utf8(std::wstring_view(wide).substr(begin, count));
}

[[nodiscard]] auto utf8_index_any(std::string_view text, std::string_view chars)
    -> long long {
  if (text.empty() || chars.empty()) return 0;

  auto wide_text = utf8_to_wstring(text);
  auto wide_chars = utf8_to_wstring(chars);
  if (wide_text.empty() || wide_chars.empty()) {
    auto pos = text.find_first_of(chars);
    return pos == std::string_view::npos ? 0 : static_cast<long long>(pos + 1);
  }

  auto pos = wide_text.find_first_of(wide_chars);
  return pos == std::wstring::npos ? 0 : static_cast<long long>(pos + 1);
}

// [GNU] expr uses arbitrary-precision integers (GMP): + - * cannot
// overflow, and / % truncate toward zero.  Division by zero is the only
// arithmetic error.
[[noreturn]] auto throw_division_by_zero() -> void {
  throw ExprError{winux::i18n::translate("command.expr.error.division_by_zero",
                                         "division by zero")};
}

auto do_colon(const Value& lhs, const Value& rhs) -> Value {
  std::string text = lhs.as_string();
  std::string pattern = rhs.as_string();
  auto compiled =
      portable_regex::compile(portable_regex::Syntax::Basic, pattern);
  if (!compiled) {
    throw ExprError{compiled.error.empty() ? "invalid regular expression"
                                           : compiled.error};
  }

  auto match = compiled.pattern.find_first(text, 0);
  const bool has_capture = compiled.pattern.capture_count() > 0;
  if (!match || match->begin != 0) {
    return has_capture ? Value::make_string("") : Value::make_integer(0);
  }

  if (has_capture) {
    auto group = match->group(1);
    if (!group) return Value::make_string("");
    return Value::make_string(
        std::string_view(text).substr(group->begin, group->end - group->begin));
  }

  return Value::make_integer(static_cast<long long>(
      utf8_logical_length(std::string_view(text).substr(0, match->end))));
}

class Parser {
 public:
  explicit Parser(std::span<const std::string_view> args) : args_(args) {}

  auto parse() -> Value {
    if (args_.empty()) throw ExprError{"missing operand"};
    Value value = eval_or(true);
    if (!at_end()) {
      throw ExprError{"syntax error: unexpected argument " +
                      std::string(args_[pos_])};
    }
    return value;
  }

 private:
  std::span<const std::string_view> args_;
  size_t pos_ = 0;

  [[nodiscard]] auto at_end() const -> bool { return pos_ >= args_.size(); }

  [[nodiscard]] auto previous_token() const -> std::string {
    if (pos_ == 0 || pos_ - 1 >= args_.size()) return {};
    return std::string(args_[pos_ - 1]);
  }

  auto require_more_args() -> void {
    if (!at_end()) return;
    auto previous = previous_token();
    if (previous.empty()) {
      throw ExprError{"missing operand"};
    }
    throw ExprError{"syntax error: missing argument after " + previous};
  }

  auto next(std::string_view token) -> bool {
    if (at_end() || args_[pos_] != token) return false;
    ++pos_;
    return true;
  }

  auto eval_or(bool evaluate) -> Value {
    Value lhs = eval_and(evaluate);
    while (next("|")) {
      Value rhs = eval_and(evaluate && is_null(lhs));
      if (!evaluate) continue;
      if (is_null(lhs)) {
        lhs = std::move(rhs);
        if (is_null(lhs)) lhs = Value::make_integer(0);
      } else {
        lhs = Value::make_integer(1);
      }
    }
    return lhs;
  }

  auto eval_and(bool evaluate) -> Value {
    Value lhs = eval_compare(evaluate);
    while (next("&")) {
      Value rhs = eval_compare(evaluate && !is_null(lhs));
      if (!evaluate) continue;
      if (is_null(lhs) || is_null(rhs)) {
        lhs = Value::make_integer(0);
      }
    }
    return lhs;
  }

  auto eval_compare(bool evaluate) -> Value {
    Value lhs = eval_add(evaluate);
    while (true) {
      enum class Cmp { None, Lt, Le, Eq, Ne, Ge, Gt };
      Cmp op = Cmp::None;
      if (next("<")) {
        op = Cmp::Lt;
      } else if (next("<=")) {
        op = Cmp::Le;
      } else if (next("=") || next("==")) {
        op = Cmp::Eq;
      } else if (next("!=")) {
        op = Cmp::Ne;
      } else if (next(">=")) {
        op = Cmp::Ge;
      } else if (next(">")) {
        op = Cmp::Gt;
      } else {
        return lhs;
      }

      Value rhs = eval_add(evaluate);
      if (!evaluate) continue;

      std::string left = lhs.as_string();
      std::string right = rhs.as_string();
      int cmp = looks_like_integer(left) && looks_like_integer(right)
                    ? compare_integer_text(left, right)
                    : left.compare(right);
      bool result = false;
      switch (op) {
        case Cmp::Lt:
          result = cmp < 0;
          break;
        case Cmp::Le:
          result = cmp <= 0;
          break;
        case Cmp::Eq:
          result = cmp == 0;
          break;
        case Cmp::Ne:
          result = cmp != 0;
          break;
        case Cmp::Ge:
          result = cmp >= 0;
          break;
        case Cmp::Gt:
          result = cmp > 0;
          break;
        case Cmp::None:
          break;
      }
      lhs = Value::make_integer(result ? 1 : 0);
    }
  }

  auto eval_add(bool evaluate) -> Value {
    Value lhs = eval_mul(evaluate);
    while (true) {
      enum class Add { None, Plus, Minus };
      Add op = Add::None;
      if (next("+")) {
        op = Add::Plus;
      } else if (next("-")) {
        op = Add::Minus;
      } else {
        return lhs;
      }

      Value rhs = eval_mul(evaluate);
      if (!evaluate) continue;
      auto left = coerce_integer(lhs);
      auto right = coerce_integer(rhs);
      lhs = Value::make_integer(op == Add::Plus ? left + right : left - right);
    }
  }

  auto eval_mul(bool evaluate) -> Value {
    Value lhs = eval_colon(evaluate);
    while (true) {
      enum class Mul { None, Multiply, Divide, Mod };
      Mul op = Mul::None;
      if (next("*")) {
        op = Mul::Multiply;
      } else if (next("/")) {
        op = Mul::Divide;
      } else if (next("%")) {
        op = Mul::Mod;
      } else {
        return lhs;
      }

      Value rhs = eval_colon(evaluate);
      if (!evaluate) continue;
      auto left = coerce_integer(lhs);
      auto right = coerce_integer(rhs);
      switch (op) {
        case Mul::Multiply:
          lhs = Value::make_integer(left * right);
          break;
        case Mul::Divide:
          if (right.is_zero()) throw_division_by_zero();
          lhs = Value::make_integer(left / right);
          break;
        case Mul::Mod:
          if (right.is_zero()) throw_division_by_zero();
          lhs = Value::make_integer(left % right);
          break;
        case Mul::None:
          break;
      }
    }
  }

  auto eval_colon(bool evaluate) -> Value {
    Value lhs = eval_keyword(evaluate);
    while (next(":")) {
      Value rhs = eval_keyword(evaluate);
      if (evaluate) lhs = do_colon(lhs, rhs);
    }
    return lhs;
  }

  auto eval_keyword(bool evaluate) -> Value {
    if (next("+")) {
      require_more_args();
      std::string token(args_[pos_++]);
      auto parsed = parse_integer(token);
      if (parsed) return Value::make_integer(*parsed);
      return Value::make_string(token);
    }

    if (next("length")) {
      Value value = eval_keyword(evaluate);
      return Value::make_integer(
          static_cast<long long>(utf8_logical_length(value.as_string())));
    }

    if (next("match")) {
      Value lhs = eval_keyword(evaluate);
      Value rhs = eval_keyword(evaluate);
      return evaluate ? do_colon(lhs, rhs) : lhs;
    }

    if (next("index")) {
      Value lhs = eval_keyword(evaluate);
      Value rhs = eval_keyword(evaluate);
      return Value::make_integer(
          utf8_index_any(lhs.as_string(), rhs.as_string()));
    }

    if (next("substr")) {
      Value source = eval_keyword(evaluate);
      Value pos = eval_keyword(evaluate);
      Value len = eval_keyword(evaluate);
      auto pos_int = coerce_i64(pos);
      auto len_int = coerce_i64(len);
      if (!pos_int || !len_int) return Value::make_string("");
      return Value::make_string(
          utf8_logical_substr(source.as_string(), *pos_int, *len_int));
    }

    return eval_primary(evaluate);
  }

  auto eval_primary(bool evaluate) -> Value {
    (void)evaluate;
    require_more_args();
    if (next("(")) {
      Value value = eval_or(evaluate);
      if (at_end()) {
        throw ExprError{"syntax error: expecting ')' after " +
                        previous_token()};
      }
      if (!next(")")) {
        throw ExprError{"syntax error: expecting ')' instead of " +
                        std::string(args_[pos_])};
      }
      return value;
    }
    if (next(")")) throw ExprError{"syntax error: unexpected ')'"};
    std::string token(args_[pos_++]);
    auto parsed = parse_integer(token);
    if (parsed) return Value::make_integer(*parsed);
    return Value::make_string(token);
  }
};

[[nodiscard]] auto display_value(const Value& value) -> std::string {
  return value.as_string();
}

}  // namespace

REGISTER_COMMAND(
    expr_cmd,
    /* name */
    "expr",

    /* synopsis */
    "expr EXPRESSION",
    "Evaluate expressions.\n"
    "\n"
    "Print the value of EXPRESSION to standard output.\n"
    "\n"
    "Supported GNU-style operators and keywords:\n"
    "  ARG1 | ARG2, ARG1 & ARG2\n"
    "  ARG1 < ARG2, <=, =, ==, !=, >=, >\n"
    "  ARG1 + ARG2, ARG1 - ARG2, ARG1 * ARG2, ARG1 / ARG2, ARG1 % ARG2\n"
    "  STRING : REGEXP, match STRING REGEXP\n"
    "  substr STRING POS LENGTH, index STRING CHARS, length STRING\n"
    "  + TOKEN, ( EXPRESSION )\n"
    "\n"
    "Regex matching uses WinuxCmd's POSIX basic regex module and is anchored "
    "at "
    "the start of STRING. Numeric arithmetic uses arbitrary-precision "
    "integers like GNU expr.",
    "  expr 2 + 3 \\* 4\n"
    "  expr \\( 2 + 3 \\) \\* 4\n"
    "  expr abc123 : '[a-z]*\\\\([0-9]*\\\\)'\n"
    "  expr length hello",

    /* see also */
    "test(1)", "WinuxCmd", "Copyright © 2026 WinuxCmd", EXPR_OPTIONS) {
  try {
    Parser parser(ctx.positionals);
    Value value = parser.parse();
    safePrintLn(display_value(value));
    return is_null(value) ? 1 : 0;
  } catch (const ExprError& error) {
    safeErrorPrintLn("expr: " + error.message);
    return 2;
  } catch (const std::exception& error) {
    safeErrorPrintLn(std::string("expr: ") + error.what());
    return 3;
  }
}
