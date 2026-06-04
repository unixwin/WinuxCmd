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
