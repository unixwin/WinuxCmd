// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(infocmp, infocmp_basic) {
  Pipeline p;
  p.add(L"infocmp.exe", {});

  TEST_LOG_CMD_LIST("infocmp.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("infocmp output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("Windows ANSI Terminal") != std::string::npos);
}
