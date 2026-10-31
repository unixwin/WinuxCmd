// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(tic, tic_with_file) {
  // [#1462] Terminfo compilation is genuinely unsupported in this
  // build, so a source-file operand is an honest failure on stderr —
  // the historical build printed the notice on stdout and exited 0,
  // which scripts read as a successful compile.
  Pipeline p;
  p.add(L"tic.exe", {L"myterm.ti"});

  TEST_LOG_CMD_LIST("tic.exe", L"myterm.ti");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tic output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_FALSE(r.stderr_text.empty());
}

TEST(tic, tic_missing_file) {
  Pipeline p;
  p.add(L"tic.exe", {});

  TEST_LOG_CMD_LIST("tic.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tic stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stderr_text.empty());
}
