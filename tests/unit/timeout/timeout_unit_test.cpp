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

  // Should fail with missing command error
  EXPECT_NE(r.exit_code, 0);
}

TEST(timeout, timeout_invalid_duration) {
  Pipeline p;
  p.add(L"timeout.exe", {L"abc", L"echo.exe", L"test"});
  auto r = p.run();

  // Should fail with invalid duration error
  EXPECT_NE(r.exit_code, 0);
}

TEST(timeout, timeout_invalid_signal) {
  Pipeline p;
  p.add(L"timeout.exe", {L"-s", L"INVALID", L"5", L"echo.exe", L"test"});
  auto r = p.run();

  // Should fail with invalid signal error
  EXPECT_NE(r.exit_code, 0);
}
