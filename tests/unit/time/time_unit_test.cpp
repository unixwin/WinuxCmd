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
// #1375: time's own option parsing stops at COMMAND; `time ls -l`
// used to fail with "time: invalid option -- 'l'".
TEST(time_cmd, time_wrapped_command_options_pass_through_issue1375) {
  TempDir tmp;
  tmp.write("marker.txt", "x\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"time.exe", {L"ls.exe", L"-l"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("marker.txt") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("real") != std::string::npos);
}

