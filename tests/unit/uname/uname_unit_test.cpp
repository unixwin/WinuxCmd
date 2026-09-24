// SPDX-License-Identifier: MIT
// Copyright (c) 2026 caomengxuan666 <caomengxuan666@users.noreply.github.com>
#include "framework/winuxtest.h"

TEST(uname, uname_basic) {
  Pipeline p;
  p.add(L"uname.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(uname, uname_all) {
  Pipeline p;
  p.add(L"uname.exe", {L"-a"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should contain kernel name, hostname, release, version, machine.
  // [GNU] sysname is "Windows_NT", matching uname -o.
  EXPECT_TRUE(r.stdout_text.find("Windows_NT") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("MSWindows_NT") == std::string::npos);
}

TEST(uname, uname_machine) {
  Pipeline p;
  p.add(L"uname.exe", {L"-m"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should show machine architecture
  EXPECT_TRUE(r.stdout_text.find("x86_64") != std::string::npos ||
              r.stdout_text.find("i386") != std::string::npos ||
              r.stdout_text.find("aarch64") != std::string::npos);
}

// [GNU] --sysname and --release are obsolescent hidden aliases for -s
// and -r accepted for back-compat (issue #1074).
TEST(uname, uname_obsolescent_sysname_release_aliases) {
  Pipeline ps;
  ps.add(L"uname.exe", {L"--sysname"});
  Pipeline pc;
  pc.add(L"uname.exe", {L"-s"});
  auto rs = ps.run();
  auto rc = pc.run();
  EXPECT_EQ(rs.exit_code, 0);
  EXPECT_EQ_TEXT(rs.stdout_text, rc.stdout_text);

  Pipeline pr;
  pr.add(L"uname.exe", {L"--release"});
  Pipeline prc;
  prc.add(L"uname.exe", {L"-r"});
  auto rr = pr.run();
  auto rrc = prc.run();
  EXPECT_EQ(rr.exit_code, 0);
  EXPECT_EQ_TEXT(rr.stdout_text, rrc.stdout_text);
}
