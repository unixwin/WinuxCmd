// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(users, users_basic) {
  Pipeline p;
  p.add(L"users.exe", {});

  TEST_LOG_CMD_LIST("users.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("users output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

// [#1462] coreutils users.c takes at most one operand (the utmp FILE):
// a second operand is an "extra operand" error, exit 1.
TEST(users, users_extra_operand) {
  Pipeline p;
  p.add(L"users.exe", {L"a", L"b"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "extra operand");
}

// [#1462] A missing FILE operand is the GNU error, exit 1.
TEST(users, users_missing_file) {
  Pipeline p;
  p.add(L"users.exe", {L"no_such_utmp_file_xyz"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_CONTAINS(r.stderr_text, "No such file or directory");
}
