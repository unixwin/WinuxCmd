// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(logname, logname_basic) {
  Pipeline p;
  p.add(L"logname.exe", {});

  TEST_LOG_CMD_LIST("logname.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("logname output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(logname, logname_version_succeeds) {
  Pipeline p;
  p.add(L"logname.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("logname (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

// [#1215] logname takes no operands: GNU coreutils reports
// "logname: extra operand 'extra'" and exits 1 instead of printing the answer.
TEST(logname, logname_extra_operand_rejected_issue1215) {
  Pipeline p;
  p.add(L"logname.exe", {L"extra"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  EXPECT_EXIT_CODE(r, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("extra operand 'extra'") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.find("Try 'logname --help'") != std::string::npos);
}
