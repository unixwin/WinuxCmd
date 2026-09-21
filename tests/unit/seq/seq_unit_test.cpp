// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(seq, seq_single_arg) {
  Pipeline p;
  p.add(L"seq.exe", {L"5"});

  TEST_LOG_CMD_LIST("seq.exe", L"5");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n2\n3\n4\n5\n");
}

TEST(seq, seq_two_args) {
  Pipeline p;
  p.add(L"seq.exe", {L"1", L"5"});

  TEST_LOG_CMD_LIST("seq.exe", L"1", L"5");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq two args output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n2\n3\n4\n5\n");
}

TEST(seq, seq_three_args) {
  Pipeline p;
  p.add(L"seq.exe", {L"1", L"2", L"10"});

  TEST_LOG_CMD_LIST("seq.exe", L"1", L"2", L"10");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq three args output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n3\n5\n7\n9\n");
}

TEST(seq, seq_separator) {
  Pipeline p;
  p.add(L"seq.exe", {L"-s", L" ", L"1", L"5"});

  TEST_LOG_CMD_LIST("seq.exe", L"-s", L" ", L"1", L"5");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq separator output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1 2 3 4 5\n");
}

TEST(seq, seq_long_separator) {
  Pipeline p;
  p.add(L"seq.exe", {L"--separator", L",", L"1", L"4"});

  TEST_LOG_CMD_LIST("seq.exe", L"--separator", L",", L"1", L"4");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq long separator output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1,2,3,4\n");
}

TEST(seq, seq_long_terminator_replaces_final_newline) {
  Pipeline p;
  p.add(L"seq.exe", {L"--terminator=,", L"3"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n2\n3,");
}

TEST(seq, seq_short_terminator_replaces_only_final_separator) {
  Pipeline p;
  p.add(L"seq.exe", {L"-t", L" END ", L"1", L"2", L"5"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1\n3\n5 END ");
}

TEST(seq, seq_rejects_invalid_number_without_throwing) {
  Pipeline p;
  p.add(L"seq.exe", {L"not-a-number"});

  TEST_LOG_CMD_LIST("seq.exe", L"not-a-number");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq invalid number stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("invalid floating point argument") !=
              std::string::npos);
}

TEST(seq, seq_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"seq.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "seq: missing operand\n"
                 "Try 'seq --help' for more information.\n");
}

TEST(seq, seq_rejects_extra_operand_with_help_hint) {
  Pipeline p;
  p.add(L"seq.exe", {L"1", L"2", L"3", L"4"});

  TEST_LOG_CMD_LIST("seq.exe", L"1", L"2", L"3", L"4");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "seq: extra operand '4'\n"
                 "Try 'seq --help' for more information.\n");
}

TEST(seq, seq_rejects_zero_increment) {
  Pipeline p;
  p.add(L"seq.exe", {L"1", L"0", L"3"});

  TEST_LOG_CMD_LIST("seq.exe", L"1", L"0", L"3");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq zero increment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "seq: invalid Zero increment value: '0'\n"
                 "Try 'seq --help' for more information.\n");
}

TEST(seq, seq_negative_decreasing_range) {
  Pipeline p;
  p.add(L"seq.exe", {L"3", L"-2", L"-3"});

  TEST_LOG_CMD_LIST("seq.exe", L"3", L"-2", L"-3");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq negative decreasing output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "3\n1\n-1\n-3\n");
}

TEST(seq, seq_wrong_direction_outputs_nothing) {
  Pipeline p;
  p.add(L"seq.exe", {L"5", L"1"});

  TEST_LOG_CMD_LIST("seq.exe", L"5", L"1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq wrong direction output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
}

TEST(seq, seq_equal_width_pads_with_zeroes) {
  Pipeline p;
  p.add(L"seq.exe", {L"-w", L"8", L"10"});

  TEST_LOG_CMD_LIST("seq.exe", L"-w", L"8", L"10");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq equal width output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "08\n09\n10\n");
}

TEST(seq, seq_equal_width_pads_after_negative_sign) {
  Pipeline p;
  p.add(L"seq.exe", {L"-w", L"-10", L"1", L"-8"});

  TEST_LOG_CMD_LIST("seq.exe", L"-w", L"-10", L"1", L"-8");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq negative equal width output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "-10\n-09\n-08\n");
}

TEST(seq, seq_default_fixed_decimal_precision) {
  Pipeline p;
  p.add(L"seq.exe", {L"1", L"0.5", L"2"});

  TEST_LOG_CMD_LIST("seq.exe", L"1", L"0.5", L"2");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq fixed decimal output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.0\n1.5\n2.0\n");
}

TEST(seq, seq_format_accepts_printf_float_conversion) {
  Pipeline p;
  p.add(L"seq.exe", {L"-f", L"[%05.1f]", L"1", L"1", L"3"});

  TEST_LOG_CMD_LIST("seq.exe", L"-f", L"[%05.1f]", L"1", L"1", L"3");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq format output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "[001.0]\n[002.0]\n[003.0]\n");
}

TEST(seq, seq_format_allows_literal_percent) {
  Pipeline p;
  p.add(L"seq.exe", {L"--format", L"v=%g%%", L"1", L"2"});

  TEST_LOG_CMD_LIST("seq.exe", L"--format", L"v=%g%%", L"1", L"2");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq format percent output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "v=1%\nv=2%\n");
}

TEST(seq, seq_format_rejects_non_float_conversion) {
  Pipeline p;
  p.add(L"seq.exe", {L"-f", L"%d", L"1", L"3"});

  TEST_LOG_CMD_LIST("seq.exe", L"-f", L"%d", L"1", L"3");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq invalid format stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("format") != std::string::npos);
}

TEST(seq, seq_rejects_format_with_equal_width) {
  Pipeline p;
  p.add(L"seq.exe", {L"-w", L"-f", L"%f", L"1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "seq: format string may not be specified when printing equal width "
      "strings\nTry 'seq --help' for more information.\n");
}

// [GNU] scientific-notation operands keep a fixed-point default format:
// precision = mantissa decimals - exponent (uutils #14153 family).
TEST(seq, seq_scientific_notation_precision) {
  Pipeline p;
  p.add(L"seq.exe", {L"8.0e-1", L"1.0e0"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "0.80\n");
}

TEST(seq, seq_scientific_notation_negative_exponent) {
  Pipeline p;
  p.add(L"seq.exe", {L"1e-20", L"1e-20", L"1e-20"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "0.00000000000000000001\n");
}

TEST(seq, seq_scientific_notation_positive_exponent) {
  Pipeline p;
  p.add(L"seq.exe", {L"1.0e+5", L"2", L"1.0e+5"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "100000\n");
}

TEST(seq, seq_equal_width_scientific_notation) {
  Pipeline p;
  p.add(L"seq.exe", {L"-w", L"8.0e-1", L"0.1", L"1.0e0"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "0.80\n0.90\n1.00\n");
}

// [GNU] the last value must be included when it is reachable in exact
// arithmetic even though repeated addition accumulates rounding error
// (issue: 1 + 0.1*k must still print 1.3).
TEST(seq, seq_fractional_increment_includes_last) {
  Pipeline p;
  p.add(L"seq.exe", {L"1", L"0.1", L"1.3"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.0\n1.1\n1.2\n1.3\n");
}

// [GNU] the default precision is derived from FIRST and INCREMENT only,
// never from LAST.
TEST(seq, seq_precision_ignores_last_operand) {
  Pipeline p;
  p.add(L"seq.exe", {L"0.5", L"1.55"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "0.5\n1.5\n");
}

TEST(seq, seq_two_digit_increment_precision) {
  Pipeline p;
  p.add(L"seq.exe", {L"1", L"0.15", L"1.4"});

  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "1.00\n1.15\n1.30\n");
}
