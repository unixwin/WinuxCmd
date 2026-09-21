// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(groups, groups_basic) {
  Pipeline p;
  p.add(L"groups.exe", {});

  TEST_LOG_CMD_LIST("groups.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("groups output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(groups, groups_with_user) {
  Pipeline p;
  p.add(L"groups.exe", {L"testuser"});

  TEST_LOG_CMD_LIST("groups.exe", L"testuser");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("groups output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_CONTAINS(r.stderr_text, "user name could not be found");
}
