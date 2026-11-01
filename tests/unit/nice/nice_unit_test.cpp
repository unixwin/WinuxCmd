// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(nice, nice_no_command) {
  Pipeline p;
  p.add(L"nice.exe", {});

  TEST_LOG_CMD_LIST("nice.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_EQ(r.stdout_text.find("Current priority class"), std::string::npos);
}

TEST(nice, nice_with_adjustment) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"5", L"echo.exe", L"hello"});

  TEST_LOG_CMD_LIST("nice.exe", L"-n", L"5", L"echo.exe", L"hello");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(nice, nice_with_attached_adjustment) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n5", L"echo.exe", L"hello"});

  TEST_LOG_CMD_LIST("nice.exe", L"-n5", L"echo.exe", L"hello");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(nice, nice_preserves_argument_with_spaces) {
  Pipeline p;
  p.add(L"nice.exe", {L"printf.exe", L"<%s>", L"a b"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice preserved space argument output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
  EXPECT_EQ_TEXT(r.stdout_text, "<a b>");
}

TEST(nice, nice_legacy_bare_adjustment_runs_command) {
  Pipeline p;
  p.add(L"nice.exe", {L"-1", L"echo.exe", L"-n", L"a"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice bare adjustment output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a");
}

TEST(nice, nice_legacy_double_dash_adjustment_runs_command) {
  Pipeline p;
  p.add(L"nice.exe", {L"--1", L"echo.exe", L"-n", L"a"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice double dash adjustment output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a");
}

TEST(nice, nice_legacy_plus_prefixed_adjustment_runs_command) {
  Pipeline p;
  p.add(L"nice.exe", {L"-+1", L"echo.exe", L"-n", L"a"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice plus prefixed adjustment output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "a");
}

TEST(nice, nice_huge_adjustment_is_clamped_and_still_runs_command) {
  Pipeline p;
  p.add(L"nice.exe",
        {L"-n",
         L"9999999999999999999999999999999999999999999999999999999999999999",
         L"true.exe"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice huge adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(nice, nice_huge_negative_adjustment_is_clamped_and_still_runs_command) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"-9999999999999999999999", L"true.exe"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice huge negative adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(nice, nice_invalid_adjustment_reports_usage_error) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"-2+4", L"true.exe"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice invalid adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "nice: invalid adjustment '-2+4'\n"
                 "Try 'nice --help' for more information.\n");
}

TEST(nice, nice_invalid_option_returns_125) {
  Pipeline p;
  p.add(L"nice.exe", {L"--invalid"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice invalid option stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "nice: unrecognized option '--invalid'\n"
                 "Try 'nice --help' for more information.\n");
}

TEST(nice, nice_explicit_adjustment_without_command_reports_usage_error) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"19"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice explicit adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  // [GNU] nice.c:181: the message is lowercase with no period --
  // error (0, 0, "a command must be given with an adjustment") --
  // followed by usage (EXIT_CANCELED).
  EXPECT_EQ_TEXT(r.stderr_text,
                 "nice: a command must be given with an adjustment\n"
                 "Try 'nice --help' for more information.\n");
}

TEST(nice, nice_long_adjustment_without_command_reports_usage_error) {
  Pipeline p;
  p.add(L"nice.exe", {L"--adjustment=19"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice long adjustment stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  // [GNU] nice.c:181: lowercase, no period (see the -n form above).
  EXPECT_EQ_TEXT(r.stderr_text,
                 "nice: a command must be given with an adjustment\n"
                 "Try 'nice --help' for more information.\n");
}

TEST(nice, nice_missing_command_reports_gnu_style_error_and_127) {
  TempDir tmp;

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"nice.exe", {L"definitely-not-a-command"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice missing command stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 127);
  EXPECT_TRUE(r.stdout_text.empty());
  // [GNU] nice.c:215-218: an execvp failure is reported as
  // error (0, errno, "%s", quote (argv[i])) -- the bare quotef form,
  // not timeout/stdbuf's "failed to run command" wording.
  EXPECT_EQ_TEXT(r.stderr_text,
                 "nice: 'definitely-not-a-command': No such file or "
                 "directory\n");
}


// #1359: [GNU] when the requested priority cannot be applied, nice
// prints "cannot set niceness: <reason>" to stderr and still runs the
// command, propagating the command's exit status.  Whether a raise
// can be applied depends on the caller's privileges, so the stderr
// assertion is a strict either/or: empty (the raise was applied) or
// exactly the GNU warning line (it was not); any other shape fails.
TEST(nice, nice_raise_warns_when_unapplied_and_propagates_status) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"-1", L"cmd.exe", L"/d", L"/c", L"exit", L"7"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice raise stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 7);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_TRUE(r.stderr_text.empty() ||
              r.stderr_text ==
                  "nice: cannot set niceness: Permission denied\n");
}

// #1359 control: lowering the priority is always permitted, so it
// must stay silent (GNU: no warning when setpriority succeeds).
TEST(nice, nice_lower_priority_stays_silent) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"5", L"echo.exe", L"hello"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("hello") != std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}

// #1408 audit: nice's option parsing stops at the wrapped command (GNU
// nice.c parses with a leading-'+' optstring), so the command's own
// flags pass through instead of being diagnosed as nice options.
TEST(nice, nice_wrapped_command_options_pass_through_issue1408) {
  Pipeline p;
  p.add(L"nice.exe", {L"-n", L"5", L"echo.exe", L"-n", L"hi"});

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("nice passthrough stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "hi");
  EXPECT_TRUE(r.stderr_text.empty());
}
