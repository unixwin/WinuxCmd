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
 *  - File: chmod_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(chmod, chmod_numeric_644) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"644", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"644", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_numeric_755) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"755", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"755", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_symbolic_add) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"u+w", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"u+w", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_symbolic_remove) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"go-w", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"go-w", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_verbose) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-v", L"644", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-v", L"644", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_recursive) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");
  std::filesystem::create_directory(tmp.path / "subdir");
  tmp.write("subdir/file.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-R", L"755", L"subdir"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-R", L"755", L"subdir");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_multiple_files) {
  TempDir tmp;
  tmp.write("file1.txt", "hello\n");
  tmp.write("file2.txt", "world\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"644", L"file1.txt", L"file2.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"644", L"file1.txt", L"file2.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(chmod, chmod_changes) {
  TempDir tmp;
  tmp.write("test.txt", "hello\n");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"chmod.exe", {L"-c", L"755", L"test.txt"});

  TEST_LOG_CMD_LIST("chmod.exe", L"-c", L"755", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("chmod output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);
}
