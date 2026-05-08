/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(dos2unix, dos2unix_from_stdin) {
  Pipeline p;
  p.set_stdin("line1\r\nline2\r\n");
  p.add(L"dos2unix.exe", {});

  TEST_LOG_CMD_LIST("dos2unix.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("dos2unix output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\n");
}
