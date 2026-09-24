// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(reset, reset_basic) {
  Pipeline p;
  p.add(L"reset.exe", {});

  TEST_LOG_CMD_LIST("reset.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("reset output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(reset, reset_quiet) {
  Pipeline p;
  p.add(L"reset.exe", {L"-q"});

  TEST_LOG_CMD_LIST("reset.exe", L"-q");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("reset output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(reset, reset_initialize) {
  Pipeline p;
  p.add(L"reset.exe", {L"-I"});

  TEST_LOG_CMD_LIST("reset.exe", L"-I");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("reset output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
