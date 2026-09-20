/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 *  deal in the Software without restriction, including without limitation the
 *  rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 *  sell copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 *  all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 *  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 *  IN THE SOFTWARE.
 *
 *  - File: uname_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
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
