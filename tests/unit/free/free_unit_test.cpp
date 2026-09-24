// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(free, free_default) {
  Pipeline p;
  p.add(L"free.exe", {});

  TEST_LOG_CMD_LIST("free.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("free output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(free, free_bytes) {
  Pipeline p;
  p.add(L"free.exe", {L"-b"});

  TEST_LOG_CMD_LIST("free.exe", L"-b");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("free bytes output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(free, free_human_readable) {
  Pipeline p;
  p.add(L"free.exe", {L"-h"});

  TEST_LOG_CMD_LIST("free.exe", L"-h");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("free human readable output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(free, free_megabytes) {
  Pipeline p;
  p.add(L"free.exe", {L"-m"});

  TEST_LOG_CMD_LIST("free.exe", L"-m");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("free megabytes output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(free, free_new_display_options) {
  for (const auto& args :
       {std::vector<std::wstring>{L"--mega"},
        std::vector<std::wstring>{L"--lohi"},
        std::vector<std::wstring>{L"--seconds", L"0", L"--count", L"1"}}) {
    Pipeline p;
    p.add(L"free.exe", args);
    auto r = p.run();
    EXPECT_EQ(r.exit_code, 0);
    EXPECT_TRUE(r.stderr_text.empty());
  }
}
