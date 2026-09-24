/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(sdiff, sdiff_basic_matches_gnu_width_80_shape) {
  TempDir tmp;
  tmp.write("file1.txt", "same\nleft\n");
  tmp.write("file2.txt", "same\nright\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"80", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "same\t\t\t\t\tsame\n"
                 "left\t\t\t\t      |\tright\n");
}

TEST(sdiff, sdiff_output_file) {
  TempDir tmp;
  tmp.write("a.txt", "line1\nline2\n");
  tmp.write("b.txt", "line1\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-o", L"output.txt", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}

TEST(sdiff, sdiff_suppress_common_matches_gnu_width_80_shape) {
  TempDir tmp;
  tmp.write("a.txt", "same\nleft\n");
  tmp.write("b.txt", "same\nright\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-s", L"-w", L"80", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "left\t\t\t\t      |\tright\n");
}

TEST(sdiff, sdiff_width_difference_returns_one) {
  TempDir tmp;
  tmp.write("a.txt", "hello\n");
  tmp.write("b.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"120", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}

TEST(sdiff, sdiff_ignore_tab_expansion) {
  TempDir tmp;
  tmp.write("a.txt", "hello\tworld\n");
  tmp.write("b.txt", "hello    world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-E", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}

TEST(sdiff, sdiff_identical_files) {
  TempDir tmp;
  tmp.write("a.txt", "same\n");
  tmp.write("b.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sdiff, sdiff_empty_files) {
  TempDir tmp;
  tmp.write("a.txt", "");
  tmp.write("b.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(sdiff, sdiff_rejects_extra_operand_with_help_hint) {
  Pipeline p;
  p.add(L"sdiff.exe", {L"a.txt", L"b.txt", L"c.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sdiff extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "sdiff: extra operand 'c.txt'\n"
            "Try 'sdiff --help' for more information.\n");
}

TEST(sdiff, sdiff_missing_all_operands_reports_help_hint) {
  Pipeline p;
  p.add(L"sdiff.exe", {});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sdiff missing all operands stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "sdiff: missing operand\n"
            "Try 'sdiff --help' for more information.\n");
}

TEST(sdiff, sdiff_single_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"sdiff.exe", {L"a.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sdiff single operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "sdiff: missing operand after 'a.txt'\n"
            "Try 'sdiff --help' for more information.\n");
}
