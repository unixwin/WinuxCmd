/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(sdiff, sdiff_basic) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\nworld\n");
  tmp.write("file2.txt", "hello\nuniverse\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("sdiff.exe", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}
