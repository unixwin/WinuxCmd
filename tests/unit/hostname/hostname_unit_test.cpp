// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(hostname, hostname_basic) {
  Pipeline p;
  p.add(L"hostname.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Remove trailing newline
  std::string hostname = r.stdout_text;
  if (!hostname.empty() && hostname.back() == '\n') {
    hostname.pop_back();
  }
  EXPECT_FALSE(hostname.empty());
}

TEST(hostname, hostname_long) {
  Pipeline p;
  p.add(L"hostname.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should return the full hostname
  EXPECT_FALSE(r.stdout_text.empty());
}
