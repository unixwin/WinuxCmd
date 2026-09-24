/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(mpicalc, mpicalc_hex_rpn_basic) {
  Pipeline p;
  p.set_stdin("2 3 + p\n0a 5 * p\n10 3 / p\n10 3 % p\n");
  p.add(L"mpicalc.exe", {});

  TEST_LOG_CMD_LIST("mpicalc.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mpicalc output", r.stdout_text);
  TEST_LOG("mpicalc stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "05\n32\n05\n01\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(mpicalc, mpicalc_hex_rpn_stack_ops) {
  Pipeline p;
  p.set_stdin("2 8 11 ^ p\n2 8 11 m p\n0ff b p\n2 3 r f\n");
  p.add(L"mpicalc.exe", {});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("mpicalc output", r.stdout_text);
  TEST_LOG("mpicalc stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "01\n10\n08\n[ 1]: 02\n[ 0]: 03\n");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}
