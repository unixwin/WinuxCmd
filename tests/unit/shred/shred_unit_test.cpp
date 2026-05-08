/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(shred, shred_basic) {
  TempDir tmp;
  tmp.write("secret.txt", "sensitive data");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"shred.exe", {L"-u", L"secret.txt"});

  TEST_LOG_CMD_LIST("shred.exe", L"-u", L"secret.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}
