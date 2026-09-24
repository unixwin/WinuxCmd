// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(getconf, getconf_all) {
  Pipeline p;
  p.add(L"getconf.exe", {L"-a"});

  TEST_LOG_CMD_LIST("getconf.exe", L"-a");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("getconf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("System configuration") != std::string::npos);
}

TEST(getconf, getconf_page_size) {
  Pipeline p;
  p.add(L"getconf.exe", {L"PAGE_SIZE"});

  TEST_LOG_CMD_LIST("getconf.exe", L"PAGE_SIZE");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("getconf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(getconf, getconf_path_max) {
  Pipeline p;
  p.add(L"getconf.exe", {L"PATH_MAX"});

  TEST_LOG_CMD_LIST("getconf.exe", L"PATH_MAX");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("getconf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "260\n");
}

TEST(getconf, getconf_nprocessors) {
  Pipeline p;
  p.add(L"getconf.exe", {L"NPROCESSORS_ONLN"});

  TEST_LOG_CMD_LIST("getconf.exe", L"NPROCESSORS_ONLN");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("getconf output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  int num = std::stoi(r.stdout_text);
  EXPECT_GT(num, 0);
}
