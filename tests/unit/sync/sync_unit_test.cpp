/*
 *  Copyright © 2026 [caomengxuan666]
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software", to
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
 *  - File: sync_unit_test.cpp
 *  - Username: Administrator
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(sync, sync_all) {
  Pipeline p;
  p.add(L"sync.exe", {});

  TEST_LOG_CMD_LIST("sync.exe");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sync, sync_file) {
  TempDir tmp;
  tmp.write("test.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sync.exe", {L"test.txt"});

  TEST_LOG_CMD_LIST("sync.exe", L"test.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);

  EXPECT_EQ(r.exit_code, 0);
}

TEST(sync, sync_directory_operand_is_accepted) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sync.exe", {L"dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(sync, sync_data_option_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sync.exe", {L"-d", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(sync, sync_data_directory_operand_is_accepted) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sync.exe", {L"--data", L"dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(sync, sync_data_without_files_reports_gnu_style_error) {
  Pipeline p;
  p.add(L"sync.exe", {L"--data"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(r.stderr_text,
                 "sync: --data needs at least one argument\n");
}

TEST(sync, sync_data_and_file_system_are_mutually_exclusive) {
  TempDir tmp;
  tmp.write("test.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sync.exe", {L"--data", L"--file-system", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "sync: options --data and --file-system are mutually exclusive\n");
}

TEST(sync, sync_file_system_option_is_accepted) {
  TempDir tmp;
  tmp.write("test.txt", "hello world");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sync.exe", {L"--file-system", L"test.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(sync, sync_file_system_accepts_directory_operand) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"sync.exe", {L"--file-system", L"dir"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(r.stderr_text.empty());
}

TEST(sync, sync_unknown_option_reports_gnu_style_parse_error) {
  Pipeline p;
  p.add(L"sync.exe", {L"--bogus"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "sync: unrecognized option '--bogus'\n"
      "Try 'sync --help' for more information.\n");
}

TEST(sync, sync_nonexistent_file) {
  Pipeline p;
  p.add(L"sync.exe", {L"nonexistent.txt"});

  TEST_LOG_CMD_LIST("sync.exe", L"nonexistent.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("sync stderr", r.stderr_text);

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "sync: error opening 'nonexistent.txt': No such file or directory\n");
}

TEST(sync, sync_multiple_nonexistent_files_report_all_errors) {
  Pipeline p;
  p.add(L"sync.exe", {L"--data", L"bad1", L"bad2"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 1);
  EXPECT_TRUE(r.stdout_text.empty());
  EXPECT_EQ_TEXT(
      r.stderr_text,
      "sync: error opening 'bad1': No such file or directory\n"
      "sync: error opening 'bad2': No such file or directory\n");
}
