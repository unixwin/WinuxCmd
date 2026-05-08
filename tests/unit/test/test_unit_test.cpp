/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(test, test_file_exists) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"test.exe", {L"-f", L"test.txt"});

  TEST_LOG_CMD_LIST("test.exe", L"-f", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("test stdout", r.stdout_text);
  TEST_LOG("test stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(test, test_file_not_exists) {
  Pipeline p;
  p.add(L"test.exe", {L"-f", L"nonexistent.txt"});

  TEST_LOG_CMD_LIST("test.exe", L"-f", L"nonexistent.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
}
