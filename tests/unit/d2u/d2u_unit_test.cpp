// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(d2u, d2u_from_stdin) {
  Pipeline p;
  p.set_stdin("line1\r\nline2\r\nline3\r\n");
  p.add(L"d2u.exe", {});

  TEST_LOG_CMD_LIST("d2u.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("d2u output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\nline3\n");
}

TEST(d2u, d2u_file) {
  TempDir tmp;
  tmp.write("dos.txt", "line1\r\nline2\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"d2u.exe", {L"dos.txt"});

  TEST_LOG_CMD_LIST("d2u.exe", L"dos.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(d2u, d2u_verbose) {
  TempDir tmp;
  tmp.write("dos.txt", "line1\r\nline2\r\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"d2u.exe", {L"-v", L"dos.txt"});

  TEST_LOG_CMD_LIST("d2u.exe", L"-v", L"dos.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("d2u stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("converted") != std::string::npos);
}
