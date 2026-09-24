/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(test_bracket, test_bracket_file_exists) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"[.exe", {L"-f", L"test.txt", L"]"});

  TEST_LOG_CMD_LIST("[.exe", L"-f", L"test.txt", L"]");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(test_bracket, test_bracket_numeric_false_expression) {
  Pipeline p;
  p.add(L"[.exe", {L"12", L"-eq", L"13", L"]"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
}

TEST(test_bracket, test_bracket_logical_precedence_and_negation) {
  Pipeline p;
  p.add(L"[.exe", {L"!", L"(", L"1", L"-eq", L"2", L")", L"-a", L"3", L"-eq",
                   L"3", L"]"});
  EXPECT_EQ(p.run().exit_code, 0);
}

// [GNU] test.c honors --help/--version for `[` only when it is the sole
// argument (i.e. `[ --help` with no closing `]`); otherwise the token is
// an expression operand (Savannah #1194 class).

TEST(test_bracket, test_bracket_help_without_closing_bracket_prints_usage) {
  Pipeline p;
  p.add(L"[.exe", {L"--help"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Usage"), std::string::npos);
}

TEST(test_bracket,
     test_bracket_version_without_closing_bracket_prints_version) {
  Pipeline p;
  p.add(L"[.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("[ (WinuxCmd)"), std::string::npos);
}

TEST(test_bracket, test_bracket_help_before_closing_bracket_is_string_test) {
  // GNU: `[ --help ]` is a silent non-empty-string test -> true (0).
  Pipeline p;
  p.add(L"[.exe", {L"--help", L"]"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "");
  EXPECT_EQ_TEXT(r.stderr_text, "");
}

TEST(test_bracket, test_bracket_help_with_extra_operand_is_expression_error) {
  Pipeline p;
  p.add(L"[.exe", {L"--help", L"x", L"]"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "[: missing argument after 'x'\n");
}
