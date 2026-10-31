// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(reset, reset_basic) {
  Pipeline p;
  p.add(L"reset.exe", {});

  TEST_LOG_CMD_LIST("reset.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("reset output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  // [#1462] GNU reset aims its reset strings at stderr (reset_start's
  // stream), leaving stdout clean; the byte used to go to stdout.
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.find("\x1b" "c") != std::string::npos);
}

TEST(reset, reset_quiet) {
  // [#1462] GNU tset -q is "display term only": it prints the terminal
  // type on stdout and changes nothing (no reset bytes at all).
  Pipeline p;
  p.add(L"reset.exe", {L"-q", L"vt100"});

  TEST_LOG_CMD_LIST("reset.exe", L"-q", L"vt100");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("reset output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "vt100\n");
}

TEST(reset, reset_report_type_issue1462) {
  // [GNU] tset -r reports "Terminal type is %s." on stderr, with or
  // without an operand.
  Pipeline p;
  p.add(L"reset.exe", {L"-r", L"vt100"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("Terminal type is vt100.") !=
              std::string::npos);
}

TEST(reset, reset_shell_commands_issue1462) {
  // [GNU] tset -s emits shell commands for the resolved terminal type
  // (operand here), not a hardcoded TERM=xterm.
  Pipeline p;
  p.set_env(L"SHELL", L"/bin/sh");
  p.add(L"reset.exe", {L"-s", L"vt100"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "TERM=vt100;\n");
}

TEST(reset, reset_initialize) {
  Pipeline p;
  p.add(L"reset.exe", {L"-I"});

  TEST_LOG_CMD_LIST("reset.exe", L"-I");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("reset output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
