/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(xxd, xxd_from_stdin) {
  Pipeline p;
  p.set_stdin("hello");
  p.add(L"xxd.exe", {});

  TEST_LOG_CMD_LIST("xxd.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("xxd output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}
