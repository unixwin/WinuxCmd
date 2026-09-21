// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(yes, yes_default) {
  Pipeline p;
  p.set_env(L"WINUXCMD_YES_REPEAT_LIMIT", L"5");
  p.add(L"yes.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "y\ny\ny\ny\ny\n");
}

TEST(yes, yes_custom_string) {
  Pipeline p;
  p.set_env(L"WINUXCMD_YES_REPEAT_LIMIT", L"3");
  p.add(L"yes.exe", {L"hello"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello\nhello\nhello\n");
}

TEST(yes, yes_joins_all_arguments_with_spaces) {
  Pipeline p;
  p.set_env(L"WINUXCMD_YES_REPEAT_LIMIT", L"3");
  p.add(L"yes.exe", {L"a", L"bar", L"c"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a bar c\na bar c\na bar c\n");
}

TEST(yes, yes_version_succeeds) {
  Pipeline p;
  p.add(L"yes.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("yes (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(yes, yes_double_dash_keeps_version_literal) {
  Pipeline p;
  p.set_env(L"WINUXCMD_YES_REPEAT_LIMIT", L"3");
  p.add(L"yes.exe", {L"--", L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "--version\n--version\n--version\n");
  EXPECT_TRUE(r.stderr_text.empty());
}
