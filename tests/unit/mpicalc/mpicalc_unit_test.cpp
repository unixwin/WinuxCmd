/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(mpicalc, mpicalc_basic) {
  Pipeline p;
  p.add(L"mpicalc.exe", {L"2+2"});

  TEST_LOG_CMD_LIST("mpicalc.exe", L"2+2");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mpicalc output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
