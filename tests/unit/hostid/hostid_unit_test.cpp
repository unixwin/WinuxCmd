// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(hostid, hostid_basic) {
  Pipeline p;
  p.add(L"hostid.exe", {});

  TEST_LOG_CMD_LIST("hostid.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("hostid output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_EQ(r.stdout_text.length(), 9);  // 8 hex chars + newline
}

TEST(hostid, hostid_version_succeeds) {
  Pipeline p;
  p.add(L"hostid.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("hostid (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}
