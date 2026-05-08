/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(timeout, timeout_basic) {
  Pipeline p;
  p.add(L"timeout.exe", {L"5", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("timeout.exe", L"5", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}
