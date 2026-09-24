/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(test, test_file_exists) {
  TempDir tmp;
  tmp.write("test.txt", "hello");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"test.exe", {L"-f", L"test.txt"});

  TEST_LOG_CMD_LIST("test.exe", L"-f", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("test stdout", r.stdout_text);
  TEST_LOG("test stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(test, test_file_not_exists) {
  Pipeline p;
  p.add(L"test.exe", {L"-f", L"nonexistent.txt"});

  TEST_LOG_CMD_LIST("test.exe", L"-f", L"nonexistent.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 1);
}

TEST(test, test_file_time_and_identity_operators) {
  TempDir tmp;
  tmp.write("old.txt", "old");
  tmp.write("new.txt", "new");
  std::error_code ec;
  auto old_time = std::filesystem::last_write_time(tmp.path / "old.txt", ec);
  std::filesystem::last_write_time(tmp.path / "new.txt",
                                   old_time + std::chrono::seconds(2), ec);

  Pipeline newer;
  newer.set_cwd(tmp.wpath());
  newer.add(L"test.exe", {L"new.txt", L"-nt", L"old.txt"});
  EXPECT_EQ(newer.run().exit_code, 0);

  Pipeline older;
  older.set_cwd(tmp.wpath());
  older.add(L"test.exe", {L"old.txt", L"-ot", L"new.txt"});
  EXPECT_EQ(older.run().exit_code, 0);

  Pipeline same;
  same.set_cwd(tmp.wpath());
  same.add(L"test.exe", {L"old.txt", L"-ef", L"old.txt"});
  EXPECT_EQ(same.run().exit_code, 0);
}

TEST(test, test_logical_precedence_and_parentheses) {
  Pipeline p;
  p.add(L"test.exe", {L"1", L"-eq", L"2", L"-o", L"3", L"-eq", L"3", L"-a",
                      L"4", L"-eq", L"4"});
  EXPECT_EQ(p.run().exit_code, 0);

  Pipeline grouped;
  grouped.add(L"test.exe", {L"(", L"1", L"-eq", L"2", L"-o", L"3", L"-eq", L"3",
                            L")", L"-a", L"4", L"-eq", L"4"});
  EXPECT_EQ(grouped.run().exit_code, 0);
}

TEST(test, test_single_arg_is_string_test) {
  // GNU: a single argument is always a string test, even '!' or '('.
  Pipeline bang;
  bang.add(L"test.exe", {L"!"});
  EXPECT_EQ(bang.run().exit_code, 0);

  Pipeline paren;
  paren.add(L"test.exe", {L"("});
  EXPECT_EQ(paren.run().exit_code, 0);

  Pipeline unary;
  unary.add(L"test.exe", {L"-w"});
  EXPECT_EQ(unary.run().exit_code, 0);
}

TEST(test, test_missing_argument_after_binary_operator) {
  Pipeline p;
  p.add(L"test.exe", {L"x", L"-a"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "test: missing argument after '-a'\n");
}

TEST(test, test_unary_operator_expected) {
  Pipeline p;
  p.add(L"test.exe", {L"-o", L"x"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "test: '-o': unary operator expected\n");
}

TEST(test, test_missing_argument_after_integer_operator) {
  Pipeline p;
  p.add(L"test.exe", {L"x", L"-eq"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "test: missing argument after '-eq'\n");
}

TEST(test, test_invalid_integer_message) {
  Pipeline p;
  p.add(L"test.exe", {L"x", L"-eq", L"3"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "test: invalid integer 'x'\n");

  Pipeline reversed;
  reversed.add(L"test.exe", {L"3", L"-eq", L"x"});
  EXPECT_EQ_TEXT(reversed.run().stderr_text, "test: invalid integer 'x'\n");
}

TEST(test, test_two_plain_args_missing_argument) {
  Pipeline p;
  p.add(L"test.exe", {L"x", L"y"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "test: missing argument after 'y'\n");
}

TEST(test, test_three_plain_args_binary_operator_expected) {
  Pipeline p;
  p.add(L"test.exe", {L"x", L"y", L"z"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "test: 'y': binary operator expected\n");
}

// [GNU] test.c never calls getopt: --help/--version are honored only as
// the sole argument; otherwise they are expression operands diagnosed by
// the expression parser with status 2 (Savannah #1194 class).

TEST(test, test_help_only_as_sole_argument) {
  Pipeline p;
  p.add(L"test.exe", {L"--help"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("Usage"), std::string::npos);
}

TEST(test, test_version_only_as_sole_argument) {
  Pipeline p;
  p.add(L"test.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("test (WinuxCmd)"), std::string::npos);
}

TEST(test, test_help_with_extra_operand_is_expression_error) {
  Pipeline p;
  p.add(L"test.exe", {L"--help", L"x"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "test: missing argument after 'x'\n");
}

TEST(test, test_help_after_operand_is_expression_error) {
  Pipeline p;
  p.add(L"test.exe", {L"x", L"--help"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "test: missing argument after '--help'\n");
}

TEST(test, test_version_with_extra_operand_is_expression_error) {
  Pipeline p;
  p.add(L"test.exe", {L"--version", L"x"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 2);
  EXPECT_EQ_TEXT(r.stderr_text, "test: missing argument after 'x'\n");
}

TEST(test, test_long_option_operand_is_a_nonempty_string) {
  // GNU: `test --foo` is a single non-empty STRING test -> true (0),
  // not an "unrecognized option" error.
  Pipeline p;
  p.add(L"test.exe", {L"--foo"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stderr_text, "");
}
