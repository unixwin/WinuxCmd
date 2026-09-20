// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(less, less_file_basic) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  TEST_LOG_FILE_CONTENT("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-F", L"test.txt"});

  TEST_LOG_CMD_LIST("less.exe", L"-F", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("less output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\nline3\n");
  p.add(L"less.exe", {L"-F"});

  TEST_LOG_CMD_LIST("less.exe", L"-F");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("less stdin output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_quit_if_one_screen) {
  TempDir tmp;
  tmp.write("short.txt", "test\n");

  TEST_LOG_FILE_CONTENT("short.txt", "test\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-F", L"short.txt"});

  TEST_LOG_CMD_LIST("less.exe", L"-F", L"short.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("less quit if one screen output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_numeric_window_option_is_accepted) {
  TempDir tmp;
  tmp.write("short.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-50", L"-F", L"short.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_quit_on_interrupt_option_is_accepted) {
  TempDir tmp;
  tmp.write("short.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"-K", L"-F", L"short.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(less, less_no_init_option_is_accepted) {
  TempDir tmp;
  tmp.write("short.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"less.exe", {L"--no-init", L"-F", L"short.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}
