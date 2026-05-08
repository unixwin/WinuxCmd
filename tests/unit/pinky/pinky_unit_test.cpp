/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(pinky, pinky_basic) {
  Pipeline p;
  p.add(L"pinky.exe", {});

  TEST_LOG_CMD_LIST("pinky.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("pinky output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
