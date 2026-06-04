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
 *  - File: diff_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(diff, diff_identical) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\nworld\n");
  tmp.write("file2.txt", "hello\nworld\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(diff, diff_different) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\nworld\n");
  tmp.write("file2.txt", "hello\nthere\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stdout_text.empty());
}

TEST(diff, diff_brief) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-q", L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"-q", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff brief output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("differ") != std::string::npos);
}

TEST(diff, diff_unified) {
  TempDir tmp;
  tmp.write("file1.txt", "line1\nline2\nline3\n");
  tmp.write("file2.txt", "line1\nlineX\nline3\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-u", L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"-u", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff unified output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.find("@@") != std::string::npos);
}

TEST(diff, diff_side_by_side) {
  TempDir tmp;
  tmp.write("file1.txt", "left\nsame\n");
  tmp.write("file2.txt", "right\nsame\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-y", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_FALSE(r.stdout_text.empty());
  EXPECT_TRUE(r.stdout_text.find("left") != std::string::npos);
  EXPECT_TRUE(r.stdout_text.find("right") != std::string::npos);
}

TEST(diff, diff_ignore_all_space_unified) {
  TempDir tmp;
  tmp.write("file1.txt", "alpha beta\nsame\n");
  tmp.write("file2.txt", "alphabeta\nsame\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-w", L"-u", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(diff, diff_ignore_all_space_side_by_side) {
  TempDir tmp;
  tmp.write("file1.txt", "alpha beta\nsame\n");
  tmp.write("file2.txt", "alphabeta\nsame\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"-w", L"-y", L"file1.txt", L"file2.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}

TEST(diff, diff_wildcard_pair_expands) {
  TempDir tmp;
  tmp.write("a.txt", "same\n");
  tmp.write("b.txt", "same\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"diff.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("diff.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("diff wildcard output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stdout_text.empty());
}
