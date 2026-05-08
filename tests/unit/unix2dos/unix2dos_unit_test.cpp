/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(unix2dos, unix2dos_from_stdin) {
  Pipeline p;
  p.set_stdin("line1\nline2\n");
  p.add(L"unix2dos.exe", {});

  TEST_LOG_CMD_LIST("unix2dos.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("unix2dos output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\r\n") != std::string::npos);
}
