// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(uptime, uptime_basic) {
  Pipeline p;
  p.add(L"uptime.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should contain time and load information
  EXPECT_TRUE(r.stdout_text.find(":") != std::string::npos);
}
