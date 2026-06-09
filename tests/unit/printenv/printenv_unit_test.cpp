#include "framework/winuxtest.h"

TEST(printenv, printenv_named_variable_prints_value_only) {
  Pipeline p;
  p.set_env(L"WINUXCMD_PRINTENV_TEST", L"expected-value");
  p.add(L"printenv.exe", {L"WINUXCMD_PRINTENV_TEST"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "expected-value\n");
}

TEST(printenv, printenv_missing_variable_returns_one) {
  Pipeline p;
  p.add(L"printenv.exe", {L"WINUXCMD_PRINTENV_DOES_NOT_EXIST"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(printenv, printenv_empty_variable_prints_empty_line_and_succeeds) {
  Pipeline p;
  p.set_env(L"WINUXCMD_PRINTENV_EMPTY_TEST", L"");
  p.add(L"printenv.exe", {L"WINUXCMD_PRINTENV_EMPTY_TEST"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, "\n");
}

TEST(printenv, printenv_null_terminates_named_variable) {
  Pipeline p;
  p.set_env(L"WINUXCMD_PRINTENV_NUL_TEST", L"nul-value");
  p.add(L"printenv.exe", {L"-0", L"WINUXCMD_PRINTENV_NUL_TEST"});

  auto r = p.run();

  std::string expected = "nul-value";
  expected.push_back('\0');
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}

TEST(printenv, printenv_null_terminates_empty_variable) {
  Pipeline p;
  p.set_env(L"WINUXCMD_PRINTENV_EMPTY_NUL_TEST", L"");
  p.add(L"printenv.exe", {L"-0", L"WINUXCMD_PRINTENV_EMPTY_NUL_TEST"});

  auto r = p.run();

  std::string expected(1, '\0');
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ(r.stdout_text, expected);
}

TEST(printenv, printenv_partial_missing_keeps_found_output_and_returns_one) {
  Pipeline p;
  p.set_env(L"WINUXCMD_PRINTENV_FOUND_TEST", L"found-value");
  p.add(L"printenv.exe",
        {L"WINUXCMD_PRINTENV_FOUND_TEST", L"WINUXCMD_PRINTENV_MISSING_TEST"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "found-value\n");
}

TEST(printenv, printenv_equal_sign_variable_name_is_silently_ignored) {
  Pipeline p;
  p.set_env(L"a=b", L"c");
  p.add(L"printenv.exe", {L"a=b"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(printenv,
     printenv_equal_sign_variable_name_keeps_other_found_output_and_returns_one) {
  Pipeline p;
  p.set_env(L"WINUXCMD_PRINTENV_KEY", L"VALUE");
  p.set_env(L"a=b", L"c");
  p.add(L"printenv.exe", {L"WINUXCMD_PRINTENV_KEY", L"a=b"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_EQ(r.stdout_text, "VALUE\n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(printenv, printenv_invalid_option_returns_two_with_gnu_style_error) {
  Pipeline p;
  p.add(L"printenv.exe", {L"-/"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 2);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "printenv: unrecognized option '-/'\n"
      "Try 'printenv --help' for more information.\n");
}
