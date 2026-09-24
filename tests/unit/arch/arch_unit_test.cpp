// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(arch, arch_basic) {
  Pipeline p;
  p.add(L"arch.exe", {});

  TEST_LOG_CMD_LIST("arch.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("arch output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  std::string arch = r.stdout_text;
  arch.pop_back();  // remove newline
  EXPECT_TRUE(arch == "x86_64" || arch == "i386" || arch == "arm" ||
              arch == "aarch64" || arch == "ia64" || arch == "unknown");
}

TEST(arch, arch_version_succeeds) {
  Pipeline p;
  p.add(L"arch.exe", {L"--version"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_NE(r.stdout_text.find("arch (WinuxCmd)"), std::string::npos);
  EXPECT_TRUE(r.stderr_text.empty());
}
