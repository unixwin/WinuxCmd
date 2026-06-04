/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(tee, tee_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("hello\n");
  p.add(L"tee.exe", {L"output.txt"});

  TEST_LOG_CMD_LIST("tee.exe", L"output.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tee output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hello\n");
}

TEST(tee, tee_output_operand_is_literal_not_glob) {
  TempDir tmp;
  tmp.write("outa.txt", "old\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("new\n");
  p.add(L"tee.exe", {L"out[abc].txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "new\n");
  EXPECT_EQ(tmp.read("outa.txt"), "old\n");
  EXPECT_EQ(tmp.read("out[abc].txt"), "new\n");
}
