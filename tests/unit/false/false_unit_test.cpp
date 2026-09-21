// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(false_cmd, false_basic) {
  Pipeline p;
  p.add(L"false.exe", {});

  TEST_LOG_CMD_LIST("false.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
}

TEST(false_cmd, false_version_succeeds) {
  Pipeline p;
  p.add(L"false.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("false (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}
