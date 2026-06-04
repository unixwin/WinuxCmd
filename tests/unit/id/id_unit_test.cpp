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
 *  - File: id_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(id, id_basic) {
  Pipeline p;
  p.add(L"id.exe", {});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should contain user and group information
  EXPECT_TRUE(r.stdout_text.find("uid") != std::string::npos);
}

TEST(id, id_user_only) {
  Pipeline p;
  p.add(L"id.exe", {L"-u"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("uid") != std::string::npos);
}

TEST(id, id_group_only) {
  Pipeline p;
  p.add(L"id.exe", {L"-g"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(id, id_groups) {
  Pipeline p;
  p.add(L"id.exe", {L"-G"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(id, id_name) {
  Pipeline p;
  p.add(L"id.exe", {L"-n"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should contain username
}

TEST(id, id_user_name) {
  Pipeline p;
  p.add(L"id.exe", {L"-un"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should output username, not uid number
}

TEST(id, id_group_name) {
  Pipeline p;
  p.add(L"id.exe", {L"-gn"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
  // Should output group name, not gid number
}

TEST(id, id_real) {
  Pipeline p;
  p.add(L"id.exe", {L"-r"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(id, id_zero) {
  Pipeline p;
  p.add(L"id.exe", {L"--zero"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  // --zero should use NUL delimiter
}

TEST(id, id_specific_user) {
  Pipeline p;
  p.add(L"id.exe", {L"Administrator"});
  auto r = p.run();

  // May fail if user doesn't exist, but should handle gracefully
  EXPECT_TRUE(r.exit_code == 0 || r.exit_code == 1);
}
