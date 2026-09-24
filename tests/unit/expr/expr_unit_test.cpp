#include "framework/winuxtest.h"

TEST(expr, expr_arithmetic_precedence_matches_gnu_expr) {
  Pipeline p;
  p.add(L"expr.exe", {L"2", L"+", L"3", L"*", L"4"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "14\n");
}

TEST(expr, expr_parentheses_override_precedence) {
  Pipeline p;
  p.add(L"expr.exe", {L"(", L"2", L"+", L"3", L")", L"*", L"4"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "20\n");
}

TEST(expr, expr_zero_result_exits_one) {
  Pipeline p;
  p.add(L"expr.exe", {L"10", L"-", L"10"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "0\n");
}

TEST(expr, expr_string_or_and_return_gnu_shaped_values) {
  Pipeline or_expr;
  or_expr.add(L"expr.exe", {L"0", L"|", L"fallback"});
  auto or_result = or_expr.run();

  EXPECT_EXIT_CODE(or_result, 0);
  EXPECT_EQ_TEXT(or_result.stdout_text, "fallback\n");

  Pipeline and_expr;
  and_expr.add(L"expr.exe", {L"left", L"&", L"right"});
  auto and_result = and_expr.run();

  EXPECT_EXIT_CODE(and_result, 0);
  EXPECT_EQ_TEXT(and_result.stdout_text, "left\n");
}

TEST(expr, expr_short_circuits_unevaluated_boolean_branch) {
  Pipeline p;
  p.add(L"expr.exe", {L"1", L"|", L"1", L"/", L"0"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(expr, expr_comparisons_are_numeric_when_both_operands_are_integers) {
  Pipeline numeric;
  numeric.add(L"expr.exe", {L"10", L"<", L"2"});
  auto numeric_result = numeric.run();

  EXPECT_EXIT_CODE(numeric_result, 1);
  EXPECT_EQ_TEXT(numeric_result.stdout_text, "0\n");

  Pipeline lexical;
  lexical.add(L"expr.exe", {L"a10", L"<", L"a2"});
  auto lexical_result = lexical.run();

  EXPECT_EXIT_CODE(lexical_result, 0);
  EXPECT_EQ_TEXT(lexical_result.stdout_text, "1\n");
}

TEST(expr, expr_length_index_and_substr_keywords) {
  Pipeline length;
  length.add(L"expr.exe", {L"length", L"hello"});
  auto length_result = length.run();

  EXPECT_EXIT_CODE(length_result, 0);
  EXPECT_EQ_TEXT(length_result.stdout_text, "5\n");

  Pipeline index;
  index.add(L"expr.exe", {L"index", L"alphabet", L"px"});
  auto index_result = index.run();

  EXPECT_EXIT_CODE(index_result, 0);
  EXPECT_EQ_TEXT(index_result.stdout_text, "3\n");

  Pipeline substr;
  substr.add(L"expr.exe", {L"substr", L"alphabet", L"2", L"3"});
  auto substr_result = substr.run();

  EXPECT_EXIT_CODE(substr_result, 0);
  EXPECT_EQ_TEXT(substr_result.stdout_text, "lph\n");
}

TEST(expr, expr_colon_operator_uses_anchored_basic_regex) {
  Pipeline captured;
  captured.add(L"expr.exe", {L"abc123", L":", L"[a-z]*\\([0-9][0-9]*\\)"});
  auto captured_result = captured.run();

  EXPECT_EXIT_CODE(captured_result, 0);
  EXPECT_EQ_TEXT(captured_result.stdout_text, "123\n");

  Pipeline no_match;
  no_match.add(L"expr.exe", {L"abc123", L":", L"[0-9]*"});
  auto no_match_result = no_match.run();

  EXPECT_EXIT_CODE(no_match_result, 1);
  EXPECT_EQ_TEXT(no_match_result.stdout_text, "0\n");
}

TEST(expr, expr_match_keyword_returns_match_length_without_capture) {
  Pipeline p;
  p.add(L"expr.exe", {L"match", L"abc123", L"[a-z]*"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "3\n");
}

TEST(expr, expr_plus_quotes_keyword_or_operator_tokens) {
  Pipeline p;
  p.add(L"expr.exe", {L"+", L"length"});
  auto r = p.run();

  EXPECT_EXIT_CODE(r, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "length\n");
}

// [#1005] GNU expr uses arbitrary-precision integers (GMP); results beyond
// int64 must be exact rather than overflowing or erroring.
TEST(expr, expr_arbitrary_precision_arithmetic) {
  Pipeline add;
  add.add(L"expr.exe", {L"9223372036854775807", L"+", L"1"});
  auto add_result = add.run();
  EXPECT_EXIT_CODE(add_result, 0);
  EXPECT_EQ_TEXT(add_result.stdout_text, "9223372036854775808\n");

  Pipeline mul;
  mul.add(L"expr.exe",
          {L"99999999999999999999", L"*", L"99999999999999999999"});
  auto mul_result = mul.run();
  EXPECT_EXIT_CODE(mul_result, 0);
  EXPECT_EQ_TEXT(mul_result.stdout_text,
                 "9999999999999999999800000000000000000001\n");

  Pipeline sub;
  sub.add(L"expr.exe", {L"-9223372036854775808", L"-", L"1"});
  auto sub_result = sub.run();
  EXPECT_EXIT_CODE(sub_result, 0);
  EXPECT_EQ_TEXT(sub_result.stdout_text, "-9223372036854775809\n");
}

// [GNU] / and % truncate toward zero; the remainder takes the sign of the
// dividend.
TEST(expr, expr_division_and_remainder_truncate_toward_zero) {
  Pipeline div;
  div.add(L"expr.exe", {L"-7", L"/", L"2"});
  auto div_result = div.run();
  EXPECT_EXIT_CODE(div_result, 0);
  EXPECT_EQ_TEXT(div_result.stdout_text, "-3\n");

  Pipeline mod_neg;
  mod_neg.add(L"expr.exe", {L"-7", L"%", L"3"});
  auto mod_neg_result = mod_neg.run();
  EXPECT_EXIT_CODE(mod_neg_result, 0);
  EXPECT_EQ_TEXT(mod_neg_result.stdout_text, "-1\n");

  Pipeline mod_pos;
  mod_pos.add(L"expr.exe", {L"7", L"%", L"-3"});
  auto mod_pos_result = mod_pos.run();
  EXPECT_EXIT_CODE(mod_pos_result, 0);
  EXPECT_EQ_TEXT(mod_pos_result.stdout_text, "1\n");
}

TEST(expr, expr_invalid_expression_exits_two) {
  Pipeline division;
  division.add(L"expr.exe", {L"1", L"/", L"0"});
  auto division_result = division.run();

  EXPECT_EXIT_CODE(division_result, 2);
  EXPECT_CONTAINS(division_result.stderr_text, "division by zero");

  Pipeline missing;
  missing.add(L"expr.exe", {L"1", L"+"});
  auto missing_result = missing.run();

  EXPECT_EXIT_CODE(missing_result, 2);
  EXPECT_CONTAINS(missing_result.stderr_text, "missing argument");
}
