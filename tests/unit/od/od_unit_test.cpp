/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(od, od_basic) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"od.exe", {L"-c"});

  TEST_LOG_CMD_LIST("od.exe", L"-c");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("od output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}
