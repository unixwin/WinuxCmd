/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(pathchk, pathchk_valid) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"/valid/path.txt"});

  TEST_LOG_CMD_LIST("pathchk.exe", L"/valid/path.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}
