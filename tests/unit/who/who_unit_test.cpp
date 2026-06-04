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
 *  - File: who_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(who, who_basic) {
  Pipeline p;
  p.add(L"who.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(who, who_short) {
  Pipeline p;
  p.add(L"who.exe", {L"-s"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(who, who_count) {
  Pipeline p;
  p.add(L"who.exe", {L"-q"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("# users") != std::string::npos ||
              r.stdout_text.find("1") != std::string::npos);
}

TEST(who, who_heading) {
  Pipeline p;
  p.add(L"who.exe", {L"-H"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show column headers
}

TEST(who, who_idle) {
  Pipeline p;
  p.add(L"who.exe", {L"-u"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show idle time
}

TEST(who, who_all) {
  Pipeline p;
  p.add(L"who.exe", {L"-a"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show all information
}

TEST(who, who_boot) {
  Pipeline p;
  p.add(L"who.exe", {L"-b"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show last boot time
}

TEST(who, who_dead) {
  Pipeline p;
  p.add(L"who.exe", {L"-d"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show dead processes
}

TEST(who, who_login) {
  Pipeline p;
  p.add(L"who.exe", {L"-l"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show login processes
}

TEST(who, who_process) {
  Pipeline p;
  p.add(L"who.exe", {L"-p"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show active processes
}

TEST(who, who_runlevel) {
  Pipeline p;
  p.add(L"who.exe", {L"-r"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show current runlevel
}

TEST(who, who_message) {
  Pipeline p;
  p.add(L"who.exe", {L"-T"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show message status
}

TEST(who, who_writable) {
  Pipeline p;
  p.add(L"who.exe", {L"-w"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // Should show writable status
}

TEST(who, who_lookup) {
  Pipeline p;
  p.add(L"who.exe", {L"--lookup"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --lookup is no-op on Windows but should be accepted
}
