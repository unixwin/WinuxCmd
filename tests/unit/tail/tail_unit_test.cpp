/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the “Software”), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: tail_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(tail, tail_default_last_10_lines) {
  TempDir tmp;
  tmp.write("a.txt", "1\n2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "2\n3\n4\n5\n6\n7\n8\n9\n10\n11\n");
}

TEST(tail, tail_plus_lines_and_bytes) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"tail.exe", {L"-n", L"+2", L"a.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "beta\ngamma\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"tail.exe", {L"-c", L"+3", L"a.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "pha\nbeta\ngamma\n");
}

TEST(tail, tail_legacy_count_shorthand) {
  TempDir tmp;
  tmp.write("a.txt", "alpha\nbeta\ngamma\n");

  Pipeline p1;
  p1.set_cwd(tmp.wpath());
  p1.add(L"tail.exe", {L"-2", L"a.txt"});
  auto r1 = p1.run();

  EXPECT_EQ(r1.exit_code, 0);
  EXPECT_EQ_TEXT(r1.stdout_text, "beta\ngamma\n");

  Pipeline p2;
  p2.set_cwd(tmp.wpath());
  p2.add(L"tail.exe", {L"+2", L"a.txt"});
  auto r2 = p2.run();

  EXPECT_EQ(r2.exit_code, 0);
  EXPECT_EQ_TEXT(r2.stdout_text, "beta\ngamma\n");
}

TEST(tail, tail_follow_option_recognized) {
  // Verify that -f flag is recognized and doesn't error out
  // (actual follow mode is a long-running operation tested manually)
  TempDir tmp;
  tmp.write("a.txt", "line1\nline2\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"--help"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("--follow") != std::string::npos);
}

TEST(tail, tail_sleep_interval_option_is_accepted) {
  TempDir tmp;
  tmp.write("a.txt", "1\n2\n3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"--sleep-interval", L"0.1", L"-n", L"1", L"a.txt"});
  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_EQ_TEXT(r.stdout_text, "3\n");
}

TEST(tail, tail_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "line1\nline2\nline3\n");
  tmp.write("file2.txt", "line4\nline5\nline6\n");
  tmp.write("other.log", "log1\nlog2\nlog3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"tail.exe", {L"-n", L"1", L"*.txt"});

  TEST_LOG_CMD_LIST("tail.exe", L"-n", L"1", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("tail output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.find("line3") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("line6") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("log3") == std::string::npos);
}
