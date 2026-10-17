// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(false_cmd, false_basic) {
  Pipeline p;
  p.add(L"false.exe", {});

  TEST_LOG_CMD_LIST("false.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
}

TEST(false_cmd, false_version_succeeds) {
  Pipeline p;
  p.add(L"false.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("false (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

// [GNU] false ignores every operand: `false -x -q --foo` exits with the
// plain failure status 1 and no diagnostics (WinuxCmd #1196).
TEST(false_cmd, false_ignores_nonempty_operand) {
  Pipeline p;
  p.add(L"false.exe", {L"-x"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("false -x stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(false_cmd, false_ignores_every_operand_silently) {
  Pipeline p;
  p.add(L"false.exe", {L"-x", L"--foo", L"--", L"operand"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(false_cmd, false_help_only_as_sole_argument) {
  // [GNU] `false -x --help` is a plain failure with no output; --help is
  // honored only when it is the sole argument.
  Pipeline p;
  p.add(L"false.exe", {L"-x", L"--help"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.empty());
}
