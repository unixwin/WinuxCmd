/*
 *  Copyright (c) 2026 WinuxCmd
 */
#include "framework/winuxtest.h"

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
  EXPECT_TRUE(r.stderr_text.empty());
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
  const std::string expected =
      "====3\n"
      "1:1c\n"
      "2:1c\n"
      "  base\n"
      "3:1c\n"
      "  yours\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
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
  const std::string expected =
      "====1\n"
      "1:1c\n"
      "  mine\n"
      "2:1c\n"
      "3:1c\n"
      "  base\n";
  EXPECT_EQ_TEXT(r.stdout_text, expected);
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
  const std::string default_expected =
      "====2\n"
      "1:1c\n"
      "  same-change\n"
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

TEST(diff3, diff3_ed_script_options_remain_accepted) {
  TempDir tmp;
  tmp.write("mine.txt", "same\nmine\n");
  tmp.write("base.txt", "same\nbase\n");
  tmp.write("yours.txt", "same\nyours\n");

  for (const auto* opt : {L"-e", L"-E", L"-A"}) {
    Pipeline p;
    p.set_cwd(tmp.wpath());
    p.add(L"diff3.exe", {opt, L"mine.txt", L"base.txt", L"yours.txt"});
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
  }
}

TEST(diff3, diff3_missing_file_fails) {
  Pipeline p;
  p.add(L"diff3.exe", {L"a.txt", L"b.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 missing operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "diff3: missing operand after 'b.txt'\n"
            "Try 'diff3 --help' for more information.\n");
}

TEST(diff3, diff3_missing_all_operands_reports_help_hint) {
  Pipeline p;
  p.add(L"diff3.exe", {});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 missing all operands stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "diff3: missing operand\n"
            "Try 'diff3 --help' for more information.\n");
}

TEST(diff3, diff3_too_many_files_fails) {
  Pipeline p;
  p.add(L"diff3.exe", {L"a.txt", L"b.txt", L"c.txt", L"d.txt"});
  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff3 extra operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ(r.stderr_text,
            "diff3: extra operand 'd.txt'\n"
            "Try 'diff3 --help' for more information.\n");
}
