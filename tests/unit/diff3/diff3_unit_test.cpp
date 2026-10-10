// SPDX-License-Identifier: MIT
// Copyright (c) 2026 WinuxCmd
#include "framework/winuxtest.h"

// The expectations below are byte-for-byte against GNU diffutils 3.10
// (verified with a WSL oracle, issue #1127 P1).

TEST(diff3, diff3_default_conflict_matches_gnu_shape) {
  TempDir tmp;
  tmp.write("mine.txt", "mine\n");
  tmp.write("base.txt", "base\n");
  tmp.write("yours.txt", "yours\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"mine.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  const std::string expected =
      "====\n"
      "1:1c\n"
      "  mine\n"
      "2:1c\n"
      "  base\n"
      "3:1c\n"
      "  yours\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(diff3, diff3_merge_conflict_matches_gnu_shape_and_status) {
  TempDir tmp;
  tmp.write("mine.txt", "same\nmine\n");
  tmp.write("base.txt", "same\nbase\n");
  tmp.write("yours.txt", "same\nyours\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"-m", L"mine.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  const std::string expected =
      "same\n"
      "<<<<<<< mine.txt\n"
      "mine\n"
      "||||||| base.txt\n"
      "base\n"
      "=======\n"
      "yours\n"
      ">>>>>>> yours.txt\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
}

TEST(diff3, diff3_yours_only_change_matches_gnu_shape) {
  TempDir tmp;
  tmp.write("mine.txt", "base\n");
  tmp.write("base.txt", "base\n");
  tmp.write("yours.txt", "yours\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"mine.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] Only YOURFILE changed: the odd file is 3, sections print in
  // order and section 1's content is suppressed (it equals YOURFILE).
  EXPECT_EQ_TEXT(r.stdout_text,
                 "====3\n"
                 "1:1c\n"
                 "2:1c\n"
                 "  base\n"
                 "3:1c\n"
                 "  yours\n");
}

TEST(diff3, diff3_mine_only_change_matches_gnu_shape) {
  TempDir tmp;
  tmp.write("mine.txt", "mine\n");
  tmp.write("base.txt", "base\n");
  tmp.write("yours.txt", "base\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"mine.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // [GNU] ====1: section 1 prints MYFILE's line, section 2's content is
  // suppressed (oddoneout 0), section 3 prints YOURFILE's identical line.
  EXPECT_EQ_TEXT(r.stdout_text,
                 "====1\n"
                 "1:1c\n"
                 "  mine\n"
                 "2:1c\n"
                 "3:1c\n"
                 "  base\n");
}

TEST(diff3, diff3_same_change_matches_gnu_default_and_merge_shape) {
  TempDir tmp;
  tmp.write("mine.txt", "same-change\n");
  tmp.write("base.txt", "base\n");
  tmp.write("yours.txt", "same-change\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"mine.txt", L"base.txt", L"yours.txt"});
  auto default_run = p.run();

  EXPECT_EQ(default_run.exit_code, 0);
  // [GNU] DIFF_2ND: sections print in skew order 1, 3, 2 and section 1's
  // content is suppressed.
  const std::string default_expected =
      "====2\n"
      "1:1c\n"
      "3:1c\n"
      "  same-change\n"
      "2:1c\n"
      "  base\n";
  EXPECT_EQ_TEXT(default_run.stdout_text, default_expected);

  Pipeline merge;
  merge.set_cwd(tmp.wpath());
  merge.add(L"diff3.exe", {L"-m", L"mine.txt", L"base.txt", L"yours.txt"});
  auto merged = merge.run();

  EXPECT_EQ(merged.exit_code, 1);
  const std::string merge_expected =
      "<<<<<<< base.txt\n"
      "base\n"
      "=======\n"
      "same-change\n"
      ">>>>>>> yours.txt\n";
  EXPECT_EQ_TEXT(merged.stdout_text, merge_expected);
}

TEST(diff3, diff3_identical_default_is_silent) {
  TempDir tmp;
  tmp.write("mine.txt", "same\n");
  tmp.write("base.txt", "same\n");
  tmp.write("yours.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"mine.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(diff3, diff3_identical_merge_prints_input) {
  TempDir tmp;
  tmp.write("mine.txt", "same\n");
  tmp.write("base.txt", "same\n");
  tmp.write("yours.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"-m", L"mine.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "same\n");
}

TEST(diff3, diff3_empty_files_are_valid_inputs) {
  TempDir tmp;
  tmp.write("mine.txt", "");
  tmp.write("base.txt", "");
  tmp.write("yours.txt", "");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"mine.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(diff3, diff3_wildcard_triplet_expands) {
  TempDir tmp;
  tmp.write("mine.txt", "same\n");
  tmp.write("older.txt", "same\n");
  tmp.write("yours.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"*.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(diff3, diff3_ed_script_applies_non_overlapping_yours_change) {
  TempDir tmp;
  tmp.write("mine.txt", "base\n");
  tmp.write("base.txt", "base\n");
  tmp.write("yours.txt", "yours\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"-e", L"mine.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text,
                 "1c\n"
                 "yours\n"
                 ".\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(diff3, diff3_ed_script_modes_follow_gnu_selection_rules) {
  TempDir tmp;
  // Overlapping conflict: mine and yours both rewrite the same region.
  tmp.write("mine.txt", "a\nB\nc\n");
  tmp.write("base.txt", "a\nb\nc\n");
  tmp.write("yours.txt", "a\nb\nC\n");

  // -e resolves the overlap by taking YOURFILE wholesale.
  Pipeline ed;
  ed.set_cwd(tmp.wpath());
  ed.add(L"diff3.exe", {L"-e", L"mine.txt", L"base.txt", L"yours.txt"});
  auto ed_run = ed.run();
  EXPECT_EQ(ed_run.exit_code, 0);
  EXPECT_EQ_TEXT(ed_run.stdout_text, "2,3c\nb\nC\n.\n");

  // -3 keeps only nonoverlapping changes (none here).
  Pipeline easy;
  easy.set_cwd(tmp.wpath());
  easy.add(L"diff3.exe", {L"-3", L"mine.txt", L"base.txt", L"yours.txt"});
  auto easy_run = easy.run();
  EXPECT_EQ(easy_run.exit_code, 0);
  EXPECT_TRUE(easy_run.stdout_text.empty());

  // -x keeps only the overlapping change.
  Pipeline overlap;
  overlap.set_cwd(tmp.wpath());
  overlap.add(L"diff3.exe", {L"-x", L"mine.txt", L"base.txt", L"yours.txt"});
  auto overlap_run = overlap.run();
  EXPECT_EQ(overlap_run.exit_code, 0);
  EXPECT_EQ_TEXT(overlap_run.stdout_text, "2,3c\nb\nC\n.\n");

  // -E brackets the overlapping change without the OLDFILE section.
  Pipeline show_overlap;
  show_overlap.set_cwd(tmp.wpath());
  show_overlap.add(L"diff3.exe",
                   {L"-E", L"mine.txt", L"base.txt", L"yours.txt"});
  auto show_overlap_run = show_overlap.run();
  EXPECT_EQ(show_overlap_run.exit_code, 1);
  EXPECT_EQ_TEXT(show_overlap_run.stdout_text,
                 "3a\n"
                 "=======\n"
                 "b\n"
                 "C\n"
                 ">>>>>>> yours.txt\n"
                 ".\n"
                 "1a\n"
                 "<<<<<<< mine.txt\n"
                 ".\n");

  // -A adds the ||||||| OLDFILE section.
  Pipeline show_all;
  show_all.set_cwd(tmp.wpath());
  show_all.add(L"diff3.exe", {L"-A", L"mine.txt", L"base.txt", L"yours.txt"});
  auto show_all_run = show_all.run();
  EXPECT_EQ(show_all_run.exit_code, 1);
  EXPECT_EQ_TEXT(show_all_run.stdout_text,
                 "3a\n"
                 "||||||| base.txt\n"
                 "b\n"
                 "c\n"
                 "=======\n"
                 "b\n"
                 "C\n"
                 ">>>>>>> yours.txt\n"
                 ".\n"
                 "1a\n"
                 "<<<<<<< mine.txt\n"
                 ".\n");

  // -i appends w and q to ed scripts.
  Pipeline final_write;
  final_write.set_cwd(tmp.wpath());
  final_write.add(L"diff3.exe",
                  {L"-i", L"-e", L"mine.txt", L"base.txt", L"yours.txt"});
  auto final_write_run = final_write.run();
  EXPECT_EQ(final_write_run.exit_code, 0);
  EXPECT_EQ_TEXT(final_write_run.stdout_text, "2,3c\nb\nC\n.\nw\nq\n");
}

TEST(diff3, diff3_merge_mode_selection_matches_gnu) {
  TempDir tmp;
  tmp.write("mine.txt", "a\nB\nc\n");
  tmp.write("base.txt", "a\nb\nc\n");
  tmp.write("yours.txt", "a\nb\nC\n");

  // Bare -m means -A: full bracketed conflict.
  Pipeline merge;
  merge.set_cwd(tmp.wpath());
  merge.add(L"diff3.exe", {L"-m", L"mine.txt", L"base.txt", L"yours.txt"});
  auto merge_run = merge.run();
  EXPECT_EQ(merge_run.exit_code, 1);
  EXPECT_EQ_TEXT(merge_run.stdout_text,
                 "a\n"
                 "<<<<<<< mine.txt\n"
                 "B\n"
                 "c\n"
                 "||||||| base.txt\n"
                 "b\n"
                 "c\n"
                 "=======\n"
                 "b\n"
                 "C\n"
                 ">>>>>>> yours.txt\n");

  // -m -e resolves by taking YOURFILE's lines (no conflict).
  Pipeline merge_ed;
  merge_ed.set_cwd(tmp.wpath());
  merge_ed.add(L"diff3.exe",
               {L"-m", L"-e", L"mine.txt", L"base.txt", L"yours.txt"});
  auto merge_ed_run = merge_ed.run();
  EXPECT_EQ(merge_ed_run.exit_code, 0);
  EXPECT_EQ_TEXT(merge_ed_run.stdout_text, "a\nb\nC\n");

  // -m -3 keeps MYFILE's lines when nothing nonoverlapping applies.
  Pipeline merge_easy;
  merge_easy.set_cwd(tmp.wpath());
  merge_easy.add(L"diff3.exe",
                 {L"-m", L"-3", L"mine.txt", L"base.txt", L"yours.txt"});
  auto merge_easy_run = merge_easy.run();
  EXPECT_EQ(merge_easy_run.exit_code, 0);
  EXPECT_EQ_TEXT(merge_easy_run.stdout_text, "a\nB\nc\n");
}

TEST(diff3, diff3_labels_replace_conflict_tags) {
  TempDir tmp;
  tmp.write("mine.txt", "a\nB\nc\n");
  tmp.write("base.txt", "a\nb\nc\n");
  tmp.write("yours.txt", "a\nb\nC\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"-m", L"-L", L"L1", L"-L", L"L2", L"mine.txt",
                       L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("<<<<<<< L1\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("||||||| L2\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find(">>>>>>> yours.txt\n") != std::string::npos);
}

TEST(diff3, diff3_initial_tab_indents_report_lines) {
  TempDir tmp;
  tmp.write("mine.txt", "a\nB\nc\n");
  tmp.write("base.txt", "a\nb\nc\n");
  tmp.write("yours.txt", "a\nb\nC\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"-T", L"mine.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("\tB\n") != std::string::npos);
}

TEST(diff3, diff3_missing_operand_matches_gnu_message_and_status) {
  Pipeline p;
  p.add(L"diff3.exe", {L"a.txt", L"b.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 missing operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "diff3: missing operand after 'b.txt'\n"
            "diff3: Try 'diff3 --help' for more information.\n");
}

TEST(diff3, diff3_missing_all_operands_names_the_program) {
  Pipeline p;
  p.add(L"diff3.exe", {});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 missing all operands stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "diff3: missing operand after 'diff3'\n"
            "diff3: Try 'diff3 --help' for more information.\n");
}

TEST(diff3, diff3_too_many_files_fails) {
  Pipeline p;
  p.add(L"diff3.exe", {L"a.txt", L"b.txt", L"c.txt", L"d.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "diff3: extra operand 'd.txt'\n"
            "diff3: Try 'diff3 --help' for more information.\n");
}

TEST(diff3, diff3_incompatible_options_match_gnu) {
  Pipeline p;
  p.add(L"diff3.exe", {L"-e", L"-A", L"a", L"b", L"c"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ(r.stderr_text,
            "diff3: incompatible options\n"
            "diff3: Try 'diff3 --help' for more information.\n");

  Pipeline labels;
  labels.add(L"diff3.exe", {L"-L", L"x", L"a", L"b", L"c"});
  auto labels_run = labels.run();
  EXPECT_EQ(labels_run.exit_code, 2);
  EXPECT_EQ(labels_run.stderr_text,
            "diff3: incompatible options\n"
            "diff3: Try 'diff3 --help' for more information.\n");
}

TEST(diff3, diff3_missing_input_reports_subsidiary_failure) {
  TempDir tmp;
  tmp.write("base.txt", "base\n");
  tmp.write("yours.txt", "yours\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff3.exe", {L"missing.txt", L"base.txt", L"yours.txt"});
  auto r = p.run();

  // [GNU] The subsidiary diff reports the unreadable operand, then diff3
  // reports the failure (exit status 2).
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(
      r.stderr_text.find("diff: missing.txt: No such file or directory") !=
      std::string::npos);
  EXPECT_TRUE(r.stderr_text.find(
                  "diff3: subsidiary program 'diff' failed (exit status 2)") !=
              std::string::npos);
}
