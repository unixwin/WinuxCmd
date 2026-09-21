// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(tzset, tzset_display_current) {
  Pipeline p;
  p.add(L"tzset.exe", {});

  TEST_LOG_CMD_LIST("tzset.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tzset output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("TZ=") != std::string::npos);
}

TEST(tzset, tzset_set_timezone) {
  Pipeline p;
  p.add(L"tzset.exe", {L"UTC"});

  TEST_LOG_CMD_LIST("tzset.exe", L"UTC");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}
