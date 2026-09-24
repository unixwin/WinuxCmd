#include "framework/winuxtest.h"

TEST(envsubst, envsubst_replaces_dollar_variables_from_stdin) {
  Pipeline p;
  p.set_env(L"WINUX_ENV_A", L"alpha");
  p.set_env(L"WINUX_ENV_B", L"beta");
  p.set_stdin("$WINUX_ENV_A ${WINUX_ENV_B} $MISSING\n");
  p.add(L"envsubst.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha beta \n");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(envsubst, envsubst_shell_format_limits_replacements) {
  Pipeline p;
  p.set_env(L"WINUX_ENV_A", L"alpha");
  p.set_env(L"WINUX_ENV_B", L"beta");
  p.set_stdin("$WINUX_ENV_A $WINUX_ENV_B\n");
  p.add(L"envsubst.exe", {L"$WINUX_ENV_A"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "alpha $WINUX_ENV_B\n");
}

TEST(envsubst, envsubst_variables_lists_shell_format_names) {
  Pipeline p;
  p.add(L"envsubst.exe", {L"--variables", L"$WINUX_ENV_A ${WINUX_ENV_B}"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("WINUX_ENV_A\n") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("WINUX_ENV_B\n") != std::string::npos);
}
