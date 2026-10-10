// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WinuxCmd
#include "framework/winuxtest.h"

// Expectations byte-matched against GNU diffutils 3.10 (WSL oracle,
// issue #1127 P1).  The display is exactly `diff --side-by-side` output.

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

TEST(sdiff, sdiff_left_column_marks_suppressed_right) {
  TempDir tmp;
  tmp.write("a.txt", "same\nleft\n");
  tmp.write("b.txt", "same\nright\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-l", L"-w", L"80", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  // [GNU] Common lines print once with '(' in the gutter; changed rows
  // still pair with '|'.
  EXPECT_EQ_TEXT(r.stdout_text,
                 "same\t\t\t\t      (\n"
                 "left\t\t\t\t      |\tright\n");
}

TEST(sdiff, sdiff_identical_files_print_common_rows_and_exit_zero) {
  TempDir tmp;
  tmp.write("a.txt", "same\n");
  tmp.write("b.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"80", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "same\t\t\t\t\tsame\n");

  Pipeline suppressed;
  suppressed.set_cwd(tmp.wpath());
  suppressed.add(L"sdiff.exe", {L"-s", L"-w", L"80", L"a.txt", L"b.txt"});
  auto suppressed_run = suppressed.run();
  EXPECT_EQ(suppressed_run.exit_code, 0);
  EXPECT_TRUE(suppressed_run.stdout_text.empty());
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

TEST(sdiff, sdiff_insert_only_rows_use_right_arrow) {
  TempDir tmp;
  tmp.write("a.txt", "a\n");
  tmp.write("b.txt", "a\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-s", L"-w", L"20", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text, "      >\tb\n      >\tc\n");
}

TEST(sdiff, sdiff_ignore_tab_expansion_matches_gnu_comparison) {
  TempDir tmp;
  tmp.write("a.txt", "hello\tworld\n");
  tmp.write("b.txt", "hello   world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-E", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU -E] Tabs compare as spaces up to the next tab stop, so the lines
  // match and the files are equal.
  EXPECT_EQ(r.exit_code, 0);
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
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(sdiff, sdiff_ignore_family_zero_exit_when_lines_match) {
  TempDir tmp;
  tmp.write("a.txt", "Alpha\nend \n");
  tmp.write("b.txt", "ALPHA\nend\n");

  // -i: case differences only.
  Pipeline case_insensitive;
  case_insensitive.set_cwd(tmp.wpath());
  case_insensitive.add(L"sdiff.exe", {L"-i", L"a.txt", L"b.txt"});
  auto case_run = case_insensitive.run();
  EXPECT_EQ(case_run.exit_code, 1);

  // -Z ignores the trailing space, -b its run: "end " == "end".
  Pipeline trailing;
  trailing.set_cwd(tmp.wpath());
  trailing.add(L"sdiff.exe", {L"-Z", L"a.txt", L"b.txt"});
  auto trailing_run = trailing.run();
  EXPECT_EQ(trailing_run.exit_code, 1);

  // -W ignores everything whitespace, so both rows are common and the
  // only difference left is the case on line 1 - still a difference.
  Pipeline all_space;
  all_space.set_cwd(tmp.wpath());
  all_space.add(L"sdiff.exe", {L"-W", L"a.txt", L"b.txt"});
  auto all_space_run = all_space.run();
  EXPECT_EQ(all_space_run.exit_code, 1);

  // Same lines entirely: every ignore mode agrees on zero.
  tmp.write("b.txt", "ALPHA\nend \n");
  Pipeline case_only;
  case_only.set_cwd(tmp.wpath());
  case_only.add(L"sdiff.exe", {L"-i", L"-s", L"a.txt", L"b.txt"});
  auto case_only_run = case_only.run();
  EXPECT_EQ(case_only_run.exit_code, 0);
  EXPECT_TRUE(case_only_run.stdout_text.empty());
}

TEST(sdiff, sdiff_no_trailing_newline_uses_slash_separators) {
  TempDir tmp;
  tmp.write_bytes("a.txt", {'a', '\n', 'l', 'e', 'f', 't'});
  tmp.write("b.txt", "a\nright\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-w", L"20", L"a.txt", L"b.txt"});
  auto r = p.run();

  // [GNU] When only one side of a changed row is newline-terminated the
  // separator becomes '/' (left continues) or '\\' (right continues).
  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "a\ta\n"
                 "left  \\\tright\n");
}

TEST(sdiff, sdiff_output_file_auto_merges_non_interactively) {
  TempDir tmp;
  tmp.write("a.txt", "a\nleft\nkeep\n");
  tmp.write("b.txt", "a\nright\nkeep\nmore\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"a.txt", L"b.txt"});
  auto r = p.run();

  // Issue #1127 P1: -o merges without prompting.  Overlapping changes
  // keep the left column; one-sided changes take their only side.  A
  // completed merge exits 0 like GNU's completed interactive merge.
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "a\nleft\nkeep\nmore\n");
}

TEST(sdiff, sdiff_output_file_takes_right_for_right_only_changes) {
  TempDir tmp;
  tmp.write("a.txt", "a\nb\nc\n");
  tmp.write("b.txt", "a\nb\nc\nD\nE\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "a\nb\nc\nD\nE\n");
}

TEST(sdiff, sdiff_output_file_keeps_left_for_conflicting_rows) {
  TempDir tmp;
  tmp.write("a.txt", "a\nB\nc\n");
  tmp.write("b.txt", "a\nb\nc\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"-o", L"merged.txt", L"a.txt", L"b.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(tmp.read("merged.txt"), "a\nB\nc\n");
}

TEST(sdiff, sdiff_rejects_extra_operand_with_help_hint) {
  Pipeline p;
  p.add(L"sdiff.exe", {L"a.txt", L"b.txt", L"c.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sdiff extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "sdiff: extra operand 'c.txt'\n"
            "sdiff: Try 'sdiff --help' for more information.\n");
}

TEST(sdiff, sdiff_missing_all_operands_reports_help_hint) {
  Pipeline p;
  p.add(L"sdiff.exe", {});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sdiff missing all operands stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "sdiff: missing operand after 'sdiff'\n"
            "sdiff: Try 'sdiff --help' for more information.\n");
}

TEST(sdiff, sdiff_single_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"sdiff.exe", {L"a.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sdiff single operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "sdiff: missing operand after 'a.txt'\n"
            "sdiff: Try 'sdiff --help' for more information.\n");
}

TEST(sdiff, sdiff_missing_input_reports_diff_diagnostic) {
  TempDir tmp;
  tmp.write("b.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sdiff.exe", {L"missing.txt", L"b.txt"});
  auto r = p.run();

  // [GNU] Without -o sdiff execs diff, so the diagnostic carries diff's
  // program name and the exit status is trouble.
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ(r.stderr_text, "diff: missing.txt: No such file or directory\n");
}
