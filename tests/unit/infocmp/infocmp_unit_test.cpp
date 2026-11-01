// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

// [#1462] infocmp dumps the entry for the requested terminal (operand,
// else $TERM) in terminfo source form; the historical build ignored
// both and always printed one hardcoded entry with exit code 0.
TEST(infocmp, infocmp_dumps_requested_terminal_issue1462) {
  Pipeline p;
  p.add(L"infocmp.exe", {L"xterm"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("xterm|xterm terminal emulator,") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("clear=\\E[H\\E[2J\\E[3J") !=
              std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("cols#80") != std::string::npos);
}

TEST(infocmp, infocmp_uses_term_environment_issue1462) {
  Pipeline p;
  p.set_env(L"TERM", L"vt100");
  p.add(L"infocmp.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("vt100|DEC VT100,") != std::string::npos);
}

TEST(infocmp, infocmp_keeps_shipped_windows_ansi_entry_issue1462) {
  Pipeline p;
  p.add(L"infocmp.exe", {L"windows-ansi"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("Windows ANSI Terminal") !=
              std::string::npos);
}

TEST(infocmp, infocmp_unknown_terminal_is_an_error_issue1462) {
  Pipeline p;
  p.add(L"infocmp.exe", {L"nosuchterm"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stderr_text.find("couldn't open terminfo file nosuchterm") !=
              std::string::npos);
}

TEST(infocmp, infocmp_one_per_line_issue1462) {
  Pipeline p;
  p.add(L"infocmp.exe", {L"-1", L"vt100"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\n\tcols#80,") != std::string::npos);
}
