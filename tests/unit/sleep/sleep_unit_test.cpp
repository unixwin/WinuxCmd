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
 *  - File: sleep_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include <chrono>
#include <thread>

#include "framework/winuxtest.h"

TEST(sleep, sleep_zero) {
  Pipeline p;
  p.add(L"sleep.exe", {L"0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sleep, sleep_short) {
  Pipeline p;
  p.add(L"sleep.exe", {L"0.1"});

  auto start = std::chrono::steady_clock::now();
  auto r = p.run();
  auto end = std::chrono::steady_clock::now();

  EXPECT_EQ(r.exit_code, 0);
  // Should sleep for at least 0.1 seconds
  auto duration =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  EXPECT_TRUE(duration.count() >= 90);   // Allow some tolerance
  EXPECT_TRUE(duration.count() <= 200);  // But not too long
}

TEST(sleep, sleep_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"sleep.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "sleep: missing operand\n"
      "Try 'sleep --help' for more information.\n");
}

TEST(sleep, sleep_invalid_interval_reports_gnu_style_error) {
  Pipeline p;
  p.add(L"sleep.exe", {L"abc"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "sleep: invalid time interval 'abc'\n"
      "Try 'sleep --help' for more information.\n");
}

TEST(sleep, sleep_negative_interval_is_rejected) {
  Pipeline p;
  p.add(L"sleep.exe", {L"--", L"-1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "sleep: invalid time interval '-1'\n"
      "Try 'sleep --help' for more information.\n");
}

TEST(sleep, sleep_leading_whitespace_is_accepted) {
  Pipeline p;
  p.add(L"sleep.exe", {L" 0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(sleep, sleep_trailing_whitespace_is_rejected) {
  Pipeline p;
  p.add(L"sleep.exe", {L"0 "});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "sleep: invalid time interval '0 '\n"
      "Try 'sleep --help' for more information.\n");
}

TEST(sleep, sleep_reports_all_invalid_intervals_before_help_hint) {
  Pipeline p;
  p.add(L"sleep.exe", {L"abc", L"100000.0", L"1years", L" "});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "sleep: invalid time interval 'abc'\n"
      "sleep: invalid time interval '1years'\n"
      "sleep: invalid time interval ' '\n"
      "Try 'sleep --help' for more information.\n");
}
