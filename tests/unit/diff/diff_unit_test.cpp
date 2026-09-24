// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(diff, diff_identical) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\nworld\n");
  tmp.write("file2.txt", "hello\nworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(diff, diff_distinguishes_crlf_and_lf_without_ignore_space) {
  TempDir tmp;
  tmp.write_bytes("crlf.txt", {'a', '\r', '\n'});
  tmp.write_bytes("lf.txt", {'a', '\n'});
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"crlf.txt", L"lf.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 1);
}

TEST(diff, diff_different) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\nworld\n");
  tmp.write("file2.txt", "hello\nthere\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(diff, diff_brief) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-q", L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"-q", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff brief output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("differ") != std::string::npos);
}

TEST(diff, diff_unified) {
  TempDir tmp;
  tmp.write("file1.txt", "line1\nline2\nline3\n");
  tmp.write("file2.txt", "line1\nlineX\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-u", L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"-u", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff unified output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("@@") != std::string::npos);
}

TEST(diff, diff_unified_pure_insertion_has_valid_header) {
  TempDir tmp;
  tmp.write("old.txt", "one\ntwo\n");
  tmp.write("new.txt", "one\ninserted\ntwo\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-u", L"old.txt", L"new.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("184467") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("@@") != std::string::npos);
}

TEST(diff, diff_side_by_side) {
  TempDir tmp;
  tmp.write("file1.txt", "left\nsame\n");
  tmp.write("file2.txt", "right\nsame\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-y", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("left") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("right") != std::string::npos);
}

TEST(diff, diff_ignore_all_space_unified) {
  TempDir tmp;
  tmp.write("file1.txt", "alpha beta\nsame\n");
  tmp.write("file2.txt", "alphabeta\nsame\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-w", L"-u", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(diff, diff_ignore_all_space_side_by_side) {
  TempDir tmp;
  tmp.write("file1.txt", "alpha beta\nsame\n");
  tmp.write("file2.txt", "alphabeta\nsame\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-w", L"-y", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(diff, diff_ignore_blank_lines_text_and_color_options) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\n\n");
  tmp.write("b.txt", "alpha\n");
  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"--ignore-blank-lines", L"a.txt", L"b.txt"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());

  tmp.write("c.txt", "alpha\n");
  tmp.write("d.txt", "beta\n");
  Pipeline colored;
  colored.set_cwd(tmp.wpath());
  colored.add(L"diff.exe", {L"--text", L"--color=always", L"c.txt", L"d.txt"});
  auto cr = colored.run();
  EXPECT_EQ(cr.exit_code, 1);
  EXPECT_FALSE(cr.stdout_text.empty());
}

TEST(diff, diff_wildcard_pair_expands) {
  TempDir tmp;
  tmp.write("a.txt", "same\n");
  tmp.write("b.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff wildcard output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(diff, diff_rejects_extra_operand_with_help_hint) {
  Pipeline p;
  p.add(L"diff.exe", {L"a", L"b", L"c"});

  TEST_LOG_CMD_LIST("diff.exe", L"a", L"b", L"c");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "diff: extra operand 'c'\n"
                 "Try 'diff --help' for more information.\n");
}

TEST(diff, diff_missing_all_operands_reports_help_hint) {
  Pipeline p;
  p.add(L"diff.exe", {});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff missing all operands stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "diff: missing operand\n"
                 "Try 'diff --help' for more information.\n");
}

TEST(diff, diff_single_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"diff.exe", {L"a"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff single operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "diff: missing operand after 'a'\n"
                 "Try 'diff --help' for more information.\n");
}

TEST(diff, diff_missing_input_reports_no_such_file) {
  TempDir tmp;
  tmp.write("file2.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"missing.txt", L"file2.txt"});

  auto r = p.run();

  // [GNU] An unreadable operand is "trouble": exit 2 with
  // "diff: <path>: No such file or directory" (0=same, 1=differ, 2=trouble).
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find("diff: missing.txt: No such file or directory") !=
      std::string::npos);
}

TEST(diff, diff_directory_input_reports_is_a_directory) {
  TempDir tmp;
  tmp.mkdir("indir");
  tmp.write("file2.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"indir", L"file2.txt"});

  auto r = p.run();

  // [GNU] A directory operand is rewritten to "<dir>/<basename of peer>":
  // "diff indir file2.txt" compares indir/file2.txt with file2.txt, so a
  // missing member reports "diff: indir/file2.txt: No such file or
  // directory" and exits 2.
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(
      r.stderr_text.find("diff: indir/file2.txt: No such file or directory") !=
      std::string::npos);
}

TEST(diff, diff_directory_operand_compares_member_file) {
  TempDir tmp;
  tmp.mkdir("indir");
  tmp.write("indir/file2.txt", "inner\n");
  tmp.write("file2.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"indir", L"file2.txt"});

  auto r = p.run();

  // [GNU] "diff dir file" compares dir/file with file: a differing member
  // produces a normal diff body and exit 1.
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("< inner") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("> hello") != std::string::npos);
}
