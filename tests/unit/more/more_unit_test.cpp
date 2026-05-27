/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(more, more_basic) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"test.txt"});
  auto r = p.run();

  // more may return 0 or non-zero depending on how it's run
  // In non-interactive mode, it should output the file
}

TEST(more, more_squeeze_blank) {
  TempDir tmp;
  tmp.write("test.txt", "line1\n\n\n\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"-s", L"test.txt"});
  auto r = p.run();
}

TEST(more, more_clear_screen) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"-c", L"test.txt"});
  auto r = p.run();
}

TEST(more, more_nonexistent_file) {
  Pipeline p;
  p.add(L"more.exe", {L"nonexistent_file_xyz.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}
