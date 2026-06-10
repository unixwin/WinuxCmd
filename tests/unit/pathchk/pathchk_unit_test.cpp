/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(pathchk, pathchk_valid) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"/valid/path.txt"});

  TEST_LOG_CMD_LIST("pathchk.exe", L"/valid/path.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pathchk, pathchk_accepts_windows_drive_absolute_path) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"C:\\valid\\path.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(pathchk, pathchk_default_rejects_leading_dash_component) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"safe/-bad"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("leading '-'") != std::string::npos);
}

TEST(pathchk, pathchk_default_accepts_trailing_separator) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"safe/path/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(pathchk, pathchk_P_accepts_repeated_separators_without_empty_name_error) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"-P", L"safe//nested///path/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(pathchk, pathchk_default_empty_path_reports_gnu_style_missing_entry) {
  Pipeline p;
  p.add(L"pathchk.exe", {L""});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "pathchk: '': No such file or directory\n");
}

TEST(pathchk, pathchk_multiple_empty_paths_report_each_gnu_style_error) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"", L""});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "pathchk: '': No such file or directory\n"
                 "pathchk: '': No such file or directory\n");
}

TEST(pathchk, pathchk_posixly_correct_disables_default_dash_check) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"pathchk.exe", {L"safe/-bad"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(pathchk, pathchk_P_rejects_empty_name) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"-P", L""});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("empty file name") != std::string::npos);
}

TEST(pathchk, pathchk_portability_rejects_nonportable_character) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"-p", L"has space"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("nonportable") != std::string::npos);
}

TEST(pathchk, pathchk_long_option_portability_implies_P) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"--portability", L"ok/-bad"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("leading '-'") != std::string::npos);
}

TEST(pathchk, pathchk_p_checks_posix_component_length) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"-p", L"abcdefghijklmnop"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("too long") != std::string::npos);
}

TEST(pathchk, pathchk_rejects_windows_reserved_name) {
  Pipeline p;
  p.add(L"pathchk.exe", {L"COM1.txt"});

  auto r = p.run();

  EXPECT_NE(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.find("invalid Windows filename") !=
              std::string::npos);
}

TEST(pathchk, pathchk_missing_operand_reports_help_hint_on_stderr) {
  Pipeline p;
  p.add(L"pathchk.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "pathchk: missing operand\nTry 'pathchk --help' for more information.\n");
}
