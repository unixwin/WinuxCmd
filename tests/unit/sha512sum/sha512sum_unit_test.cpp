/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(sha512sum, sha512sum_basic) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sha512sum.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("sha512sum.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sha512sum output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
