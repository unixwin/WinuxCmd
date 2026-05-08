/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(numfmt, numfmt_basic) {
  Pipeline p;
  p.set_stdin("1000\n2000\n");
  p.add(L"numfmt.exe", {});

  TEST_LOG_CMD_LIST("numfmt.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("numfmt output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
