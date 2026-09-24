// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(watch, watch_basic) {
  Pipeline p;
  p.add(L"watch.exe", {L"-n", L"1", L"-c", L"1", L"echo", L"test"});

  TEST_LOG_CMD_LIST("watch.exe", L"-n", L"1", L"-c", L"1", L"echo", L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("watch output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(watch, watch_no_title) {
  Pipeline p;
  p.add(L"watch.exe", {L"-n", L"1", L"-c", L"1", L"-t", L"echo", L"test"});

  TEST_LOG_CMD_LIST("watch.exe", L"-n", L"1", L"-c", L"1", L"-t", L"echo",
                    L"test");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("watch no title output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(watch, watch_finite_run_returns_child_status) {
  Pipeline p;
  p.add(L"watch.exe",
        {L"-n", L"0", L"-c", L"1", L"-t", L"cmd", L"/c", L"exit", L"7"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 7);
}

TEST(watch, watch_differences_option) {
  Pipeline p;
  p.add(L"watch.exe",
        {L"--differences", L"-n", L"0", L"-c", L"1", L"-t", L"echo", L"test"});
  auto r = p.run();
  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("test"), std::string::npos);
}
