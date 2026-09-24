/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(more, more_basic_non_tty_passthrough) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\nline3\n");
}

TEST(more, more_squeeze_blank_non_tty) {
  TempDir tmp;
  tmp.write("test.txt", "line1\n\n\n\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"-s", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\n\nline2\n");
}

TEST(more, more_clean_print_short_option_does_not_consume_file_operand) {
  TempDir tmp;
  tmp.write("test.txt", "one\ntwo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"-p", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\ntwo\n");
}

TEST(more, more_clean_print_long_option_does_not_consume_file_operand) {
  TempDir tmp;
  tmp.write("test.txt", "one\ntwo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"--clean-print", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "one\ntwo\n");
}

TEST(more, more_numeric_window_option_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"-5", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\nline3\n");
}

TEST(more, more_long_lines_option_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "line1\nline2\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"--lines", L"5", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "line1\nline2\nline3\n");
}

TEST(more, more_plus_line_starts_output_at_line_number) {
  TempDir tmp;
  tmp.write("test.txt", "one\ntwo\nthree\nfour\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"+3", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "three\nfour\n");
}

TEST(more, more_plus_pattern_starts_output_at_match) {
  TempDir tmp;
  tmp.write("test.txt", "alpha\nbeta\nneedle here\nomega\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"+/needle", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "needle here\nomega\n");
}

TEST(more, more_plain_and_exit_on_eof_options_are_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "plain\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"more.exe", {L"--plain", L"--exit-on-eof", L"test.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "plain\n");
}

TEST(more, more_nonexistent_file) {
  Pipeline p;
  p.add(L"more.exe", {L"nonexistent_file_xyz.txt"});
  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
}
