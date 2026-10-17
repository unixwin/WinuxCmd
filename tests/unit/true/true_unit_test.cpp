// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(true_cmd, true_basic) {
  Pipeline p;
  p.add(L"true.exe", {});

  TEST_LOG_CMD_LIST("true.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(true_cmd, true_version_succeeds) {
  Pipeline p;
  p.add(L"true.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("true (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

// [GNU] coreutils truefalse.c ignores every operand: `true -x` exits 0
// with no diagnostics instead of a usage error (WinuxCmd #1196).
TEST(true_cmd, true_ignores_nonempty_operand) {
  Pipeline p;
  p.add(L"true.exe", {L"-x"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("true -x stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(true_cmd, true_ignores_every_operand_silently) {
  Pipeline p;
  p.add(L"true.exe", {L"-x", L"--foo", L"--", L"operand"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(true_cmd, true_help_only_as_sole_argument) {
  // [GNU] `true -x --help` is a plain success with no output; --help is
  // honored only when it is the sole argument.
  Pipeline p;
  p.add(L"true.exe", {L"-x", L"--help"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}
