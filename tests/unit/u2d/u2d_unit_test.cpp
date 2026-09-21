// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(u2d, u2d_from_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\nline3\n");
  p.add(L"u2d.exe", {});

  TEST_LOG_CMD_LIST("u2d.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("u2d output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\r\n") != std::string::npos);
}

TEST(u2d, u2d_file) {
  TempDir tmp;
  tmp.write("unix.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"u2d.exe", {L"unix.txt"});

  TEST_LOG_CMD_LIST("u2d.exe", L"unix.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(u2d, u2d_verbose) {
  TempDir tmp;
  tmp.write("unix.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"u2d.exe", {L"-v", L"unix.txt"});

  TEST_LOG_CMD_LIST("u2d.exe", L"-v", L"unix.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("u2d stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("converted") != std::string::npos);
}
