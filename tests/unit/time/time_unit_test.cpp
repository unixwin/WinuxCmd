// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(time_cmd, time_preserves_exit_code) {
  Pipeline p;
  p.add(L"time.exe", {L"cmd.exe", L"/c", L"exit", L"7"});

  TEST_LOG_CMD_LIST("time.exe", L"cmd.exe", L"/c", L"exit", L"7");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("time.exe output", r.stderr_text);

  EXPECT_EQ(r.exit_code, 7);
  EXPECT_FALSE(r.stderr_text.empty());
  EXPECT_NE(r.stderr_text.find("real"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("user"), std::string::npos);
  EXPECT_NE(r.stderr_text.find("sys"), std::string::npos);
}

TEST(time_cmd, time_posix_format) {
  Pipeline p;
  p.add(L"time.exe", {L"-p", L"cmd.exe", L"/c", L"exit", L"0"});

  TEST_LOG_CMD_LIST("time.exe", L"-p", L"cmd.exe", L"/c", L"exit", L"0");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("time.exe -p output", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stderr_text.empty());
  EXPECT_NE(r.stderr_text.find("real "), std::string::npos);
  EXPECT_NE(r.stderr_text.find("user "), std::string::npos);
  EXPECT_NE(r.stderr_text.find("sys "), std::string::npos);
}
