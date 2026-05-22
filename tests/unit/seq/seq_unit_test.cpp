/*
 *  Copyright © 2026 [caomengxuan666]
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
 *  - File: seq_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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

TEST(seq, seq_rejects_zero_increment) {
  Pipeline p;
  p.add(L"seq.exe", {L"1", L"0", L"3"});

  TEST_LOG_CMD_LIST("seq.exe", L"1", L"0", L"3");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("seq zero increment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("zero increment") != std::string::npos);
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
