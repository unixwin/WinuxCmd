// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(true_cmd, true_basic) {
  Pipeline p;
  p.add(L"true.exe", {});

  TEST_LOG_CMD_LIST("true.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(true_cmd, true_version_succeeds) {
  Pipeline p;
  p.add(L"true.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("true (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}
