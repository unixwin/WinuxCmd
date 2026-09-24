// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(cal, cal_current_month) {
  Pipeline p;
  p.add(L"cal.exe", {});

  TEST_LOG_CMD_LIST("cal.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cal output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("Su Mo Tu We Th Fr Sa") != std::string::npos);
}

TEST(cal, cal_month_year) {
  Pipeline p;
  p.add(L"cal.exe", {L"3", L"2024"});

  TEST_LOG_CMD_LIST("cal.exe", L"3", L"2024");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cal output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("March") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("2024") != std::string::npos);
}

TEST(cal, cal_month_year_matches_util_linux_fixed_width) {
  Pipeline p;
  p.add(L"cal.exe", {L"7", L"2026"});

  TEST_LOG_CMD_LIST("cal.exe", L"7", L"2026");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cal output", r.stdout_text);

  std::string expected;
  expected += "      July 2026     \n";
  expected += "Su Mo Tu We Th Fr Sa\n";
  expected += "          1  2  3  4\n";
  expected += " 5  6  7  8  9 10 11\n";
  expected += "12 13 14 15 16 17 18\n";
  expected += "19 20 21 22 23 24 25\n";
  expected += "26 27 28 29 30 31   \n";
  expected += std::string(20, char(32)) + "\n";

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(cal, cal_single_year_outputs_year_calendar) {
  Pipeline p;
  p.add(L"cal.exe", {L"2026"});

  TEST_LOG_CMD_LIST("cal.exe", L"2026");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cal output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("January 2026") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("December 2026") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("July 2026") != std::string::npos);
}
TEST(cal, cal_monday_first_month) {
  Pipeline p;
  p.add(L"cal.exe", {L"-m", L"3", L"2024"});

  TEST_LOG_CMD_LIST("cal.exe", L"-m", L"3", L"2024");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("cal output", r.stdout_text);

  std::string expected;
  expected += "     March 2024     \n";
  expected += "Mo Tu We Th Fr Sa Su\n";
  expected += "             1  2  3\n";
  expected += " 4  5  6  7  8  9 10\n";
  expected += "11 12 13 14 15 16 17\n";
  expected += "18 19 20 21 22 23 24\n";
  expected += "25 26 27 28 29 30 31\n";
  expected += "                    \n";

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}
