/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(clear, clear_matches_xterm_sequence) {
  Pipeline p;
  p.add(L"clear.exe", {});

  TEST_LOG_CMD_LIST("clear.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG_HEX("clear output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "\x1b[H\x1b[2J\x1b[3J");
}
