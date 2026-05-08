/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(stdbuf, stdbuf_basic) {
  Pipeline p;
  p.add(L"stdbuf.exe", {L"--output=0", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("stdbuf.exe", L"--output=0", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}
