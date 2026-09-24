/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(hmac256, hmac256_basic) {
  TempDir tmp;
  tmp.write("data.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"hmac256.exe", {L"secret", L"data.txt"});

  TEST_LOG_CMD_LIST("hmac256.exe", L"secret", L"data.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("hmac256 output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "88aab3ede8d3adf94d26ab90d3bafd4a2083070c3bcce9c014ee04a443847"
                 "c0b  data.txt\n");
}
