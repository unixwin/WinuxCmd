// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(whoami, whoami_basic) {
  Pipeline p;
  p.add(L"whoami.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Remove trailing newline
  std::string username = r.stdout_text;
  if (!username.empty() && username.back() == '\n') {
    username.pop_back();
  }
  EXPECT_FALSE(username.empty());
}

TEST(whoami, whoami_version_succeeds) {
  Pipeline p;
  p.add(L"whoami.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("whoami (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}
