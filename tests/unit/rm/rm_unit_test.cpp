/*
 *  Copyright © 2026 WinuxCmd
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 *  - File: rm_unit_test.cpp
 *  - CopyrightYear: 2026
 */
#include "framework/winuxtest.h"

TEST(rm, rm_basic) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"file.txt"});

  TEST_LOG_CMD_LIST("rm.exe", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the file was removed
  bool file_exists = std::filesystem::exists(tmp.path / "file.txt");
  EXPECT_TRUE(!file_exists);
}

TEST(rm, rm_recursive) {
  TempDir tmp;
  std::filesystem::create_directory(tmp.path / "dir1");
  tmp.write("dir1/file.txt", "content");

  TEST_LOG_FILE_CONTENT("dir1/file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-r", L"dir1"});

  TEST_LOG_CMD_LIST("rm.exe", L"-r", L"dir1");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm.exe -r output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the directory and its contents were removed
  bool dir_exists = std::filesystem::exists(tmp.path / "dir1");
  EXPECT_TRUE(!dir_exists);
}

TEST(rm, rm_force) {
  TempDir tmp;
  tmp.write("file.txt", "content");

  TEST_LOG_FILE_CONTENT("file.txt", "content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"-f", L"file.txt"});

  TEST_LOG_CMD_LIST("rm.exe", L"-f", L"file.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm.exe -f output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify the file was removed
  bool file_exists = std::filesystem::exists(tmp.path / "file.txt");
  EXPECT_TRUE(!file_exists);
}

TEST(rm, rm_multiple_files) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  tmp.write("file3.txt", "content3");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");
  TEST_LOG_FILE_CONTENT("file3.txt", "content3");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"file1.txt", L"file2.txt", L"file3.txt"});

  TEST_LOG_CMD_LIST("rm.exe", L"file1.txt", L"file2.txt", L"file3.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm.exe output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify all files were removed
  bool file1_exists = std::filesystem::exists(tmp.path / "file1.txt");
  bool file2_exists = std::filesystem::exists(tmp.path / "file2.txt");
  bool file3_exists = std::filesystem::exists(tmp.path / "file3.txt");
  EXPECT_TRUE(!file1_exists);
  EXPECT_TRUE(!file2_exists);
  EXPECT_TRUE(!file3_exists);
}

TEST(rm, rm_wildcard) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  tmp.write("keep.log", "log content");

  TEST_LOG_FILE_CONTENT("file1.txt", "content1");
  TEST_LOG_FILE_CONTENT("file2.txt", "content2");
  TEST_LOG_FILE_CONTENT("keep.log", "log content");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.add(L"rm.exe", {L"*.txt"});

  TEST_LOG_CMD_LIST("rm.exe", L"*.txt");

  auto r = p.run();

  TEST_LOG_EXIT_CODE(r);
  TEST_LOG("rm output", r.stdout_text);

  EXPECT_EQ(r.exit_code, 0);

  // Verify .txt files were removed but .log file remains
  bool txt1_exists = std::filesystem::exists(tmp.path / "file1.txt");
  bool txt2_exists = std::filesystem::exists(tmp.path / "file2.txt");
  bool log_exists = std::filesystem::exists(tmp.path / "keep.log");
  EXPECT_TRUE(!txt1_exists);
  EXPECT_TRUE(!txt2_exists);
  EXPECT_TRUE(log_exists);
}

TEST(rm, rm_interactive_once_declines_bulk_remove) {
  TempDir tmp;
  tmp.write("file1.txt", "content1");
  tmp.write("file2.txt", "content2");
  tmp.write("file3.txt", "content3");
  tmp.write("file4.txt", "content4");

  Pipeline p;
  p.set_cwd(tmp.wpath());
  p.set_stdin("n\n");
  p.add(L"rm.exe", {L"-I", L"file1.txt", L"file2.txt", L"file3.txt", L"file4.txt"});

  auto r = p.run();

  EXPECT_EQ(r.exit_code, 0);
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file1.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file2.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file3.txt"));
  EXPECT_TRUE(std::filesystem::exists(tmp.path / "file4.txt"));
}
