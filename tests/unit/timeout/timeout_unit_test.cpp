/*
 *  Copyright © 2026 [caomengxuan666]
 */
#include "framework/winuxtest.h"

TEST(timeout, timeout_basic) {
  Pipeline p;
  p.add(L"timeout.exe", {L"5", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("timeout.exe", L"5", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_zero_duration_disables_timeout) {
  Pipeline p;
  p.add(L"timeout.exe", {L"0", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("timeout.exe", L"0", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_accepts_attached_signal_name) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-sTERM", L"5", L"echo.exe", L"test"});

  TEST_LOG_CMD_LIST("timeout.exe", L"-sTERM", L"5", L"echo.exe", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_signal_kILL) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-s", L"KILL", L"5", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_signal_number) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-s", L"9", L"5", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_kill_after) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-k", L"1s", L"5", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_foreground) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-f", L"5", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_foreground_still_enforces_timeout_promptly) {
  Pipeline p;
  p.add(L"timeout.exe", {L"--foreground", L"0.1", L"sleep.exe", L"2"});

  auto start = std::chrono::steady_clock::now();
  auto r = p.run();
  auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_EQ(r.exit_code, 124);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_LT(elapsed, std::chrono::milliseconds(1200));
}

TEST(timeout, timeout_preserve_status) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-p", L"5", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_verbose) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-v", L"5", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_preserves_argument_with_spaces) {
  Pipeline p;
  p.add(L"timeout.exe", {L"5", L"printf.exe", L"<%s>", L"a b"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "<a b>");
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(timeout, timeout_verbose_reports_signal_name) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-v", L"0.1", L"sleep.exe", L"1"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 124);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "timeout: sending signal TERM to command 'sleep.exe'\n");
}

TEST(timeout, timeout_foreground_signal0_kill_after_returns_kill_status) {
  Pipeline p;
  p.add(L"timeout.exe",
        {L"--foreground", L"-s0", L"-k0.1", L"0.1", L"sleep.exe", L"2"});

  auto start = std::chrono::steady_clock::now();
  auto r = p.run();
  auto elapsed = std::chrono::steady_clock::now() - start;

  EXPECT_EQ(r.exit_code, 137);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_LT(elapsed, std::chrono::milliseconds(1500));
}

TEST(timeout, timeout_verbose_signal0_kill_after_reports_probe_then_kill) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-v", L"-s0", L"-k0.1", L"0.1", L"sleep.exe", L"2"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 137);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "timeout: sending signal 0 to command 'sleep.exe'\n"
      "timeout: sending signal KILL to command 'sleep.exe'\n");
}

TEST(timeout, timeout_duration_minutes) {
  Pipeline p;
  p.add(L"timeout.exe", {L"1m", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_duration_hours) {
  Pipeline p;
  p.add(L"timeout.exe", {L"1h", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(timeout, timeout_missing_command) {
  Pipeline p;
  p.add(L"timeout.exe", {L"5"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "timeout: missing command\n"
      "Try 'timeout --help' for more information.\n");
}

TEST(timeout, timeout_missing_operand_reports_help_hint) {
  Pipeline p;
  p.add(L"timeout.exe", {});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "timeout: missing operand\n"
      "Try 'timeout --help' for more information.\n");
}

TEST(timeout, timeout_invalid_duration) {
  Pipeline p;
  p.add(L"timeout.exe", {L"abc", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "timeout: invalid time interval 'abc'\n");
}

TEST(timeout, timeout_invalid_signal) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-s", L"INVALID", L"5", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "timeout: 'INVALID': invalid signal\n");
}

TEST(timeout, timeout_invalid_kill_after_reports_gnu_style_usage_error) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-k", L"xyz", L"5", L"echo.exe", L"test"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text, "timeout: invalid time interval 'xyz'\n");
}

TEST(timeout, timeout_invalid_option_returns_125_with_help_hint) {
  Pipeline p;
  p.add(L"timeout.exe", {L"--definitely-invalid"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 125);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "timeout: unrecognized option '--definitely-invalid'\n"
      "Try 'timeout --help' for more information.\n");
}
