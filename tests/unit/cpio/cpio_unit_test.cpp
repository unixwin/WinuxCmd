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

TEST(cpio, cpio_newc_create_and_list_roundtrip) {
  TempDir tmp;
  tmp.write("cat-no-newline.txt", "tail");

  Pipeline create;
  create.set_cwd(tmp.wpath());
  create.set_stdin("cat-no-newline.txt\n");
  create.add(L"cpio.exe", {L"-o"});
  auto created = create.run();

  EXPECT_EQ(created.exit_code, 0);
  EXPECT_TRUE(created.stderr_text.empty());
  EXPECT_TRUE(created.stdout_text.starts_with("070701"));
  EXPECT_NE(created.stdout_text.find("cat-no-newline.txt"), std::string::npos);
  EXPECT_NE(created.stdout_text.find("TRAILER!!!"), std::string::npos);

  Pipeline list;
  list.set_cwd(tmp.wpath());
  list.set_stdin(created.stdout_text);
  list.add(L"cpio.exe", {L"-t"});
  auto listed = list.run();

  EXPECT_EQ(listed.exit_code, 0);
  EXPECT_EQ_TEXT(listed.stdout_text, "-          4 cat-no-newline.txt\n");
}
