/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(col, col_basic) {
  Pipeline p;
  p.set_stdin("hello\nworld\n");
  p.add(L"col.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("world") != std::string::npos);
}

TEST(col, col_no_backspaces) {
  Pipeline p;
  p.set_stdin("abc\bd\n");
  p.add(L"col.exe", {L"-b"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // With -b, backspace should be ignored
}

TEST(col, col_spaces_for_tabs) {
  Pipeline p;
  p.set_stdin("hello\tworld\n");
  p.add(L"col.exe", {L"-x"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Tabs should be converted to spaces
}

TEST(col, col_forward_half) {
  Pipeline p;
  p.set_stdin("line1\nline2\n");
  p.add(L"col.exe", {L"-f"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(col, col_pass_unknown) {
  Pipeline p;
  p.set_stdin("hello\x01world\n");
  p.add(L"col.exe", {L"-p"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(col, col_empty_input) {
  Pipeline p;
  p.set_stdin("");
  p.add(L"col.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}
