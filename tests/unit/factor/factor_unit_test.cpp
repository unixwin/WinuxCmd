/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(factor, factor_basic) {
  Pipeline p;
  p.add(L"factor.exe", {L"12"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "12: 2 2 3\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(factor, factor_version_succeeds) {
  Pipeline p;
  p.add(L"factor.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("factor (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}
