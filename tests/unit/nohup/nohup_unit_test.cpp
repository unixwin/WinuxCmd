// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(nohup, nohup_missing_operand) {
  Pipeline p;
  p.add(L"nohup.exe", {});

  TEST_LOG_CMD_LIST("nohup.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nohup: missing operand\nTry 'nohup --help' for more information.\n");
}

TEST(nohup, nohup_missing_operand_posixly_correct_returns_127) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"nohup.exe", {});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup posix missing operand stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nohup: missing operand\nTry 'nohup --help' for more information.\n");
}

TEST(nohup, nohup_basic) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("nohup.exe", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test") != std::string::npos);
}

TEST(nohup, nohup_preserves_argument_with_spaces) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"printf.exe", L"<%s>", L"a b"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup preserved space argument output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "<a b>");
}

TEST(nohup, nohup_invalid_option_default_returns_125) {
  Pipeline p;
  p.add(L"nohup.exe", {L"--invalid"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup invalid option stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "nohup: unrecognized option '--invalid'\n"
                 "Try 'nohup --help' for more information.\n");
}

TEST(nohup, nohup_invalid_option_posixly_correct_returns_127) {
  Pipeline p;
  p.set_env(L"POSIXLY_CORRECT", L"1");
  p.add(L"nohup.exe", {L"--invalid"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup posix invalid option stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "nohup: unrecognized option '--invalid'\n"
                 "Try 'nohup --help' for more information.\n");
}

TEST(nohup, nohup_missing_command_reports_gnu_style_error_and_127) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"definitely-not-a-command"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup missing command stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "nohup: failed to run command 'definitely-not-a-command': No such file "
      "or directory\n");
}

// [GNU] coreutils nohup parses with a leading-'+' getopt: the wrapped
// command keeps its own options.  Regression for the observed
// `nohup echo -n hi` -> "nohup: invalid option -- 'n'" defect, where the
// wrapped command never ran.
TEST(nohup, nohup_passes_wrapped_command_options_through) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"echo.exe", L"-n", L"hi"});

  TEST_LOG_CMD_LIST("nohup.exe", L"echo.exe", L"-n", L"hi");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup option pass-through stdout", r.stdout_text);
  TEST_LOG("nohup option pass-through stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "hi");
}

TEST(nohup, nohup_wrapped_command_own_help_is_not_intercepted) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"printf.exe", L"--help"});

  TEST_LOG_CMD_LIST("nohup.exe", L"printf.exe", L"--help");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("wrapped printf help stdout", r.stdout_text);
  TEST_LOG("wrapped printf help stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  // The child's help must be shown, not nohup's own help.
  EXPECT_TRUE(r.stdout_text.find("nohup") == std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("printf") != std::string::npos);
}

TEST(nohup, nohup_wrapped_command_own_version_is_not_intercepted) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"printf.exe", L"--version"});

  TEST_LOG_CMD_LIST("nohup.exe", L"printf.exe", L"--version");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("wrapped printf version stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("printf (WinuxCmd)") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("nohup (WinuxCmd)") == std::string::npos);
}

TEST(nohup, nohup_own_help_before_command_still_prints) {
  Pipeline p;
  p.add(L"nohup.exe", {L"--help"});

  TEST_LOG_CMD_LIST("nohup.exe", L"--help");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup help stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("nohup COMMAND [ARG]...") !=
              std::string::npos);
}

TEST(nohup, nohup_own_version_before_command_still_prints) {
  Pipeline p;
  p.add(L"nohup.exe", {L"--version"});

  TEST_LOG_CMD_LIST("nohup.exe", L"--version");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup version stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("nohup (WinuxCmd)") != std::string::npos);
}

TEST(nohup, nohup_double_dash_before_command_still_runs_command) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nohup.exe", {L"--", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("nohup.exe", L"--", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nohup double dash stdout", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("test") != std::string::npos);
}
