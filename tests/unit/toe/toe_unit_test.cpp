// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(toe, toe_list_terminals) {
  Pipeline p;
  p.add(L"toe.exe", {});

  TEST_LOG_CMD_LIST("toe.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("toe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("windows-ansi") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("vt100") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("xterm") != std::string::npos);
}
