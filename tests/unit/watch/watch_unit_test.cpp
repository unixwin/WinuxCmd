// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(watch, watch_basic) {
  Pipeline p;
  p.add(L"watch.exe", {L"-n", L"1", L"--count", L"1", L"echo", L"test"});

  TEST_LOG_CMD_LIST("watch.exe", L"-n", L"1", L"--count", L"1", L"echo",
                    L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("watch output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(watch, watch_no_title) {
  Pipeline p;
  p.add(L"watch.exe", {L"-n", L"1", L"--count", L"1", L"-t", L"echo", L"test"});

  TEST_LOG_CMD_LIST("watch.exe", L"-n", L"1", L"--count", L"1", L"-t", L"echo",
                    L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("watch no title output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

// procps watch exits 0 when it terminates normally; the child's exit
// status is NOT propagated (watch.c only returns it under -e).
TEST(watch, watch_finite_run_returns_zero_without_errexit) {
  Pipeline p;
  p.add(L"watch.exe",
        {L"-n", L"0", L"--count", L"1", L"-t", L"cmd", L"/c", L"exit", L"7"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
}

TEST(watch, watch_errexit_returns_child_status) {
  Pipeline p;
  p.add(L"watch.exe", {L"-n", L"0", L"--count", L"1", L"-t", L"-e", L"cmd",
                       L"/c", L"exit", L"7"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 7);
}

TEST(watch, watch_differences_option) {
  Pipeline p;
  p.add(L"watch.exe", {L"--differences", L"-n", L"0", L"--count", L"1", L"-t",
                       L"echo", L"test"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("test"), std::string::npos);
}

// -c is --color in procps watch (it was a product-specific count alias
// before #1127).
TEST(watch, watch_color_option_accepted) {
  Pipeline p;
  p.add(L"watch.exe",
        {L"-n", L"0", L"--count", L"1", L"-t", L"-c", L"echo", L"test"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("test"), std::string::npos);
}

// -q N (--equexit) exits after N consecutive runs with unchanged
// output; with N=1 the second identical run already ends the watch.
TEST(watch, watch_equexit_stops_on_unchanged_output) {
  Pipeline p;
  p.add(L"watch.exe",
        {L"-n", L"0", L"--count", L"10", L"-t", L"-q", L"1", L"echo", L"same"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("same"), std::string::npos);
}

// -x (--exec) runs the command directly instead of through the shell.
TEST(watch, watch_exec_mode) {
  Pipeline p;
  p.add(L"watch.exe", {L"-n", L"0", L"--count", L"1", L"-t", L"-x", L"cmd.exe",
                       L"/c", L"echo", L"hi"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("hi"), std::string::npos);
}
