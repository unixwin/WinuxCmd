// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
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
  EXPECT_EQ_TEXT(r.stderr_text,
                 "sleep: missing operand\n"
                 "Try 'sleep --help' for more information.\n");
}

TEST(sleep, sleep_invalid_interval_reports_gnu_style_error) {
  Pipeline p;
  p.add(L"sleep.exe", {L"abc"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "sleep: invalid time interval 'abc'\n"
                 "Try 'sleep --help' for more information.\n");
}

TEST(sleep, sleep_negative_interval_is_rejected) {
  Pipeline p;
  p.add(L"sleep.exe", {L"--", L"-1"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
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
  EXPECT_EQ_TEXT(r.stderr_text,
                 "sleep: invalid time interval '0 '\n"
                 "Try 'sleep --help' for more information.\n");
}

// [#1015] strtod accepts "nan" but GNU rejects it as a time interval.
TEST(sleep, sleep_nan_interval_is_rejected) {
  Pipeline p;
  p.add(L"sleep.exe", {L"nan"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "sleep: invalid time interval 'nan'\n"
                 "Try 'sleep --help' for more information.\n");
}

// [#1015] GNU 9.x accepts C99 hexadecimal floats: "0x10" is 16 seconds.
// "0x0" verifies acceptance without actually sleeping.
TEST(sleep, sleep_hex_interval_is_accepted) {
  Pipeline p;
  p.add(L"sleep.exe", {L"0x0"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

// [#1015] GNU only accepts lowercase s/m/h/d suffixes.
TEST(sleep, sleep_uppercase_suffix_is_rejected) {
  Pipeline p;
  p.add(L"sleep.exe", {L"0M"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stderr_text,
                 "sleep: invalid time interval '0M'\n"
                 "Try 'sleep --help' for more information.\n");
}

TEST(sleep, sleep_reports_all_invalid_intervals_before_help_hint) {
  Pipeline p;
  p.add(L"sleep.exe", {L"abc", L"100000.0", L"1years", L" "});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "sleep: invalid time interval 'abc'\n"
                 "sleep: invalid time interval '1years'\n"
                 "sleep: invalid time interval ' '\n"
                 "Try 'sleep --help' for more information.\n");
}
