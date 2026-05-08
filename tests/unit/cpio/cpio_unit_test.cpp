/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(cpio, cpio_basic) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"cpio.exe", {L"-o"});

  TEST_LOG_CMD_LIST("cpio.exe", L"-o");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}
